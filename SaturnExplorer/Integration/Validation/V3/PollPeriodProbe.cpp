// Diagnostic, not a test: how often does the driver's poll loop get a reply, and how many frames does
// it lose, over the unix socket versus TCP, against the real exporter fed by a stand-in emulator
// that publishes one profiler frame every <period> ms. Prints one line per transport and period.
//
//   se-v3-poll-probe [seconds=3] [period_ms ...]      (default periods: 4 10)
//
// The stand-in's snapshots carry no VRAM (zero-filled), so absolute rates say nothing about a real
// emulator; the comparison between transports under identical load is the point. On Windows the
// named pipe is the only transport and this probe is not built.
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <vector>

extern "C" {
#include "se_export.h"
}
#include "LiveDriver.h"
#include "SeLiveProtocol.h"
#include "SePerfCounters.h"

namespace
{
std::atomic<bool> gStop{ false };
std::atomic<int> gPeriodMs{ 4 };
std::atomic<unsigned long long> gFrames{ 0 };

void Emulator()
{
    while (!gStop.load())
    {
        SeExportGateFrame();
        SeExportPerfInfo info;
        std::memset(&info, 0, sizeof info);
        SeExportFrameIdentity(&info.timelineId, &info.frameNo);
        info.valid = SE_PERF_VALID_IDENTITY;
        unsigned long long c[SE_PERF_COUNT] = {};
        SeExportPublishPerf(c, SE_PERF_COUNT, nullptr, nullptr, 0, 0, &info);
        SeExportSnapshot(nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr,
                         nullptr, nullptr, nullptr, nullptr, nullptr);
        SeExportEndFrame();
        ++gFrames;
        std::this_thread::sleep_for(std::chrono::milliseconds(gPeriodMs.load()));
    }
}

void Measure(const char* label, const char* endpoint, int periodMs, double seconds)
{
    gPeriodMs = periodMs;
    se_data_source ds{};
    for (int i = 0; i < 4000 && se_live_open(endpoint, &ds) != SE_OK; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    if (!ds.close) { std::printf("%-5s: cannot connect\n", label); return; }
    se_live_set_rewind_enabled(&ds, 0);
    while (se_live_server_version(&ds) == 0) std::this_thread::sleep_for(std::chrono::milliseconds(2));
    std::this_thread::sleep_for(std::chrono::milliseconds(500));   // settle
    se_live_perf_status s0{};
    se_live_perf_status_get(&ds, &s0);
    const unsigned long long f0 = gFrames.load();
    const auto t0 = std::chrono::steady_clock::now();
    unsigned long long got = 0;
    se_live_perf_frame b[64];
    while (std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count() < seconds)
    {
        uint32_t n;
        while ((n = se_live_drain_perf(&ds, b, 64)) != 0) got += n;
        std::this_thread::sleep_for(std::chrono::milliseconds(4));
    }
    const double dt = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    se_live_perf_status s1{};
    se_live_perf_status_get(&ds, &s1);
    const unsigned long long frames = gFrames.load() - f0;
    const double replies = static_cast<double>(s1.replies - s0.replies);
    std::printf("%-5s emulator %3d ms/frame (%5.1f fps): replies/s %7.1f  poll period %6.2f ms  "
                "frames published %llu, received %llu, lost by server ring %llu, evicted by client %llu\n",
                label, periodMs, frames / dt, replies / dt, replies > 0 ? 1000.0 * dt / replies : 0.0, frames, got,
                static_cast<unsigned long long>(s1.server_lost - s0.server_lost),
                static_cast<unsigned long long>(s1.driver_evicted - s0.driver_evicted));
    ds.close(ds.user);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
}
}  // namespace

int main(int argc, char** argv)
{
    const double seconds = argc > 1 ? std::atof(argv[1]) : 3.0;
    std::vector<int> periods;
    for (int i = 2; i < argc; ++i) periods.push_back(std::atoi(argv[i]));
    if (periods.empty()) { periods.push_back(4); periods.push_back(10); }
    if (SeExportPrepare() != 0) return 1;
    SeExportSetPerfCapable(1);
    if (SeExportStart() != 0) return 1;
    std::thread emu(Emulator);
    const std::string tcp = "tcp:127.0.0.1:" + std::to_string(SE_LIVE_DEFAULT_TCP_PORT);
    for (int p : periods)
    {
        Measure("unix", SE_LIVE_DEFAULT_SOCK_PATH, p, seconds);
        Measure("tcp", tcp.c_str(), p, seconds);
    }
    gStop = true;
    emu.join();
    SeExportDeinit();
    return 0;
}
