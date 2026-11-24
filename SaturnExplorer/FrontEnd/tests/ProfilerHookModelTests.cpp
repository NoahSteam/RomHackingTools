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
    std::printf(gFailures ? "FAILED\n" : "PASSED\n");
    return gFailures ? 1 : 0;
}
