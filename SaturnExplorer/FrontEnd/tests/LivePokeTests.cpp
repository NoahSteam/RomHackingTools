// Emulator pokes, end to end: the real exporter (se_export.c) on one side of a unix socket, the
// real live driver on the other, the emulator's memory writers faked. What this pins:
//  - a poke is applied by the emulate thread at the frame gate, never by the server thread while
//    the cores run (the hooks record which thread called them);
//  - a CRAM / VDP1 frame-buffer poke -- which the emulator's ordinary bus writer silently drops --
//    is refused until the server says it has a VDP writer, then applied and visible on the next
//    capture;
//  - the server counts what it applied and what it could not, and the driver counts what it
//    accepted and never delivered;
//  - a state load discards the pokes still queued against the state it replaces, instead of
//    shipping them after it -- including pokes the server already holds, and ones it is still
//    receiving, when the load is accepted;
//  - the server's byte accounting for the poke queue survives a drain that overlaps a receive.
#if defined(_WIN32)
#error "LivePokeTests is POSIX-only: it listens on a unix socket. CMake builds it only where that exists."
#endif
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

extern "C" {
#include "se_export.h"
}
#include "LiveDriver.h"
#include "SeLiveProtocol.h"
#include "saturnexplorer/SaturnExplorer.h"

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

// ---- the fake emulator -------------------------------------------------------------------------
struct Event { char kind; unsigned addr; unsigned char val; std::thread::id tid; };
std::mutex gMtx;
std::vector<Event> gEvents;
uint16_t gCram[2048];          // host-order words, like Mednafen's CRAM[]
const std::thread::id kMain = std::this_thread::get_id();

void Log(char kind, unsigned addr, unsigned char val)
{
    std::lock_guard<std::mutex> lk(gMtx);
    gEvents.push_back({ kind, addr, val, std::this_thread::get_id() });
}
extern "C" void HookMain(unsigned a, unsigned char v) { Log('P', a, v); }
extern "C" void HookSound(unsigned o, unsigned char v) { Log('S', o, v); }
extern "C" void HookVdp(unsigned a, unsigned char v)
{
    Log('V', a, v);
    if (a >= 0x05F00000u && a < 0x05F80000u)
    {
        const unsigned off = a - 0x05F00000u, w = (off >> 1) & 0x7FF;
        gCram[w] = (off & 1) ? uint16_t((gCram[w] & 0xFF00) | v) : uint16_t((gCram[w] & 0x00FF) | (v << 8));
    }
}
extern "C" size_t FakeSave(unsigned char* buf, size_t cap)
{
    if (!buf) return 64;
    if (cap < 64) return 0;
    std::memset(buf, 0xA5, 64);
    return 64;
}
extern "C" int FakeLoad(const unsigned char*, size_t) { Log('L', 0, 0); return 0; }
extern "C" int FakeSlotInfo(unsigned int, unsigned long long* mtime) { *mtime = 1; return 1; }
extern "C" int FakeSlotLoad(unsigned int) { Log('L', 0, 0); return 0; }

// Lets a test stop the emulate thread INSIDE the gate's breakpoint-install step -- after the gate
// has looked at the poke mailbox, before it looks at the load mailbox -- which is exactly the window
// a poke and a load can both arrive in.
std::atomic<bool> gBlockInClear{ false };
std::atomic<bool> gInClear{ false };
std::atomic<bool> gRelease{ false };
extern "C" void HookAddBp(int, unsigned int) {}
extern "C" void HookClearBps()
{
    if (!gBlockInClear) return;
    gInClear = true;
    while (!gRelease) std::this_thread::sleep_for(std::chrono::milliseconds(1));
}

size_t Count(char kind)
{
    std::lock_guard<std::mutex> lk(gMtx);
    size_t n = 0;
    for (const Event& e : gEvents) n += e.kind == kind;
    return n;
}

// What the app's per-frame loop does on the emulator side: gate (which applies pokes), then a frame.
void EmulatorTick()
{
    SeExportGateFrame();
    SeExportSnapshot(nullptr, nullptr, gCram, nullptr, nullptr, nullptr, nullptr, nullptr,
                     nullptr, nullptr, nullptr, nullptr, nullptr);
}

