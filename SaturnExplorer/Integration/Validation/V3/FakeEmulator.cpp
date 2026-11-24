// Stand-in for the patched Mednafen, for testing run_v3.sh without a BIOS: the real exporter
// (se_export.c) fed by a loop that publishes one profiler frame every few milliseconds, the way
// the glue does. It answers `-help` with the setting names the script pins, ignores every other
// argument, and exits on SIGTERM. Its speed means nothing.
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

extern "C" {
#include "se_export.h"
}
#include "SeLiveProtocol.h"
#include "SePerfCounters.h"

namespace
{
std::atomic<bool> gStop{ false };
void OnSignal(int) { gStop = true; }
extern "C" void SetProfiler(unsigned int) {}
extern "C" int SlotInfo(unsigned int, unsigned long long* mtime) { *mtime = 1; return 1; }
extern "C" int SlotLoad(unsigned int) { SeExportNotifyStateLoaded(); return 0; }
}  // namespace

int main(int argc, char** argv)
{
    for (int i = 1; i < argc; ++i)
        if (!std::strcmp(argv[i], "-help"))
        {
            std::puts("-sound\n-nothrottle\n-video.fs\n-video.glvsync\n-video.frameskip\n-fps.autoenable\n"
                      "-cheats\n-autosave\n-ss.cpucache_emumode");
            return 0;
        }
    bool functions = true;   // --no-functions: a build without attribution (no SE_LIVE_CAP_PROF_FUNCTIONS)
    for (int i = 1; i < argc; ++i)
        if (!std::strcmp(argv[i], "--no-functions")) functions = false;
    std::signal(SIGTERM, OnSignal);
    std::signal(SIGINT, OnSignal);
    if (SeExportPrepare() != 0) return 1;
    if (functions) SeExportSetProfilerHook(SetProfiler);
    SeExportSetPerfCapable(1);
    SeExportSetEmuSlotHooks(SlotInfo, SlotLoad);
    if (SeExportStart() != 0) return 1;
    while (!gStop.load())
    {
        SeExportGateFrame();
        SeExportPerfInfo info;
        std::memset(&info, 0, sizeof info);
        SeExportFrameIdentity(&info.timelineId, &info.frameNo);
        info.valid = SE_PERF_VALID_IDENTITY | SE_PERF_VALID_MODE;
        info.mode = SeExportProfilerFlags();
        unsigned long long c[SE_PERF_COUNT] = {};
        SeExportPublishPerf(c, SE_PERF_COUNT, nullptr, nullptr, 0, 0, &info);
        SeExportSnapshot(nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                         nullptr, nullptr, nullptr, nullptr, nullptr);
        SeExportEndFrame();
        std::this_thread::sleep_for(std::chrono::milliseconds(4));
    }
    SeExportDeinit();
    return 0;
}
