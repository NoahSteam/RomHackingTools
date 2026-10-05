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
}  // namespace

int main()
{
    if (SeExportInit() != 0) { std::cerr << "SeExportInit failed\n"; return 1; }
    SeExportSetSaveStateHook(FakeSave);
    SeExportSetLoadStateHook(FakeLoad);
    SeExportSetEmuSlotHooks(FakeSlotInfo, FakeSlotLoad);

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

    se_destroy(ctx);
    SeExportDeinit();
    if (gFailures) { std::cerr << gFailures << " check(s) failed\n"; return 1; }
    std::cout << "LiveRestoreSignalTests passed\n";
    return 0;
}