struct Info { uint32_t applied = 0, dropped = 0, lost = 0, unconfirmed = 0, caps = 0; bool known = false; };
Info Poll(const se_data_source& ds)
{
    Info i;
    i.known = se_live_poke_info(&ds, &i.applied, &i.dropped, &i.lost, &i.unconfirmed, &i.caps) != 0;
    return i;
}

// A second client over the exporter's TCP port, to put requests in front of the gate in an order and
// at a moment the driver would not.
class RawClient
{
public:
    bool Connect()
    {
        for (int i = 0; i < 400; ++i)
        {
            mFd = ::socket(AF_INET, SOCK_STREAM, 0);
            sockaddr_in a = {};
            a.sin_family = AF_INET;
            a.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            a.sin_port = htons(SE_LIVE_DEFAULT_TCP_PORT);
            if (mFd >= 0 && ::connect(mFd, reinterpret_cast<sockaddr*>(&a), sizeof(a)) == 0)
            {
                mDrain = std::thread([this] {
                    char sink[65536];
                    while (::recv(mFd, sink, sizeof(sink), 0) > 0) { }
                });
                return true;
            }
            if (mFd >= 0) { ::close(mFd); mFd = -1; }
            Sleep(5);
        }
        return false;
    }
    void Close()
    {
        if (mFd >= 0) ::shutdown(mFd, SHUT_RDWR);
        if (mDrain.joinable()) mDrain.join();
        if (mFd >= 0) { ::close(mFd); mFd = -1; }
    }
    ~RawClient() { Close(); }
    void Send(const void* d, size_t n)
    {
        const uint8_t* p = static_cast<const uint8_t*>(d);
        while (n)
        {
            const ssize_t w = ::send(mFd, p, n, MSG_NOSIGNAL);
            if (w <= 0) return;
            p += w; n -= static_cast<size_t>(w);
        }
    }
    // 'arg' is what the header claims; 'payload' is what is actually sent (a test can send less).
    void Request(const char* verb, uint32_t arg, const std::vector<uint8_t>& payload = {})
    {
        uint8_t h[8] = { uint8_t(verb[0]), uint8_t(verb[1]), uint8_t(verb[2]), uint8_t(verb[3]),
                         uint8_t(arg), uint8_t(arg >> 8), uint8_t(arg >> 16), uint8_t(arg >> 24) };
        Send(h, sizeof(h));
        if (!payload.empty()) Send(payload.data(), payload.size());
    }
    void SendMore(const std::vector<uint8_t>& payload) { Send(payload.data(), payload.size()); }
private:
    int         mFd = -1;
    std::thread mDrain;
};

// WRM payload: destination(4 LE) + bytes.
std::vector<uint8_t> PokePayload(uint32_t dest, size_t bytes, uint8_t fill)
{
    std::vector<uint8_t> p(4 + bytes, fill);
    for (int i = 0; i < 4; ++i) p[i] = uint8_t(dest >> (8 * i));
    return p;
}
// LST payload: frame(4) + edits_len(4) + state.
std::vector<uint8_t> LoadPayload(uint32_t frame)
{
    std::vector<uint8_t> p(8 + 64, 0x5A);
    for (int i = 0; i < 4; ++i) { p[i] = uint8_t(frame >> (8 * i)); p[4 + i] = 0; }
    return p;
}

template <typename Pred>
bool Until(Pred pred, int ticks = 600)
{
    for (int i = 0; i < ticks; ++i)
    {
        EmulatorTick();
        if (pred()) return true;
        Sleep(5);
    }
    return false;
}
}  // namespace

