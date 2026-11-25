// se-bench-client: command line of the V3 benchmark client (see BenchClient.h and PROCEDURE.md).
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "BenchClient.h"

namespace
{
void Usage()
{
    std::puts(
        "se-bench-client [options]\n"
        "  --mode stream|bracket|load-slot   stream: stay connected (configs b, c); bracket: two short\n"
        "                                    connections around an unattended window (config a);\n"
        "                                    load-slot: load the emulator's own save slot and leave\n"
        "  --attribution on|off              request function attribution (stream mode; default off)\n"
        "  --warmup SEC                      discarded time before the window (default 10)\n"
        "  --duration SEC                    measured window (default 60); NOT used when --frames is given\n"
        "  --frames N                        end the window after N emulated frames instead of --duration\n"
        "                                    (stream and bracket alike; --duration is then ignored. Bracket finds the\n"
        "                                    end with few probes: it sleeps ~90% of the estimated remaining time and\n"
        "                                    looks again; the overshoot is in the summary, window_overshoot_frames)\n"
        "  --warmup-frames N                 end the warm-up after N frames past the first frame published after\n"
        "                                    attaching, instead of --warmup seconds (both given: frames win; bracket\n"
        "                                    then makes short warm-up probes, see PROCEDURE.md)\n"
        "  --timeout SEC                     give up (exit 1) if a frame-count run cannot finish (default 600)\n"
        "  --endpoint E                      unix path, tcp:host:port, or pipe; default = the driver's\n"
        "  --slot N                          load-slot: the emulator slot index\n"
        "  --pid PID                         emulator process id: records its CPU time (Linux only)\n"
        "  --drain-ms MS                     stream drain period (default 16, like the UI)\n"
        "  --connect-timeout SEC             (default 30)\n"
        "  --csv FILE  --summary FILE  --tag NAME\n"
        "Exit: 0 valid, 1 error, 2 run completed but invalid (lost samples etc.).");
}
}  // namespace

int main(int argc, char** argv)
{
    v3::BenchOptions o;
    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        auto next = [&](const char* what) -> const char* {
            if (i + 1 >= argc) { std::fprintf(stderr, "%s needs a value\n", what); std::exit(v3::kError); }
            return argv[++i];
        };
        if (a == "--mode")
        {
            const std::string m = next("--mode");
            if (m == "stream") o.mode = v3::Mode::Stream;
            else if (m == "bracket") o.mode = v3::Mode::Bracket;
            else if (m == "load-slot") o.mode = v3::Mode::LoadSlot;
            else { std::fprintf(stderr, "unknown mode %s\n", m.c_str()); return v3::kError; }
        }
        else if (a == "--attribution")
        {
            const std::string v = next("--attribution");
            if (v == "on") o.functions = true;
            else if (v == "off") o.functions = false;
            else { std::fprintf(stderr, "--attribution takes on or off\n"); return v3::kError; }
        }
        else if (a == "--warmup") o.warmupS = std::atof(next("--warmup"));
        else if (a == "--duration") o.durationS = std::atof(next("--duration"));
        else if (a == "--frames") o.frames = std::atoll(next("--frames"));
        else if (a == "--warmup-frames") o.warmupFrames = std::atoll(next("--warmup-frames"));
        else if (a == "--timeout") o.timeoutS = std::atof(next("--timeout"));
        else if (a == "--endpoint") o.endpoint = next("--endpoint");
        else if (a == "--slot") o.loadSlot = std::atoi(next("--slot"));
        else if (a == "--pid") o.pid = std::atol(next("--pid"));
        else if (a == "--drain-ms") o.drainMs = std::atoi(next("--drain-ms"));
        else if (a == "--connect-timeout") o.connectTimeoutS = std::atoi(next("--connect-timeout"));
        else if (a == "--csv") o.csvPath = next("--csv");
        else if (a == "--summary") o.summaryPath = next("--summary");
        else if (a == "--tag") o.tag = next("--tag");
        else if (a == "--help" || a == "-h") { Usage(); return v3::kOk; }
        else { std::fprintf(stderr, "unknown option %s\n", a.c_str()); Usage(); return v3::kError; }
    }
    if (o.drainMs < 1) o.drainMs = 1;
    if (o.durationS <= 0 || o.warmupS < 0) { std::fprintf(stderr, "bad --duration / --warmup\n"); return v3::kError; }
    return v3::RunBench(o, stdout);
}
