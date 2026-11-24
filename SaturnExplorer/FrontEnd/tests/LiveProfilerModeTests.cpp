// F6: the attribution mode is latched per frame. The real exporter (se_export.c) and the real live driver
// over a socket, with a fake emulator that is structured like the real one: the top-of-frame gate, halts
// and snapshots INSIDE the frame, the frame's record published at its end under the mode the emulator
// ran in, and the end-of-frame boundary call the glue makes. A request that reaches the server during a
// frame (a mid-frame halt, a snapshot) must wait: the old frame is published in its old mode, the next
// frame runs wholly in the new one, in both directions.
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

// What the glue's SeMdfnSetProfiler does: the mode the NEXT frame runs in.
std::atomic<unsigned> gMode{ 0 };
std::atomic<int> gHookCalls{ 0 };
extern "C" void ModeHook(unsigned int flags) { gMode = flags; ++gHookCalls; }

// One emulated frame. 'midFrame' runs inside the frame, between the gate and the publication, where a
// debugger halt or a snapshot can happen; it is where the test lets a request arrive.
template <typename Fn>
void Frame(Fn midFrame)
{
    SeExportGateFrame();                       // top of frame: a pending request may be applied HERE
    const unsigned ranIn = gMode.load();       // the mode this frame runs in, latched now
    midFrame();
    // End of frame: the record is published under the mode it ran in (the glue's info.mode = sFrameMode).
    unsigned long long c[SE_PERF_COUNT] = {};
    SeExportPerfInfo info;
    std::memset(&info, 0, sizeof info);
    SeExportFrameIdentity(&info.timelineId, &info.frameNo);
    info.mode = ranIn;
    info.valid = SE_PERF_VALID_IDENTITY | SE_PERF_VALID_MODE;
    SeExportPublishPerf(c, SE_PERF_COUNT, nullptr, nullptr, 0, 0, &info);
    SeExportApplyInstalls();                   // the glue's end-of-frame boundary call, after the publication
    SeExportEndFrame();
}

void MidFrameActivity()
{
    // A debugger halt gate and a snapshot (MidSync) inside the frame, with time for the request to land.
    for (int i = 0; i < 6; ++i)
    {
        SeExportGateHalt();
        SeExportSnapshot(nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                         nullptr, nullptr, nullptr);
        Sleep(15);
    }
}

std::vector<se_live_perf_frame> gGot;
void Drain(const se_data_source& ds)
{
    se_live_perf_frame batch[8];
    uint32_t n;
    while ((n = se_live_drain_perf(&ds, batch, 8)) != 0)
        for (uint32_t i = 0; i < n; ++i) gGot.push_back(batch[i]);
}
bool Settle(const se_data_source& ds)
{
    const unsigned long long want = SeExportPerfNewestSeq();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(15000);
    while (std::chrono::steady_clock::now() < deadline)
    {
        se_live_perf_status st = {};
        if (se_live_perf_status_get(&ds, &st) && st.queued_seq >= want) { Drain(ds); return true; }
        Sleep(1);
    }
    return false;
}
void AfterReplies(const se_data_source& ds, uint64_t n)
{
    se_live_perf_status st = {};
    se_live_perf_status_get(&ds, &st);
    const uint64_t target = st.replies + n;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(15000);
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (se_live_perf_status_get(&ds, &st) && st.replies >= target) return;
        Sleep(1);
    }
}
}  // namespace

int main()
{
    if (SeExportInit() != 0) { std::cerr << "SeExportInit failed\n"; return 1; }
    SeExportSetProfilerHook(ModeHook);

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
    for (int i = 0; i < 2000 && se_live_server_version(&ds) == 0; ++i) Sleep(2);
    AfterReplies(ds, 3);

    for (int i = 0; i < 3; ++i) { Frame([] {}); Check(Settle(ds), "settle"); }
    Check(gHookCalls.load() == 0 && gGot.size() >= 3, "frames flow in the mode the emulator started in (off)");
    for (const auto& f : gGot) Check((f.valid & SE_PERF_VALID_MODE) != 0 && f.mode == 0, "a frame published off says off");

    // ---- OFF -> ON requested during a frame -------------------------------------------------------------
    gGot.clear();
    const unsigned long long seqBefore = SeExportPerfNewestSeq();
    Frame([&] {
        se_live_set_profiler(&ds, SE_LIVE_PROF_FUNCTIONS);   // the frame is already running when this arrives
        AfterReplies(ds, 4);                                  // sent and answered: the server holds it
        MidFrameActivity();
        Check(gHookCalls.load() == 0, "a request that arrives during a frame is not applied by a halt or a snapshot");
        Check(SeExportProfilerFlags() == 0u, "the server still reports the old mode inside the frame");
    });
    Check(Settle(ds), "settle");
    Check(gHookCalls.load() == 1 && gMode.load() == SE_LIVE_PROF_FUNCTIONS,
          "applied at the frame boundary, after that frame was published");
    {
        bool found = false;
        for (const auto& f : gGot)
            if (f.sample_seq > seqBefore)
            {
                found = true;
                Check((f.valid & SE_PERF_VALID_MODE) != 0 && f.mode == 0, "the frame that ran off is published off");
            }
        Check(found, "that frame arrived");
    }
    gGot.clear();
    Frame([&] { MidFrameActivity(); });
    Check(Settle(ds), "settle");
    Check(gHookCalls.load() == 1, "and not applied twice");
    Check(!gGot.empty() && (gGot.back().valid & SE_PERF_VALID_MODE) != 0 && gGot.back().mode == SE_LIVE_PROF_FUNCTIONS,
          "the next frame ran, and is published, wholly in the new mode");

    // ---- ON -> OFF requested during a frame -------------------------------------------------------------
    gGot.clear();
    Frame([&] {
        se_live_set_profiler(&ds, 0);
        AfterReplies(ds, 4);
        MidFrameActivity();
        Check(gHookCalls.load() == 1, "switching off during a frame waits too");
    });
    Check(Settle(ds), "settle");
    Check(gHookCalls.load() == 2 && gMode.load() == 0u, "off applied at the boundary");
    Check(!gGot.empty() && gGot.back().mode == SE_LIVE_PROF_FUNCTIONS, "the frame that ran attributed is published attributed");
    gGot.clear();
    Frame([] {});
    Check(Settle(ds), "settle");
    Check(!gGot.empty() && gGot.back().mode == 0u, "the next frame is off");

    // ---- a request that arrives BETWEEN frames takes effect at the next top-of-frame gate --------------
    gGot.clear();
    se_live_set_profiler(&ds, SE_LIVE_PROF_FUNCTIONS);
    AfterReplies(ds, 4);                       // no emulator call at all in between: the emulator is "paused between frames"
    Check(gHookCalls.load() == 2, "nothing applies it while no frame boundary is crossed");
    Frame([] {});                              // the gate applies it before the frame runs
    Check(Settle(ds), "settle");
    Check(gHookCalls.load() == 3 && !gGot.empty() && gGot.back().mode == SE_LIVE_PROF_FUNCTIONS,
          "the gate applied it, and the frame that followed ran in the new mode");

    se_destroy(ctx);
    SeExportDeinit();
    if (gFailures) { std::cerr << gFailures << " failure(s)\n"; return 1; }
    std::cout << "LiveProfilerModeTests: all passed\n";
    return 0;
}
