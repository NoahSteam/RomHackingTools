// The restore-completion signal (protocol v19), end to end: the real exporter (se_export.c) on
// one side of a unix socket, the real live driver on the other. A state load (rewind LST,
// emulator slot ELS) is applied later, on the emulate thread, and its own reply says nothing, so
// the client needs to see it land in the snapshot stream -- and to see a refusal too -- instead
// of unlocking edits after a guessed delay.
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

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

std::atomic<int> gLoadResult{ 0 };    // what the fake emulator's load hooks answer
std::atomic<int> gLoads{ 0 };

extern "C" size_t FakeSave(unsigned char* buf, size_t cap)
{
    if (!buf) return 64;
    if (cap < 64) return 0;
    std::memset(buf, 0xA5, 64);
    return 64;
}
extern "C" int FakeLoad(const unsigned char*, size_t) { ++gLoads; return gLoadResult.load(); }
extern "C" int FakeSlotInfo(unsigned int, unsigned long long* mtime) { *mtime = 1; return 1; }
extern "C" int FakeSlotLoad(unsigned int) { ++gLoads; return gLoadResult.load(); }
std::atomic<int> gSaves{ 0 };
std::atomic<int> gSavedSlot{ -1 };
extern "C" int FakeSlotSave(unsigned int slot) { gSavedSlot = int(slot); ++gSaves; return 0; }

void Frame()
{
    SeExportSnapshot(nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                     nullptr, nullptr, nullptr, nullptr, nullptr);
}

void Sleep(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

// What the app's per-frame loop does on the emulator side: gate, then a frame.
void EmulatorTick() { SeExportGateFrame(); Frame(); }

struct Counters { uint32_t done = 0, failed = 0; bool ok = false; };

Counters Read(const se_data_source& ds)
{
    Counters c;
    c.ok = se_live_restore_state(&ds, &c.done, &c.failed) != 0;
    return c;
}

// Run emulator ticks until 'pred' holds or the budget runs out.
template <typename Pred>
bool Until(Pred pred, const se_data_source& ds)
{
    for (int i = 0; i < 400; ++i)
    {
        EmulatorTick();
        if (pred(Read(ds))) return true;
        Sleep(5);
    }
    return false;
}

// A second client, over the exporter's TCP port (the local socket is taken by the driver), that
// sends raw requests and discards every reply. It lets a test put two loads in front of the gate
// before the emulate thread has run, which the driver itself will not do.
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
    ~RawClient()
    {
        if (mFd >= 0) ::shutdown(mFd, SHUT_RDWR);
        if (mDrain.joinable()) mDrain.join();
        if (mFd >= 0) ::close(mFd);
    }
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
    void Request(const char* verb, uint32_t arg, const std::vector<uint8_t>& payload = {})
    {
        uint8_t h[8] = { uint8_t(verb[0]), uint8_t(verb[1]), uint8_t(verb[2]), uint8_t(verb[3]),
                         uint8_t(arg), uint8_t(arg >> 8), uint8_t(arg >> 16), uint8_t(arg >> 24) };
        Send(h, sizeof(h));
        if (!payload.empty()) Send(payload.data(), payload.size());
    }
private:
    int         mFd = -1;
    std::thread mDrain;
};

// LST payload: frame(4) + edits_len(4) + edits + state.
std::vector<uint8_t> LoadPayload(uint32_t frame)
{
    std::vector<uint8_t> p(8 + 64, 0x5A);
    for (int i = 0; i < 4; ++i) { p[i] = uint8_t(frame >> (8 * i)); p[4 + i] = 0; }
    return p;
}
}  // namespace

