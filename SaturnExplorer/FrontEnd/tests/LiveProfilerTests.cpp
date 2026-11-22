// The profiler's wire path (protocol v26), end to end: the real exporter (se_export.c) on one
// side of a unix socket, the real live driver on the other. The emulator hands over one frame's
// counters at a time; the client must receive each frame exactly once, with every counter intact
// (they are 64-bit and the block states its own length), and the opt-in function attribution must
// reach the emulate thread when asked for -- and be switched off again when the client leaves,
// because it is the mode that makes the emulator several times slower.
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
#include "SePerfCounters.h"
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

std::atomic<int> gHookCalls{ 0 };
std::atomic<unsigned> gHookFlags{ 0 };
extern "C" void FakeSetProfiler(unsigned int flags) { gHookFlags = flags; ++gHookCalls; }

unsigned long long CounterValue(unsigned frame, unsigned index)
{
    // Past 32 bits, so a reader that keeps only the low word is caught.
    return (static_cast<unsigned long long>(frame) << 36) | (index * 1000u + frame);
}

unsigned gEmuFrame = 0;   // frames the fake emulator has completed

// One emulated frame: the gate, the end-of-frame hand-over of its counters, the snapshot.
void EmulatorFrame(bool withPerf = true)
{
    SeExportGateFrame();
    if (withPerf)
    {
        ++gEmuFrame;
        unsigned long long c[SE_PERF_COUNT];
        for (unsigned i = 0; i < SE_PERF_COUNT; ++i) c[i] = CounterValue(gEmuFrame, i);
        const unsigned keys[3] = { 0x06004000u, 0x06010000u | SE_PERF_FN_SLAVE, SE_PERF_FN_RAW_PC | 0x06020040u };
        const unsigned cyc[3] = { 9000u + gEmuFrame, 3000u, 12u };
        const bool fns = (SeExportProfilerFlags() & SE_LIVE_PROF_FUNCTIONS) != 0;
        SeExportPublishPerf(c, SE_PERF_COUNT, keys, cyc, fns ? 3u : 0u, fns ? 77u : 0u);
    }
    SeExportSnapshot(nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                     nullptr, nullptr, nullptr, nullptr, nullptr);
}

std::vector<se_live_perf_frame> gGot;
void Drain(const se_data_source& ds)
{
    se_live_perf_frame batch[8];
    uint32_t n;
    while ((n = se_live_drain_perf(&ds, batch, 8)) != 0)
        for (uint32_t i = 0; i < n; ++i) gGot.push_back(batch[i]);
}

template <typename Pred>
bool Until(Pred pred, const se_data_source& ds, bool withPerf = true)
{
    for (int i = 0; i < 600; ++i)
    {
        EmulatorFrame(withPerf);
        Sleep(5);
        Drain(ds);
        if (pred()) return true;
    }
    return false;
}

uint32_t Caps(const se_data_source& ds)
{
    uint32_t caps = 0;
    se_live_poke_info(&ds, nullptr, nullptr, nullptr, nullptr, &caps);
    return caps;
}
}  // namespace

