// What the model makes of the F4 / F5 / F8 counters: command work is not clamped, a DMA's discarded
// draw budget is its own figure, the 68000 executed share follows total - idle, and the two bus
// buckets that are no device stay out of every bus total.
#include <cmath>
#include <cstdio>
#include <string>

#include "Debug/ProfilerModel.h"

using namespace sfe;

namespace
{
int gFailures;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++gFailures; } } while (0)
bool Near(double a, double b) { return std::fabs(a - b) < 1e-9; }
}

int main()
{
    {   // F4: instant draw does more work than the frame held
        uint64_t c[SE_PERF_COUNT] = {};
        c[SE_PERF_FRAME_CYCLES] = 1000;
        c[SE_PERF_VDP1_WORK_CYCLES] = 2500;
        c[SE_PERF_VDP1_DMA_DISCARDED] = 100;
        const PerfShares s = ComputePerfShares(c);
        CHECK(Near(s.vdp1Work, 2.5));
        CHECK(s.vdp1WorkOverFrame);
        CHECK(Near(s.vdp1DmaDiscarded, 0.1));
        c[SE_PERF_VDP1_WORK_CYCLES] = 400;
        const PerfShares t = ComputePerfShares(c);
        CHECK(Near(t.vdp1Work, 0.4) && !t.vdp1WorkOverFrame);
        // the retired index never feeds the v27 figure
        c[SE_PERF_RETIRED_6] = 999;
        CHECK(Near(ComputePerfShares(c).vdp1Work, 0.4));
    }
    {   // F5: the executed share is executed / total, both on the 68000's own clock
        uint64_t c[SE_PERF_COUNT] = {};
        c[SE_PERF_FRAME_CYCLES] = 1000;
        c[SE_PERF_M68K_TOTAL_CYCLES] = 400;
        c[SE_PERF_M68K_IDLE_CYCLES] = 300;
        c[SE_PERF_M68K_CYCLES] = 100;       // = total - idle, as the emulator now produces it
        CHECK(Near(ComputePerfShares(c).m68k, 0.25));
    }
    {   // F8: dummy and unmapped requests are counted apart and are in no bus total
        uint64_t c[SE_PERF_COUNT] = {};
        c[SE_PERF_BUS_ACCESS + SE_PERF_DEV_ABUS_CD * 2] = 7;
        c[SE_PERF_BUS_ACCESS + SE_PERF_DEV_WRAM_H * 2 + 1] = 5;
        c[SE_PERF_BUS_ACCESS_X + SE_PERF_XDEV_ABUS_DUMMY * 2] = 11;
        c[SE_PERF_BUS_ACCESS_X + SE_PERF_XDEV_ABUS_DUMMY * 2 + 1] = 13;
        c[SE_PERF_BUS_ACCESS_X + SE_PERF_XDEV_UNMAPPED * 2] = 17;
        c[SE_PERF_BUS_ACCESS_X + SE_PERF_XDEV_UNMAPPED * 2 + 1] = 19;
        const BusTotals t = ComputeBusTotals(c);
        CHECK(t.reads[static_cast<int>(PerfBus::A)] == 7);
        CHECK(t.writes[static_cast<int>(PerfBus::Cpu)] == 5);
        CHECK(t.reads[static_cast<int>(PerfBus::Cpu)] == 0 && t.reads[static_cast<int>(PerfBus::B)] == 0);
        CHECK(t.writes[static_cast<int>(PerfBus::A)] == 0 && t.writes[static_cast<int>(PerfBus::B)] == 0);
        CHECK(t.dummyReads == 11 && t.dummyWrites == 13 && t.unmappedReads == 17 && t.unmappedWrites == 19);
        CHECK(std::string(PerfXDeviceName(SE_PERF_XDEV_ABUS_DUMMY)) != std::string(PerfDeviceName(SE_PERF_DEV_ABUS_CD)));
        CHECK(std::string(PerfXDeviceName(SE_PERF_XDEV_UNMAPPED)) == "Unmapped");
        CHECK(std::string(PerfBusUnitLabel()) == "bus requests");
        CHECK(std::string(PerfCacheModeName(SE_PERF_CACHE_ON)) != std::string(PerfCacheModeName(SE_PERF_CACHE_OFF)));
        CHECK(std::string(PerfCacheModeName(SE_PERF_CACHE_UNKNOWN)) == "cache mode unknown");
    }
    std::printf(gFailures ? "FAILED\n" : "PASSED\n");
    return gFailures ? 1 : 0;
}
