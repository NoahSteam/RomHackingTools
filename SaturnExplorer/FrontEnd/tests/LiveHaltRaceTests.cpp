// The halt handshake between the emulate thread and the exporter's server thread: a stop must
// never be lost, a halt must be told from a re-report of the previous one, and an instruction
// step must count what retired. Real exporter over the default unix socket, built with the halt
// gate spinning (SE_EXPORT_SPIN_GATE) so the emulate thread notices a release at once -- the race
// under test lives in the few hundred nanoseconds after the server thread releases it, and the
// production 2 ms sleep hides it.
//
//  - RUN / IST must clear the old stop BEFORE releasing the CPU. The other order let a CPU that
//    re-halted immediately publish its new stop in the gap and have it erased by the clear that
//    followed: paused, with no reason.
//  - Every published halt takes the next sequence number (control block +40), which a resume does
//    not reset, so a halt at the same PC as the last is still a different halt.
//  - SeExportInsnStepTick counts retired instructions: a taken branch to itself retires without
//    moving the PC, while a bus-stalled instruction presents the same PC without retiring.
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

extern "C" {
#include "se_export.h"
extern void (*SeExportTestGateHook)(void);   // se_export.c, SE_EXPORT_SPIN_GATE builds only
}
#include "SeLiveProtocol.h"