int main()
{
    if (SeExportInit() != 0) { std::cerr << "SeExportInit failed\n"; return 1; }
    SeExportSetProfilerHook(FakeSetProfiler);

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

    // Counters arrive without anything being asked for.
    Check(Until([] { return gGot.size() >= 20; }, ds), "perf frames arrive");
    Check(se_live_server_version(&ds) >= SE_LIVE_MINVER_PROFILE, "the server speaks v26");
    Check((Caps(ds) & SE_LIVE_CAP_PROFILER) != 0, "the server advertises the counters");
    Check((Caps(ds) & SE_LIVE_CAP_PROF_FUNCTIONS) != 0, "and function attribution, with a hook registered");
    Check(gHookCalls.load() == 0, "the emulator is not asked to attribute until the client wants it");

    bool ordered = true, intact = true, noFns = true;
    for (size_t i = 0; i < gGot.size(); ++i)
    {
        const se_live_perf_frame& f = gGot[i];
        if (i && f.seq <= gGot[i - 1].seq) ordered = false;   // never the same frame twice, never backwards
        if (f.counter_count != SE_PERF_COUNT) intact = false;
        for (unsigned k = 0; k < SE_PERF_COUNT && intact; ++k)
            if (f.counters[k] != CounterValue(f.seq, k)) intact = false;
        if (f.fn_count != 0 || f.flags != 0 || f.fn_other_cycles != 0) noFns = false;
    }
    Check(ordered, "each frame is delivered once, in order");
    Check(intact, "every 64-bit counter survives the wire");
    Check(noFns, "no functions while attribution is off");

    // Snapshots that carry no NEW completed frame (a paused emulator, a debugger halt) repeat
    // the last frame's block; the client must not count it again.
    Drain(ds);
    for (int i = 0; i < 5; ++i) { EmulatorFrame(false); Sleep(5); }   // let in-flight replies land
    Drain(ds);
    const size_t before = gGot.size();
    for (int i = 0; i < 40; ++i) { EmulatorFrame(false); Sleep(5); Drain(ds); }
    Check(gGot.size() == before, "a repeated frame is not delivered twice");

    // Ask for function attribution: the hook runs on the emulate thread, the frames say so.
    se_live_set_profiler(&ds, SE_LIVE_PROF_FUNCTIONS);
    Check(Until([] { return gHookFlags.load() == SE_LIVE_PROF_FUNCTIONS; }, ds), "PRF reaches the emulator's hook");
    Check(gHookCalls.load() == 1, "once");
    Check(Until([] { return !gGot.empty() && gGot.back().fn_count == 3; }, ds), "attributed frames arrive");
    {
        const se_live_perf_frame& f = gGot.back();
        Check(f.flags == SE_LIVE_PROF_FUNCTIONS, "flagged as attributed");
        Check(f.fn_key[0] == 0x06004000u && f.fn_cycles[0] == 9000u + f.seq, "hottest function first");
        Check(f.fn_key[1] == (0x06010000u | SE_PERF_FN_SLAVE) && f.fn_cycles[1] == 3000u, "slave key intact");
        Check(f.fn_key[2] == (SE_PERF_FN_RAW_PC | 0x06020040u) && f.fn_cycles[2] == 12u, "raw-PC key intact");
        Check(f.fn_other_cycles == 77u, "the remainder is carried");
        Check(f.counters[SE_PERF_COUNT - 1] == CounterValue(f.seq, SE_PERF_COUNT - 1), "counters still intact beside it");
    }
    // Restating the same setting sends nothing more.
    se_live_set_profiler(&ds, SE_LIVE_PROF_FUNCTIONS);
    for (int i = 0; i < 20; ++i) { EmulatorFrame(); Sleep(5); }
    Check(gHookCalls.load() == 1, "an unchanged setting is not re-applied");

    se_live_set_profiler(&ds, 0);
    Check(Until([] { return gHookFlags.load() == 0u; }, ds), "switching it off reaches the hook");
    Check(gHookCalls.load() == 2, "as one more call");

    // The client goes away with attribution ON: the emulator must drop back to its fast loop.
    se_live_set_profiler(&ds, SE_LIVE_PROF_FUNCTIONS);
    Check(Until([] { return gHookFlags.load() == SE_LIVE_PROF_FUNCTIONS; }, ds), "on again");
    se_destroy(ctx);
    bool off = false;
    for (int i = 0; i < 600 && !off; ++i)
    {
        SeExportApplyInstalls();   // what the glue calls every frame, client or not
        off = gHookFlags.load() == 0u;
        if (!off) Sleep(5);
    }
    Check(off, "a disconnect switches attribution off");
    Check(SeExportProfilerFlags() == 0u, "and the server reports it off");

    SeExportDeinit();
    if (gFailures) { std::cerr << gFailures << " check(s) failed\n"; return 1; }
    std::cout << "LiveProfilerTests passed\n";
    return 0;
}
