// Stand-in for the patched Mednafen, for testing run_v3.sh without a BIOS: the real exporter
// (se_export.c) fed by a loop that publishes one profiler frame every few milliseconds, the way
// the glue does. It answers `-dump_settings_def FILE` in Mednafen's real dump format with the settings
// run_v3.sh pins (types and enum lists as in the pinned Mednafen source; FAKE_DUMP_OMIT=name leaves
// one out), ignores every other argument, and exits on SIGTERM. Its speed means nothing.
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

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

// One record as SettingsManager::DumpDef writes it: name, flags, description, description_extra, type,
// default, minimum, maximum, enum count, {value, description, description_extra}*, alias count.
void Record(std::FILE* f, const char* name, const char* flags, const char* type, const char* def,
            const std::vector<std::string>& enums = {})
{
    const char* omit = std::getenv("FAKE_DUMP_OMIT");
    if (omit && !std::strcmp(omit, name)) return;
    std::fprintf(f, "%s\n%s\ndesc\n\n%s\n%s\n\n\n%zu\n", name, flags, type, def, enums.size());
    for (const std::string& e : enums) std::fprintf(f, "%s\n\n\n", e.c_str());
    std::fprintf(f, "0\n");
}

int DumpSettingsDef(const char* path)
{
    std::FILE* f = std::fopen(path, "w");
    if (!f) return 1;
    Record(f, "autosave", "", "MDFNST_BOOL", "0");
    Record(f, "cheats", "", "MDFNST_BOOL", "1");
    Record(f, "fps.autoenable", "", "MDFNST_BOOL", "0");
    Record(f, "nothrottle", "", "MDFNST_BOOL", "0");
    Record(f, "sound", "", "MDFNST_BOOL", "1");
    Record(f, "ss.dbg_cem", "MDFNSF_SUPPRESS_DOC MDFNSF_NONPERSISTENT", "MDFNST_ENUM", "auto",
           { "data_cb", "data", "full", "auto" });
    Record(f, "video.blit_timesync", "", "MDFNST_BOOL", "1");
    Record(f, "video.driver", "", "MDFNST_ENUM", "default", { "default", "opengl", "softfb", "0" });
    Record(f, "video.frameskip", "", "MDFNST_BOOL", "1");
    Record(f, "video.fs", "", "MDFNST_BOOL", "0");
    Record(f, "video.glvsync", "", "MDFNST_BOOL", "1");
    std::fclose(f);
    return 0;
}
}  // namespace

int main(int argc, char** argv)
{
    for (int i = 1; i + 1 < argc; ++i)
        if (!std::strcmp(argv[i], "-dump_settings_def"))
            return DumpSettingsDef(argv[i + 1]);
    bool functions = true;   // --no-functions: a build without attribution (no SE_LIVE_CAP_PROF_FUNCTIONS)
    for (int i = 1; i < argc; ++i)
        if (!std::strcmp(argv[i], "--no-functions")) functions = false;
    // FAKE_FRAME_MS: milliseconds per frame (default 4). The tests must pass at any value; slow it to mimic a loaded host.
    int frameMs = 4;
    if (const char* e = std::getenv("FAKE_FRAME_MS")) frameMs = std::atoi(e) > 0 ? std::atoi(e) : 4;
    // FAKE_ONLY_ATTACHED=1: publish perf records only while a client is attached (frames keep being emulated, so
    // frame_no advances while nobody listens), as the Mednafen glue does; a new connection is then handed the
    // previous one's last frame. Default: publish every frame.
    const bool onlyAttached = std::getenv("FAKE_ONLY_ATTACHED") != nullptr && std::atoi(std::getenv("FAKE_ONLY_ATTACHED")) != 0;
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
        if (!onlyAttached || SeExportHasClient())
            SeExportPublishPerf(c, SE_PERF_COUNT, nullptr, nullptr, 0, 0, &info);
        SeExportSnapshot(nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                         nullptr, nullptr, nullptr, nullptr, nullptr);
        SeExportEndFrame();
        std::this_thread::sleep_for(std::chrono::milliseconds(frameMs));
    }
    SeExportDeinit();
    return 0;
}