int main()
{
    if (SeExportInit() != 0) { std::cerr << "SeExportInit failed\n"; return 1; }
    SeExportSetMemWriteHook(HookMain);
    SeExportSetSoundWriteHook(HookSound);
    SeExportSetSaveStateHook(FakeSave);
    SeExportSetLoadStateHook(FakeLoad);
    SeExportSetEmuSlotHooks(FakeSlotInfo, FakeSlotLoad);
    SeExportSetBreakpointHooks(HookAddBp, HookClearBps);

    se_data_source ds{};
    se_result r = SE_ERR_IO;
    for (int i = 0; i < 400 && r != SE_OK; ++i)
    {
        r = se_live_open(SE_LIVE_DEFAULT_SOCK_PATH, &ds);
        if (r != SE_OK) Sleep(5);
    }
    if (r != SE_OK) { std::cerr << "could not open the live source\n"; SeExportDeinit(); return 1; }
    se_config cfg;
    cfg.abi_version = SE_ABI_VERSION;
    cfg.reserved = 0;
    se_context* ctx = se_create(&ds, &cfg);
    if (!ctx) { std::cerr << "se_create failed\n"; SeExportDeinit(); return 1; }

    Check(Until([&] { return Poll(ds).known; }), "the server reports poke info (v23)");
    Check(se_begin_frame(ctx) == SE_OK, "a first capture");
    // A region-local offset stays in its region. The bus windows are adjacent, so VDP1 VRAM + 0x200000
    // is VDP2 VRAM: the driver must refuse that, and an edit straddling a region's end, not forward it.
    {
        const uint8_t b = 0x11;
        const uint8_t two[2] = { 0x11, 0x22 };
        Check(ds.write_vram(ds.user, SE_VRAM_KIND_VDP1_VRAM, 0x200000u, &b, 1) == 0, "a VDP1 offset past VDP1 VRAM is refused");
        Check(ds.write_vram(ds.user, SE_VRAM_KIND_VDP1_VRAM, 0x7FFFFu, two, 2) == 0, "an edit straddling its end is refused");
        Check(ds.write_vram(ds.user, SE_VRAM_KIND_VDP1_FB, 0x40000u, &b, 1) == 0, "so is one past the frame buffer");
        Check(ds.write_vram(ds.user, SE_VRAM_KIND_CRAM, 0x1000u, &b, 1) == 0, "and one past CRAM");
        Check(ds.write_vram(ds.user, SE_VRAM_KIND_VDP1_VRAM, 0x7FFFFu, &b, 1) == 1, "the last byte of a region is accepted");
        const uint32_t appliedBefore = Poll(ds).applied;
        Check(Until([&] { return Count('P') == 1; }), "and applied");
        Check(Until([&] { return Poll(ds).applied == appliedBefore + 1; }), "and counted");
        std::lock_guard<std::mutex> lk(gMtx);
        Check(gEvents.back().addr == 0x05C7FFFFu, "at its own bus address");
        gEvents.clear();
    }

    Info base = Poll(ds);
    Check(!(base.caps & SE_LIVE_CAP_VDP_POKE), "no VDP writer is wired yet: the capability bit is clear");

    // H1: a CRAM edit is refused outright while the server cannot apply it, not accepted and lost.
    const uint8_t v = 0x42;
    Check(se_write_vram(ctx, SE_VRAM_KIND_CRAM, 0x10, &v, 1) == 0, "a CRAM edit is refused without a VDP writer");
    uint8_t got = 0xEE;
    se_read_vram(ctx, SE_VRAM_KIND_CRAM, 0x10, &got, 1);
    Check(got == 0, "and the view does not show an edit the emulator never got");
    Check(se_write_vram(ctx, SE_VRAM_KIND_VDP1_FB, 0x20, &v, 1) == 0, "nor a frame-buffer edit");

    // The same edit once the server has a VDP writer.
    SeExportSetVdpWriteHook(HookVdp);
    Check(Until([&] { return (Poll(ds).caps & SE_LIVE_CAP_VDP_POKE) != 0; }), "the capability bit appears");
    Check(se_write_vram(ctx, SE_VRAM_KIND_CRAM, 0x10, &v, 1) == 1, "a CRAM edit is accepted with a VDP writer");

    // M2: nothing is written until the emulate thread runs its gate.
    Sleep(300);
    Check(Count('V') == 0, "the server thread did not write the poke while the emulator was not at a gate");
    EmulatorTick();
    Check(Count('V') == 1, "the gate applied it");
    {
        std::lock_guard<std::mutex> lk(gMtx);
        const Event* e = nullptr;
        for (const Event& x : gEvents) if (x.kind == 'V') e = &x;
        Check(e && e->addr == 0x05F00010u && e->val == 0x42, "to the CRAM bus address with the byte");
        Check(e && e->tid == kMain, "on the emulate thread, not the server's");
    }

    // The wired poke reads back on the next capture.
    Check(Until([&] { se_begin_frame(ctx); uint8_t b = 0; se_read_vram(ctx, SE_VRAM_KIND_CRAM, 0x10, &b, 1); return b == 0x42; }),
          "the poke is in the next capture");

    // A work-RAM poke takes the same road.
    const uint8_t w = 0x7E;
    Check(se_write_vram(ctx, SE_VRAM_KIND_WRAM_HIGH, 0x30, &w, 1) == 1, "a work-RAM edit is accepted");
    Sleep(300);
    Check(Count('P') == 0, "work RAM is not written from the server thread either");
    Check(Until([&] { return Count('P') == 1; }), "the gate applies it");
    {
        std::lock_guard<std::mutex> lk(gMtx);
        bool onMain = true;
        for (const Event& x : gEvents) if (x.kind == 'P' && x.tid != kMain) onMain = false;
        Check(onMain, "on the emulate thread");
    }

    // Counters: what the server applied, and a poke it had no writer for.
    Check(Until([&] { return Poll(ds).applied >= base.applied + 2; }), "the server counts applied pokes");
    const Info mid = Poll(ds);
    SeExportSetMemWriteHook(nullptr);
    Check(se_write_vram(ctx, SE_VRAM_KIND_WRAM_HIGH, 0x31, &w, 1) == 1, "the client cannot know the writer is gone");
    Check(Until([&] { return Poll(ds).dropped == mid.dropped + 1; }), "the server counts a poke it could not apply as dropped");
    Check(Poll(ds).applied == mid.applied, "and does not count it as applied");
    SeExportSetMemWriteHook(HookMain);

    // L2: pokes queued when a state load is requested belong to the state being replaced.
    {
        const size_t before = Count('P');
        // Straight to the driver's sink: through se_write_vram each edit also re-derives the
        // picture, slowly enough for the poll thread to ship every poke before the load is asked.
        constexpr unsigned kPokes = 5000;
        for (unsigned i = 0; i < kPokes; ++i)
        {
            const uint8_t b = uint8_t(i);
            if (ds.write_main_ram(ds.user, 0x06000100u + i, &b, 1) != 1) { Check(false, "queueing a poke"); break; }
        }
        const std::vector<uint8_t> state(64, 0x5A);
        Check(se_load_state(ctx, 7, state.data(), state.size(), nullptr, 0) == SE_OK, "the load request is accepted");
        // Until the emulator reports the load settled, a new poke would either be cleared with the
        // queue when the load ships or land on the restored state it was not made against.
        const uint8_t b = 0x33;
        Check(ds.write_main_ram(ds.user, 0x06000400u, &b, 1) == 0, "a poke is refused while the load is unapplied");
        Check(ds.write_sound_ram(ds.user, 0x400u, &b, 1) == 0, "so is a sound-RAM poke");
        Check(Until([&] { return Count('L') == 1; }), "the load is applied");
        // Give anything still wrongly queued time to ship and be applied after it.
        for (int i = 0; i < 100; ++i) { EmulatorTick(); Sleep(5); }
        size_t loadAt = 0, afterLoad = 0;
        {
            std::lock_guard<std::mutex> lk(gMtx);
            for (size_t i = 0; i < gEvents.size(); ++i) if (gEvents[i].kind == 'L') loadAt = i;
            for (size_t i = loadAt + 1; i < gEvents.size(); ++i) afterLoad += gEvents[i].kind == 'P';
        }
        Check(afterLoad == 0, "no queued poke is applied after the load that replaced its state");
        Check(Count('P') - before < kPokes, "the load discarded the queue instead of draining it first");
        Check(Until([&] { return ds.write_main_ram(ds.user, 0x06000400u, &b, 1) == 1; }),
              "pokes are accepted again once the emulator reports the load settled");
    }


    // A poke the server already holds when a restore is accepted must not run on the restored state.
    // The gate has looked at the poke mailbox (empty) and is parked inside its breakpoint-install
    // step; a WRM and then the restore arrive; the gate resumes, applies the restore, and its closing
    // install check would -- without the fix -- apply the old poke on top of it. Covers LST and ELS.
    for (int els = 0; els < 2; ++els)
    {
        RawClient raw;
        Check(raw.Connect(), "raw client attached over TCP");
        gBlockInClear = true; gInClear = false; gRelease = false;
        raw.Request(SE_LIVE_VERB_BKPTS, 0);          // publishes a (empty) breakpoint set: the gate will install it
        Sleep(200);
        const size_t loadsBefore = Count('L');
        std::thread racer([&] {
            while (!gInClear) Sleep(1);               // the gate is inside the install step
            raw.Request(SE_LIVE_VERB_WRITE, 1, PokePayload(0x06000200u, 1, 0x42));
            if (els) raw.Request(SE_LIVE_VERB_EMULOAD, 1);
            else     raw.Request(SE_LIVE_VERB_LOADSTATE, 72, LoadPayload(31));
            Sleep(400);                               // both have been received and accepted
            gRelease = true;
        });
        EmulatorTick();                               // blocks in the install step until released
        racer.join();
        gBlockInClear = false;
        Check(Until([&] { return Count('L') == loadsBefore + 1; }), els ? "the slot load is applied" : "the load is applied");
        for (int i = 0; i < 40; ++i) { EmulatorTick(); Sleep(5); }
        bool oldPokeAfterLoad = false;
        {
            std::lock_guard<std::mutex> lk(gMtx);
            size_t loadAt = 0;
            for (size_t i = 0; i < gEvents.size(); ++i) if (gEvents[i].kind == 'L') loadAt = i;
            for (size_t i = loadAt + 1; i < gEvents.size(); ++i)
                if (gEvents[i].kind == 'P' && gEvents[i].addr == 0x06000200u) oldPokeAfterLoad = true;
        }
        Check(!oldPokeAfterLoad, els ? "a poke received before a slot load does not run after it"
                                     : "a poke received before a state load does not run after it");
    }

    // The mailbox's byte accounting when a drain overlaps a receive. B reserves room and is part-way
    // through its payload when the emulate thread drains A. Zeroing the total on the drain made B's
    // later failure subtract bytes that were no longer counted, wrapping the total, after which every
    // small poke was refused.
    for (int disconnect = 0; disconnect < 2; ++disconnect)
    {
        // (The exporter serves one client per listener, so A comes in over the driver's socket and B
        // over TCP.)
        RawClient b;
        Check(b.Connect(), "raw client attached");
        const uint32_t addrA = 0x06000300u, addrB = 0x06000400u;
        const uint8_t pokeA = 0x11;
        Check(ds.write_main_ram(ds.user, addrA, &pokeA, 1) == 1, "A is accepted");   // A: complete, queued
        Sleep(300);
        b.Request(SE_LIVE_VERB_WRITE, 4096, PokePayload(addrB, 2048, 0x22));   // B: half of its payload
        Sleep(200);
        EmulatorTick();                                                       // drains A while B is mid-receive
        Check(Until([&] {
                  std::lock_guard<std::mutex> lk(gMtx);
                  for (const Event& e : gEvents) if (e.kind == 'P' && e.addr == addrA && e.val == 0x11) return true;
                  return false; }), "A is applied");
        const size_t dropsBefore = Poll(ds).dropped;
        if (disconnect)
        {
            b.Close();                                                        // B fails part-way
            Sleep(300);
        }
        else
        {
            b.SendMore(std::vector<uint8_t>(2048, 0x22));                      // B completes after the drain
            Sleep(300);
            Check(Until([&] {
                      std::lock_guard<std::mutex> lk(gMtx);
                      for (const Event& e : gEvents) if (e.kind == 'P' && e.addr == addrB + 4095) return true;
                      return false; }), "B is applied when it completes");
        }
        // A distinct address and value per pass, so the first pass's event cannot satisfy the second.
        const uint32_t addrC = 0x06000500u + (uint32_t)disconnect;
        const uint8_t c = (uint8_t)(0x33 + disconnect);
        Check(ds.write_main_ram(ds.user, addrC, &c, 1) == 1, "a later edit is accepted");
        Check(Until([&] {
                  std::lock_guard<std::mutex> lk(gMtx);
                  for (const Event& e : gEvents) if (e.kind == 'P' && e.addr == addrC && e.val == c) return true;
                  return false; }, 200), disconnect ? "and applied after a receive failed part-way"
                                                    : "and applied after an overlapping receive completed");
        Sleep(100);
        Check(Poll(ds).dropped == dropsBefore, "nothing was refused for lack of room");
    }

    se_destroy(ctx);
    SeExportDeinit();
    if (gFailures) { std::cerr << gFailures << " check(s) failed\n"; return 1; }
    std::cout << "LivePokeTests passed\n";
    return 0;
}
