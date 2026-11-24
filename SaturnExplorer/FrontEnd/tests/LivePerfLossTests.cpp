// The capability bits of the very first reply (F14): the exporter registers its hooks and declares
// its counters BEFORE listeners are admitted, so a client that asks at the first permitted instant
// already sees the right bits -- both with a debugger, only the counters without, neither for a
// build with no counters. Listens on a socket and port of its own.
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
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
void Check(bool ok, const char* what, int line)
{
    if (ok) return;
    std::printf("CHECK failed at line %d: %s\n", line, what);
    ++gFailures;
}
#define CHECK(expression) Check(static_cast<bool>(expression), #expression, __LINE__)

void SleepMs(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

const char* const kSock = SE_LIVE_DEFAULT_SOCK_PATH;

extern "C" void FakeSetProfiler(unsigned int) {}

bool OpenRetry(se_data_source* ds)
{
    for (int i = 0; i < 1000; ++i)
    {
        if (se_live_open(kSock, ds) == SE_OK) return true;
        SleepMs(5);
    }
    return false;
}

uint32_t CapsOf(const se_data_source& ds)
{
    uint32_t caps = 0;
    se_live_poke_info(&ds, nullptr, nullptr, nullptr, nullptr, &caps);
    return caps;
}

// Starts the exporter the way a glue should: prepare, register what the build has, admit
// listeners. 'racer' asks for capabilities at the first instant a listener exists.
uint32_t FirstReplyCaps(bool counters, bool debugger)
{
    // Every sample taken from the first reply on, ANDed and ORed: they must all be the same.
    std::atomic<uint32_t> andMask{ 0xFFFFFFFFu }, orMask{ 0 };
    std::atomic<int> samples{ 0 };
    std::atomic<bool> stop{ false };
    std::thread racer([&] {
        se_data_source ds = {};
        while (!stop.load() && se_live_open(kSock, &ds) != SE_OK) std::this_thread::yield();
        if (stop.load()) return;
        for (int i = 0; i < 100000 && !stop.load() && samples.load() < 100; ++i)
        {
            if (se_live_server_version(&ds) != 0)
            {
                const uint32_t c = CapsOf(ds) & (SE_LIVE_CAP_PROFILER | SE_LIVE_CAP_PROF_FUNCTIONS);
                andMask.fetch_and(c);
                orMask.fetch_or(c);
                samples.fetch_add(1);
            }
            std::this_thread::yield();
        }
        ds.close(ds.user);
    });
    CHECK(SeExportPrepare() == 0);
    if (debugger) SeExportSetProfilerHook(FakeSetProfiler);
    if (counters) SeExportSetPerfCapable(1);
    CHECK(SeExportStart() == 0);
    for (int i = 0; i < 4000 && samples.load() < 100; ++i) SleepMs(2);
    stop = true;
    racer.join();
    SeExportDeinit();
    CHECK(samples.load() > 0);
    return andMask.load() == orMask.load() ? andMask.load() : 0xDEADu;
}

void TestFirstReplyCapabilities()
{
    const uint32_t both = SE_LIVE_CAP_PROFILER | SE_LIVE_CAP_PROF_FUNCTIONS;
    // A debugger build advertises both, before any sample exists.
    CHECK(FirstReplyCaps(true, true) == both);
    // A counter-capable build without a debugger advertises only the counters.
    CHECK(FirstReplyCaps(true, false) == SE_LIVE_CAP_PROFILER);
    // A build with neither says neither; and nothing of the previous run survived its deinit.
    CHECK(FirstReplyCaps(false, false) == 0u);
}

}  // namespace

int main()
{
    TestFirstReplyCapabilities();
    if (gFailures) { std::printf("%d check(s) failed\n", gFailures); return 1; }
    std::printf("LivePerfLossTests passed\n");
    return 0;
}