int main()
{
    if (SeExportInit() != 0) { std::cerr << "SeExportInit failed\n"; return 1; }
    SeExportSetSaveStateHook(FakeSave);
    SeExportSetLoadStateHook(FakeLoad);
    SeExportSetEmuSlotHooks(FakeSlotInfo, FakeSlotLoad);
    SeExportSetEmuSlotSaveHook(FakeSlotSave);

    se_data_source ds{};
    se_result r = SE_ERR_IO;
    for (int i = 0; i < 400 && r != SE_OK; ++i)
    {
        r = se_live_open(nullptr, &ds);
        if (r != SE_OK) Sleep(5);
    }
    if (r != SE_OK) { std::cerr << "could not open the live source\n"; SeExportDeinit(); return 1; }
    se_config cfg;
    cfg.abi_version = SE_ABI_VERSION;
    cfg.reserved = 0;
    se_context* ctx = se_create(&ds, &cfg);
    if (!ctx) { std::cerr << "se_create failed\n"; SeExportDeinit(); return 1; }

    // Wait for a first snapshot carrying the counters.
    Check(Until([](const Counters& c) { return c.ok; }, ds), "the server reports restore counters (v19)");
    const Counters base = Read(ds);
    Check(base.done == 0 && base.failed == 0, "nothing has been restored yet");

    const std::vector<uint8_t> state(64, 0x5A);

    // A rewind that the emulator applies: done advances, failed does not.
    gLoadResult = 0;
    Check(se_load_state(ctx, 7, state.data(), state.size(), nullptr, 0) == SE_OK, "load request accepted");
    Check(Until([&](const Counters& c) { return c.done == base.done + 1; }, ds),
          "an applied load shows up as done");
    Counters c = Read(ds);
    Check(c.failed == base.failed, "and not as failed");

    // A load the emulator cannot apply: failed advances, done does not -- never silence.
    gLoadResult = -1;
    Check(se_load_state(ctx, 9, state.data(), state.size(), nullptr, 0) == SE_OK, "second request accepted");
    Check(Until([&](const Counters& x) { return x.failed == base.failed + 1; }, ds),
          "a refused load shows up as failed");
    c = Read(ds);
    Check(c.done == base.done + 1, "and done did not move");

    // An emulator slot load follows the same contract.
    gLoadResult = 0;
    se_live_emu_load_slot(&ds, 2);
    Check(Until([&](const Counters& x) { return x.done == base.done + 2; }, ds),
          "an applied slot load shows up as done");
    gLoadResult = -1;
    se_live_emu_load_slot(&ds, 3);
    Check(Until([&](const Counters& x) { return x.failed == base.failed + 2; }, ds),
          "a refused slot load shows up as failed");

    // Two loads in front of the gate before it has run. The second replaces the first, which will
    // now never be applied -- and "every accepted load ends in exactly one counter" means it ends
    // as refused, not that it vanishes and leaves the client waiting for an outcome that cannot
    // come. (The emulate thread is not ticked until both have arrived.)
    {
        RawClient raw;
        Check(raw.Connect(), "raw client attached over TCP");
        const Counters b0 = Read(ds);
        gLoadResult = 0;
        raw.Request(SE_LIVE_VERB_LOADSTATE, 72, LoadPayload(11));
        raw.Request(SE_LIVE_VERB_LOADSTATE, 72, LoadPayload(12));
        bool refused = false;
        for (int i = 0; i < 400 && !refused; ++i) { refused = Read(ds).failed == b0.failed + 1; if (!refused) Sleep(5); }
        Check(refused, "the replaced rewind load is reported as refused");
        Check(Read(ds).done == b0.done, "and nothing has been applied yet");
        Check(Until([&](const Counters& x) { return x.done == b0.done + 1; }, ds),
              "the surviving load is applied once the gate runs");
        Check(Read(ds).failed == b0.failed + 1, "exactly one outcome each: one refused, one applied");

        // Same for the emulator's own slots.
        const Counters b1 = Read(ds);
        raw.Request(SE_LIVE_VERB_EMULOAD, 1);
        raw.Request(SE_LIVE_VERB_EMULOAD, 2);
        refused = false;
        for (int i = 0; i < 400 && !refused; ++i) { refused = Read(ds).failed == b1.failed + 1; if (!refused) Sleep(5); }
        Check(refused, "the replaced slot load is reported as refused");
        Check(Until([&](const Counters& x) { return x.done == b1.done + 1; }, ds),
              "the surviving slot load is applied");
        Check(Read(ds).failed == b1.failed + 1, "one refused, one applied");
    }

    // A rewind load and a slot load use separate mailboxes, so one gate call runs both. Each is an
    // accepted request and must be counted done: a flag instead of a count made the next frame
    // report one completion for two applied restores, and a client waiting for two never got
    // its second.
    {
        RawClient raw;
        Check(raw.Connect(), "raw client attached for the two-mailbox case");
        const Counters b = Read(ds);
        gLoadResult = 0;
        raw.Request(SE_LIVE_VERB_LOADSTATE, 72, LoadPayload(21));
        raw.Request(SE_LIVE_VERB_EMULOAD, 1);
        Sleep(400);   // both are in their mailboxes; the emulate thread has not run
        Check(Read(ds).done == b.done && Read(ds).failed == b.failed, "nothing applied before the gate runs");
        EmulatorTick();   // one gate call applies both, then publishes one frame
        Check(Until([&](const Counters& x) { return x.done == b.done + 2; }, ds),
              "two applied restores are two completions");
        Check(Read(ds).failed == b.failed, "and neither is counted as refused");
    }

    // Saving to the emulator's own slot (v25): advertised, written at a frame boundary, and not a
    // restore -- neither counter moves, since nothing was loaded and nobody waits on it.
    {
        uint32_t caps = 0;
        se_live_poke_info(&ds, nullptr, nullptr, nullptr, nullptr, &caps);
        Check((caps & SE_LIVE_CAP_EMU_SAVE) != 0, "a build with the save hook advertises EMU_SAVE");
        const Counters b = Read(ds);
        Check(se_live_emu_save_slot(&ds, 4) == 0, "an emulator slot save is accepted");
        Check(Until([](const Counters&) { return gSaves.load() == 1; }, ds), "the gate writes the slot");
        Check(gSavedSlot.load() == 4, "the slot asked for");
        Check(Read(ds).done == b.done && Read(ds).failed == b.failed, "a save is not counted as a restore");
        Check(se_live_emu_save_slot(&ds, SE_LIVE_EMU_SLOTS) != 0, "a slot past the end is refused");

        // Without the gate (a build without --with-pause), the end of the running frame writes it.
        RawClient raw;
        Check(raw.Connect(), "raw client attached for the end-of-frame save");
        raw.Request(SE_LIVE_VERB_EMUSAVE, 6);
        for (int i = 0; i < 400 && gSaves.load() < 2; ++i) { SeExportEndFrame(); Sleep(5); }
        Check(gSaves.load() == 2 && gSavedSlot.load() == 6, "the end of a frame writes the slot too");
        SeExportEndFrame();
        Check(gSaves.load() == 2, "and only once");
    }

    se_destroy(ctx);
    SeExportDeinit();
    if (gFailures) { std::cerr << gFailures << " check(s) failed\n"; return 1; }
    std::cout << "LiveRestoreSignalTests passed\n";
    return 0;
}
