// V3 benchmark client (Docs/ProfilerFixPlan.md, Phase 5, V3): a headless client that drives the
// real LiveDriver against a running patched emulator and records what the profiler stream
// delivered, one CSV row per frame. It never talks the wire protocol itself.
//
// Modes
//   Stream    stay connected for warmup + duration, draining the perf queue the way the UI does
//             (every drainMs), optionally with function attribution requested. Configs (b), (c).
//   Bracket   stay DISCONNECTED for warmup + duration and take two short connections, one at each
//             end, each reading the newest frame number the server hands a new connection and
//             hanging up. Config (a): no client attached during the measured window.
//   LoadSlot  connect, ask the emulator to load its own numbered save slot, wait until it reports
//             the load applied, disconnect. Puts every run on the same scene.
// All modes write the same CSV columns and the same key=value summary, so one analyzer reads them.
#pragma once

#include <cstdint>
#include <cstdio>
#include <string>

namespace v3
{
enum class Mode { Stream, Bracket, LoadSlot };

struct BenchOptions
{
    Mode        mode = Mode::Stream;
    std::string endpoint;           // "" = the driver's default (unix socket / named pipe)
    std::string tag;                // free label copied to the summary (e.g. "b")
    bool        functions = false;  // request SE_LIVE_PROF_FUNCTIONS (attribution on)
    double      warmupS = 10.0;     // discarded: the window starts after it
    double      durationS = 60.0;   // length of the measured window
    int         drainMs = 16;       // Stream: drain period (the UI drains once per ~16 ms frame)
    int         connectTimeoutS = 30;
    int         loadSlot = -1;      // LoadSlot: the emulator's slot index
    long        pid = 0;            // emulator process id for the CPU-time columns (Linux only; 0 = none)
    std::string csvPath;            // "" = no CSV
    std::string summaryPath;        // "" = stdout only
};

// Process exit codes of the executable.
enum ExitCode { kOk = 0, kError = 1, kInvalid = 2 };

// Runs the selected mode. Progress and the final summary go to 'out'. Returns an ExitCode:
// kInvalid means the run completed but lost samples (or another validity rule failed), which the
// analyzer must not count.
int RunBench(const BenchOptions& opt, std::FILE* out);

// Reads utime+stime of a process in clock ticks; -1 when unavailable (non-Linux, no such pid).
long long ReadProcessCpuTicks(long pid, long* ticksPerSecond);
}  // namespace v3