namespace
{
int gFailures = 0;
void Check(bool ok, const char* what)
{
    if (ok) return;
    std::cerr << "FAIL: " << what << '\n';
    ++gFailures;
}

void Sleep(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

uint32_t Rd32(const uint8_t* p)
{
    return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24);
}

int ConnectRaw()
{
    for (int i = 0; i < 400; ++i)
    {
        const int fd = ::socket(AF_UNIX, SOCK_STREAM, 0);
        if (fd < 0) return -1;
        sockaddr_un addr;
        std::memset(&addr, 0, sizeof(addr));
        addr.sun_family = AF_UNIX;
        std::strncpy(addr.sun_path, SE_LIVE_DEFAULT_SOCK_PATH, sizeof(addr.sun_path) - 1);
        if (::connect(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0) return fd;
        ::close(fd);
        Sleep(5);
    }
    return -1;
}

bool RecvAll(int fd, uint8_t* d, size_t n)
{
    while (n)
    {
        const ssize_t r = ::recv(fd, d, n, 0);
        if (r <= 0) return false;
        d += r; n -= static_cast<size_t>(r);
    }
    return true;
}

bool SkipBytes(int fd, size_t n)
{
    uint8_t sink[65536];
    while (n)
    {
        const size_t k = n < sizeof(sink) ? n : sizeof(sink);
        if (!RecvAll(fd, sink, k)) return false;
        n -= k;
    }
    return true;
}

struct Ctl
{
    bool     ok = false;
    uint32_t paused = 0, reason = 0, cpu = 0, pc = 0, seq = 0;
};

// One attached client, kept for the whole run. It has to stay: when a client goes away the
// exporter puts the emulator back to free-running (pause, step and stop all cleared), so a
// request-per-connection would wipe the very state under test between one step and the next.
// Every reply is read to its end to keep the stream aligned.
class Client
{
public:
    bool Connect() { mFd = ConnectRaw(); return mFd >= 0; }
    ~Client() { if (mFd >= 0) ::close(mFd); }

    Ctl Exchange(const char* verb, uint32_t arg, const std::vector<uint8_t>& payload = {})
    {
        Ctl c;
        const uint8_t req[8] = { uint8_t(verb[0]), uint8_t(verb[1]), uint8_t(verb[2]), uint8_t(verb[3]),
                                 uint8_t(arg), uint8_t(arg >> 8), uint8_t(arg >> 16), uint8_t(arg >> 24) };
        if (::send(mFd, req, sizeof(req), MSG_NOSIGNAL) != static_cast<ssize_t>(sizeof(req))) return c;
        if (!payload.empty() &&
            ::send(mFd, payload.data(), payload.size(), MSG_NOSIGNAL) != static_cast<ssize_t>(payload.size()))
            return c;
        uint8_t hdr[SE_LIVE_HEADER_LEN];
        if (!RecvAll(mFd, hdr, sizeof(hdr)) || Rd32(hdr + 4) != SE_LIVE_VERSION) return c;
        // v1 v2 cram vdp2struct vdp1regs wramLow wramHigh fb, then the control block.
        size_t before = 0;
        for (int i = 0; i < 8; ++i) before += Rd32(hdr + 8 + 4 * i);
        const uint32_t ct = Rd32(hdr + 40);
        const uint32_t sh = Rd32(hdr + 44);
        std::vector<uint8_t> ctl(ct);
        if (ct < 44 || !SkipBytes(mFd, before) || !RecvAll(mFd, ctl.data(), ct)) return c;
        c.paused = Rd32(ctl.data());
        c.reason = Rd32(ctl.data() + 12);
        c.cpu    = Rd32(ctl.data() + 16);
        c.pc     = Rd32(ctl.data() + 20);
        c.seq    = Rd32(ctl.data() + 40);
        if (!SkipBytes(mFd, sh) || !SkipTrailer()) return c;
        c.ok = true;
        return c;
    }

private:
    bool Count(uint32_t& n)
    {
        uint8_t b[4];
        if (!RecvAll(mFd, b, 4)) return false;
        n = Rd32(b);
        return true;
    }
    // The trailing blocks, in the order the server sends them (v8..v17).
    bool SkipTrailer()
    {
        uint32_t n;
        if (!Count(n) || !SkipBytes(mFd, size_t(n) * SE_LIVE_EVENT_LEN)) return false;            // events
        for (int cpu = 0; cpu < 2; ++cpu)                                                        // call stacks
            if (!Count(n) || !SkipBytes(mFd, size_t(n) * SE_LIVE_CALLFRAME_LEN)) return false;
        if (!SkipBytes(mFd, SE_LIVE_KEYMAP_LEN)) return false;                                   // key map
        if (!Count(n) || !SkipBytes(mFd, size_t(n) * SE_LIVE_LOG_LINE_LEN)) return false;        // log
        for (int blk = 0; blk < 3; ++blk)                                                        // sound, SCSP, CD
            if (!Count(n) || !SkipBytes(mFd, n)) return false;
        if (!Count(n)) return false;                                                             // state blocks
        for (uint32_t i = 0; i < n; ++i)
        {
            uint8_t h[SE_LIVE_STATE_HDR_LEN];
            if (!RecvAll(mFd, h, sizeof(h)) || !SkipBytes(mFd, Rd32(h + 12))) return false;
        }
        if (!Count(n) || !SkipBytes(mFd, size_t(n) * SE_LIVE_EMU_SLOT_LEN)) return false;        // slots
        return true;
    }
    int mFd = -1;
};

// The emulate thread's side of a halt, as the per-instruction hook plays it: publish the stop
// (which raises the pause), park in the gate until the client releases it, and then -- the case
// that matters -- halt AGAIN at once, either because an instruction step was requested (and its one
// instruction retires) or because the next instruction is another breakpoint.
class HaltingCpu
{
public:
    void Start()
    {
        mThread = std::thread([this] {
            uint32_t pc = 0x06001000u;
            SeExportNotifyStop(0, pc);
            while (!mStop.load())
            {
                if (!SeExportGateFrame()) continue;   // still parked (spins: SE_EXPORT_SPIN_GATE)
                ++mReleased;
                pc += 2;
                if (SeExportInsnStepBegin() && SeExportInsnStepTick(0, pc, 0))
                    SeExportNotifyStep(0, pc);
                else
                    SeExportNotifyStop(0, pc);
                ++mHalts;
            }
        });
    }
    ~HaltingCpu()
    {
        mStop = true;
        if (mThread.joinable()) mThread.join();
    }
    uint32_t Halts() const { return mHalts.load(); }

private:
    std::atomic<bool>     mStop{false};
    std::atomic<uint32_t> mReleased{0};
    std::atomic<uint32_t> mHalts{0};
    std::thread           mThread;
};

// A halt that comes straight back after a release must be visible. The invariant, at every reply:
// a paused emulator that stopped for a reason says what the reason is. (Paused with NONE is a
// user pause, which this CPU never does, so here it can only be a lost stop.)
void TestAnImmediateStopSurvivesTheRelease(Client& cl)
{
    HaltingCpu cpu;
    cpu.Start();
    Sleep(50);

    int lost = 0, replies = 0;
    for (int i = 0; i < 400; ++i)
    {
        const bool step = (i % 2) != 0;
        const Ctl c = cl.Exchange(step ? SE_LIVE_VERB_ISTEP : SE_LIVE_VERB_RESUME, 1);
        if (!c.ok) { Check(false, "a reply to the release request"); break; }
        ++replies;
        if (c.paused != 0 && c.reason == SE_LIVE_STOP_NONE) ++lost;
    }
    Check(replies == 400, "all 400 release requests were answered");
    Check(lost == 0, "no reply reports a paused emulator whose stop was erased by the release");

    // And when it has come to rest, the final state agrees with itself.
    Sleep(50);
    const Ctl fin = cl.Exchange(SE_LIVE_VERB_GET, 0);
    Check(fin.ok && fin.paused == 1 && fin.reason != SE_LIVE_STOP_NONE,
          "at rest it is paused WITH a stop");
    Check(cpu.Halts() >= 100, "the emulate thread really did re-halt on each release");
}

// Each published halt takes the next number, a resume leaves it alone, and the same PC twice is
// still two halts.
void TestStopSequence(Client& cl)
{
    SeExportNotifyStop(0, 0x06002000u);
    const Ctl a = cl.Exchange(SE_LIVE_VERB_GET, 0);
    Check(a.ok && a.reason == SE_LIVE_STOP_EXEC_BP && a.pc == 0x06002000u && a.paused == 1,
          "a stop is reported with its PC");

    const Ctl r = cl.Exchange(SE_LIVE_VERB_RESUME, 0);
    Check(r.ok && r.reason == SE_LIVE_STOP_NONE, "a resume clears the reason");
    Check(r.seq == a.seq, "and does not move the sequence number");

    SeExportNotifyStop(0, 0x06002000u);   // the very same address
    const Ctl b = cl.Exchange(SE_LIVE_VERB_GET, 0);
    Check(b.ok && b.reason == SE_LIVE_STOP_EXEC_BP && b.pc == a.pc, "a second halt at the same PC");
    Check(b.seq != a.seq, "is told apart from the first by its sequence number");
    Check(((b.seq - a.seq) & 0x0FFFFFFFu) == 1u, "it is the next number");

    SeExportNotifyStep(1, 0x06002004u);
    const Ctl s = cl.Exchange(SE_LIVE_VERB_GET, 0);
    Check(s.ok && s.reason == SE_LIVE_STOP_STEP && s.cpu == 1 && s.seq != b.seq,
          "a step halt on the slave is a new, distinct halt");

    // An SCU-DMA watchpoint says so: no instruction made the access, so the client must not have
    // to guess the cause from the instruction at the PC.
    SeExportNotifyDmaStop(0, 0x06002008u);
    const Ctl d = cl.Exchange(SE_LIVE_VERB_GET, 0);
    Check(d.ok && d.reason == SE_LIVE_STOP_DMA_WATCH && d.pc == 0x06002008u && d.seq != s.seq,
          "a DMA watchpoint halt is reported as its own reason");
    cl.Exchange(SE_LIVE_VERB_RESUME, 0);
}

// Park the emulate thread's role on this thread: take the release the IST verb grants.
void ReleaseForStep(Client& cl, uint32_t count)
{
    const Ctl c = cl.Exchange(SE_LIVE_VERB_ISTEP, count);
    Check(c.ok, "the step request was answered");
    while (!SeExportGateFrame()) { }
    Check(SeExportInsnStepBegin() == 1, "the step is armed once the gate releases");
}

void TestInstructionStepCountsRetirement(Client& cl)
{
    constexpr uint32_t A = 0x06003000u;

    // The ordinary case: the halted instruction runs, and the next presentation is a different PC.
    SeExportNotifyStop(0, A);
    ReleaseForStep(cl, 1);
    Check(SeExportInsnStepTick(0, A + 2, 0) == 1, "one instruction: the next PC completes it");

    // A bus-stalled instruction presents the same PC again and again without retiring.
    SeExportNotifyStop(0, A);
    ReleaseForStep(cl, 1);
    int early = 0;
    for (int i = 0; i < 1000; ++i) early += SeExportInsnStepTick(0, A, 0);
    Check(early == 0, "a thousand presentations of an unretired PC spend nothing");
    Check(SeExportInsnStepTick(0, A + 2, 0) == 1, "and the step completes when the PC moves on");

    // A taken branch to itself retires an instruction and leaves the PC where it was. Reproduces
    // the failure: a PC comparison alone left the budget untouched for ever.
    SeExportNotifyStop(0, A);
    ReleaseForStep(cl, 1);
    Check(SeExportInsnStepTick(0, A, 1) == 1,
          "a taken branch to itself retires, so a one-instruction step completes");

    // Two instructions: the self-branch counts, and so does what follows it.
    SeExportNotifyStop(0, A);
    ReleaseForStep(cl, 2);
    Check(SeExportInsnStepTick(0, A, 1) == 0, "the self-branch is the first");
    Check(SeExportInsnStepTick(0, A, 0) == 0, "an unretired repeat after it is not the second");
    Check(SeExportInsnStepTick(0, A + 2, 0) == 1, "the next PC is");

    // A halt between instructions (an SCU-DMA watchpoint): the instruction at the halt PC has NOT
    // run, so its first presentation is not a retirement even if it is a self-branch...
    SeExportNotifyDmaStop(0, A);
    ReleaseForStep(cl, 1);
    Check(SeExportInsnStepTick(0, A, 1) == 0, "a pending self-branch presenting itself retires nothing");
    // ...but once it has been presented, a repeat of the branch is the branch running.
    Check(SeExportInsnStepTick(0, A, 1) == 1, "its next presentation is the retirement");

    // The pending instruction is stalled behind the DMA, then runs and moves on.
    SeExportNotifyDmaStop(0, A);
    ReleaseForStep(cl, 1);
    int stalled = 0;
    for (int i = 0; i < 100; ++i) stalled += SeExportInsnStepTick(0, A, 0);
    Check(stalled == 0, "a DMA-stalled pending instruction spends nothing");
    Check(SeExportInsnStepTick(0, A + 2, 0) == 1, "and the step completes when it retires");
    cl.Exchange(SE_LIVE_VERB_RESUME, 0);
}

// Another CPU halting ends the step in progress: its leftover budget must not survive into the
// next resume and halt a CPU the user never asked to step.
void TestAHaltOnTheOtherCpuEndsTheStep(Client& cl)
{
    constexpr uint32_t A = 0x06004000u;
    SeExportNotifyStop(0, A);
    ReleaseForStep(cl, 5);
    Check(SeExportInsnStepTick(0, A + 2, 0) == 0, "one of five instructions");
    SeExportNotifyStop(1, 0x06005000u);   // the slave hits a breakpoint
    Check(SeExportInsnStepTick(0, A + 4, 0) == 0 && SeExportInsnStepTick(0, A + 6, 0) == 0 &&
          SeExportInsnStepTick(0, A + 8, 0) == 0 && SeExportInsnStepTick(0, A + 10, 0) == 0,
          "the master's step does not carry on after the slave's halt");
    cl.Exchange(SE_LIVE_VERB_RESUME, 0);
}
// ---- debug-hook installs --------------------------------------------------------------------
// The BKP and TRC verbs arrive on a server thread, but what they install is read by the emulate
// thread on every instruction. The exporter therefore publishes each set whole and the EMULATE thread
// runs the install hooks: on any other thread a descriptor's id could be paired with another's CPU
// and address (the CPU scanning half of one set and half of the next), or the per-instruction
// callback armed against a table being replaced. The fakes below are the install hooks; every call
// checks which thread it is on and that the set it was handed is one set.
std::atomic<bool>     gEmuKnown{false};
std::thread::id       gEmuThread;
std::atomic<int>      gOffThread{0};      // hook calls made on a thread other than the emulate thread
std::atomic<int>      gTpCalls{0}, gBadTpSets{0}, gBpSets{0}, gBadBpSets{0};
std::atomic<uint32_t> gLastTpBase{0xFFFFFFFFu}, gLastBpBase{0xFFFFFFFFu};

void NoteThread()
{
    if (!gEmuKnown.load() || std::this_thread::get_id() != gEmuThread) ++gOffThread;
}

// Set K has descriptors id = K*16 + i, with cpu = id & 1 and address = 0x06000000 + id*4: each field
// is derivable from the id, so a descriptor mixed from two sets cannot be mistaken for whole.
void FakeSetTracepoints(unsigned int count, const unsigned char* d)
{
    NoteThread();
    ++gTpCalls;
    bool ok = count >= 1 && count <= 16;
    uint32_t base = 0;
    for (unsigned int i = 0; i < count; ++i)
    {
        const unsigned char* p = d + i * SE_LIVE_TRACE_DESC_LEN;
        const uint32_t id = Rd32(p), cpu = Rd32(p + 4), addr = Rd32(p + 8), flags = Rd32(p + 12);
        if (i == 0) base = id & ~15u;
        if ((id & ~15u) != base || (id & 15u) != i || cpu != (id & 1u) ||
            addr != 0x06000000u + id * 4u || flags != SE_LIVE_TP_ENABLED)
            ok = false;
    }
    if (!ok) ++gBadTpSets;
    gLastTpBase = base;
}

std::vector<uint32_t> gBpAdds;
void FakeClearBps()
{
    NoteThread();
    // The previous set is complete: consecutive ids from one base.
    bool ok = true;
    uint32_t base = gBpAdds.empty() ? 0 : ((gBpAdds[0] - 0x06000000u) / 2u) & ~15u;
    for (size_t i = 0; i < gBpAdds.size(); ++i)
    {
        const uint32_t id = (gBpAdds[i] - 0x06000000u) / 2u;
        if ((id & ~15u) != base || (id & 15u) != i) ok = false;
    }
    if (!gBpAdds.empty()) { ++gBpSets; if (!ok) ++gBadBpSets; gLastBpBase = base; }
    gBpAdds.clear();
}
void FakeAddExecBp(int, unsigned int address) { NoteThread(); gBpAdds.push_back(address); }
void FakeAddMemBp(int, unsigned int, unsigned int, unsigned int) { NoteThread(); }

std::vector<uint8_t> TpSet(uint32_t k, unsigned int count)
{
    std::vector<uint8_t> v;
    auto put = [&](uint32_t w) { for (int b = 0; b < 4; ++b) v.push_back(uint8_t(w >> (8 * b))); };
    for (unsigned int i = 0; i < count; ++i)
    {
        const uint32_t id = k * 16u + i;
        put(id); put(id & 1u); put(0x06000000u + id * 4u); put(SE_LIVE_TP_ENABLED);
    }
    return v;
}

std::vector<uint8_t> BpSet(uint32_t k, unsigned int count)
{
    std::vector<uint8_t> v;
    auto put = [&](uint32_t w) { for (int b = 0; b < 4; ++b) v.push_back(uint8_t(w >> (8 * b))); };
    for (unsigned int i = 0; i < count; ++i)
    {
        const uint32_t id = k * 16u + i;
        put(0x06000000u + id * 2u); put(0); put(SE_LIVE_BP_ENABLED);   // an execution breakpoint
    }
    return v;
}

void TestInstallsAreAppliedWholeOnTheEmulateThread(Client& cl)
{
    std::atomic<bool> stop{false};
    std::thread emu([&] {
        gEmuThread = std::this_thread::get_id();
        gEmuKnown = true;
        while (!stop.load())
        {
            SeExportGateFrame();   // the frame gate: where the emulate thread picks installs up
            std::this_thread::yield();
        }
    });
    while (!gEmuKnown.load()) Sleep(1);

    cl.Exchange(SE_LIVE_VERB_RESUME, 0);
    for (uint32_t k = 1; k <= 400; ++k)
    {
        const unsigned int n = 1 + (k % 16);
        cl.Exchange(SE_LIVE_VERB_TRACE, n, TpSet(k, n));
        cl.Exchange(SE_LIVE_VERB_BKPTS, n, BpSet(k, n));
    }
    // The last set sent is the one that ends up installed.
    for (int i = 0; i < 400 && (gLastTpBase.load() != 400u * 16u || gLastBpBase.load() == 0xFFFFFFFFu); ++i) Sleep(5);
    Sleep(50);
    stop = true;
    emu.join();

    Check(gOffThread.load() == 0, "every install hook ran on the emulate thread");
    Check(gTpCalls.load() > 0, "tracepoint sets were installed");
    Check(gBadTpSets.load() == 0, "no tracepoint set was ever half of two");
    Check(gBpSets.load() > 0, "breakpoint sets were installed");
    Check(gBadBpSets.load() == 0, "no breakpoint set was ever half of two");
    Check(gLastTpBase.load() == 400u * 16u, "and the last tracepoint set sent is the one installed");
}

// A resume must not outrun the installs sent before it. The temporary breakpoint of a Step Over is
// published, then RUN releases the CPU; if the release lands after the gate looked at the mailbox but
// before it looked at the pause, the CPU ran on with the breakpoint still unapplied. The hook places
// exactly that resume in the window.
Client* gRaceClient = nullptr;
bool    gRaceFired = false;
void ResumeInTheWindow()
{
    if (gRaceFired) return;
    gRaceFired = true;
    gRaceClient->Exchange(SE_LIVE_VERB_BKPTS, 1, BpSet(7000, 1));
    gRaceClient->Exchange(SE_LIVE_VERB_RESUME, 0);
}

void TestAResumeAppliesTheInstallsBeforeIt(Client& cl)
{
    gEmuThread = std::this_thread::get_id();
    gEmuKnown = true;
    SeExportGateFrame();                       // settle whatever the earlier tests left pending
    gBpAdds.clear();
    SeExportNotifyStop(0, 0x06000100u);        // halted at a breakpoint
    gRaceClient = &cl;
    SeExportTestGateHook = ResumeInTheWindow;
    // The hook publishes the temporary breakpoint and resumes while the gate is mid-decision.
    const int released = SeExportGateFrame();
    SeExportTestGateHook = nullptr;
    Check(gRaceFired, "the resume was placed inside the gate");
    Check(released == 1, "the CPU was released");
    bool installed = false;
    for (size_t i = 0; i < gBpAdds.size(); ++i)
        if (gBpAdds[i] == 0x06000000u + 7000u * 16u * 2u) installed = true;
    Check(installed, "the breakpoint sent before the resume is installed when the CPU is released");
    gEmuKnown = false;
}

// A client that leaves takes its breakpoints with it, but the drop happens on the emulate thread, at
// its next gate or frame -- so a hit can land first, and a halt with nobody to release it would freeze
// the game for good. The gate lets go of a hold nobody is there to end.
void TestAHaltWithNoClientIsReleased()
{
    SeExportNotifyStop(0, 0x06009000u);
    Check(SeExportGateFrame() == 1, "a halt with nobody attached does not hold the emulate thread");
    Check(SeExportGateFrame() == 1, "and stays released");
}
}  // namespace

int main()
{
    if (SeExportInit() != 0) { std::cerr << "SeExportInit failed\n"; return 1; }
    SeExportSetBreakpointHooks(FakeAddExecBp, FakeClearBps);
    SeExportSetMemBreakpointHook(FakeAddMemBp);
    SeExportSetTracepointHook(FakeSetTracepoints);
    {
    Client cl;
    if (!cl.Connect()) { std::cerr << "could not connect\n"; SeExportDeinit(); return 1; }
    TestStopSequence(cl);
    TestInstructionStepCountsRetirement(cl);
    TestAHaltOnTheOtherCpuEndsTheStep(cl);
    TestAnImmediateStopSurvivesTheRelease(cl);
    TestInstallsAreAppliedWholeOnTheEmulateThread(cl);
    TestAResumeAppliesTheInstallsBeforeIt(cl);
    }   // the client leaves
    Sleep(200);
    TestAHaltWithNoClientIsReleased();
    SeExportDeinit();
    if (gFailures) { std::cerr << gFailures << " check(s) failed\n"; return 1; }
    std::cout << "LiveHaltRaceTests passed\n";
    return 0;
}
