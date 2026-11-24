// The profiler's arithmetic, with no emulator and no UI: a profiler that divides by the wrong
// total still draws a plausible bar, so the shares, the history window, the function totals and
// the memory-pool classification are pinned here against hand-worked counters.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <vector>

#include "Debug/ProfilerModel.h"

using namespace sfe;

namespace
{
int gFailures;
#define CHECK(cond) do { if (!(cond)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++gFailures; } } while (0)
bool Near(double a, double b) { return std::fabs(a - b) < 1e-9; }

PerfFrame MakeFrame(uint32_t seq, uint64_t cycles)
{
    PerfFrame f;
    f.seq = seq;
    f.frame = seq + 100;
    f.c[SE_PERF_FRAME_CYCLES] = cycles;
    return f;
}

void TestShares()
{
    uint64_t c[SE_PERF_COUNT] = {};
    c[SE_PERF_FRAME_CYCLES] = 1000;
    c[SE_PERF_SH2_SLEEP] = 300;          // master: 30% asleep
    c[SE_PERF_SH2_STALL] = 100;          // 10% stalled -> 60% busy
    c[SE_PERF_SSH2_ON_CYCLES] = 500;     // slave on for half the frame
    c[SE_PERF_SH2_SLEEP + 1] = 200;
    c[SE_PERF_SH2_STALL + 1] = 50;       // -> 25% busy
    c[SE_PERF_V26_VDP1_ELAPSED] = 250;
    c[SE_PERF_DSP_CYCLES] = 10;
    c[SE_PERF_M68K_CYCLES] = 40;
    c[SE_PERF_M68K_TOTAL_CYCLES] = 400;  // the 68000's own clock, not the SH-2's
    c[SE_PERF_VDP1_COMMANDS] = 120;
    c[SE_PERF_VDP1_DRAW_STARTS] = 1;
    const PerfShares s = ComputePerfShares(c);
    CHECK(Near(s.frameCycles, 1000));
    CHECK(Near(s.cpu[0].busy, 0.6) && Near(s.cpu[0].sleep, 0.3) && Near(s.cpu[0].stall, 0.1) && Near(s.cpu[0].off, 0.0));
    CHECK(Near(s.cpu[1].busy, 0.25) && Near(s.cpu[1].sleep, 0.2) && Near(s.cpu[1].stall, 0.05) && Near(s.cpu[1].off, 0.5));
    CHECK(Near(s.vdp1Draw, 0.25) && Near(s.dsp, 0.01) && Near(s.m68k, 0.1));
    CHECK(Near(s.vdp1Commands, 120) && Near(s.vdp1DrawsPerFrame, 1));
    for (int cpu = 0; cpu < 2; ++cpu)
        CHECK(Near(s.cpu[cpu].busy + s.cpu[cpu].sleep + s.cpu[cpu].stall + s.cpu[cpu].off, 1.0));

    // Summed over two frames the per-frame figures halve; the shares do not move.
    uint64_t two[SE_PERF_COUNT];
    for (int i = 0; i < SE_PERF_COUNT; ++i) two[i] = c[i] * 2;
    const PerfShares s2 = ComputePerfShares(two, 2.0);
    CHECK(Near(s2.frameCycles, 1000) && Near(s2.cpu[0].busy, 0.6) && Near(s2.vdp1Commands, 120));

    // Idle counted past the frame (the counters are read at slightly different instants) is
    // clamped, never a negative busy share.
    uint64_t over[SE_PERF_COUNT] = {};
    over[SE_PERF_FRAME_CYCLES] = 1000;
    over[SE_PERF_SH2_SLEEP] = 1003;
    const PerfShares so = ComputePerfShares(over);
    CHECK(Near(so.cpu[0].busy, 0.0) && Near(so.cpu[0].sleep, 1.0));
    // A slave that was never on is entirely "off".
    CHECK(Near(so.cpu[1].off, 1.0) && Near(so.cpu[1].busy, 0.0));

    // No data: all zero rather than a division by zero.
    uint64_t none[SE_PERF_COUNT] = {};
    const PerfShares sn = ComputePerfShares(none);
    CHECK(sn.frameCycles == 0 && sn.cpu[0].busy == 0 && sn.vdp1Draw == 0);
}

void TestBuses()
{
    uint64_t c[SE_PERF_COUNT] = {};
    c[SE_PERF_BUS_ACCESS + SE_PERF_DEV_WRAM_H * 2] = 1000;       // CPU bus read
    c[SE_PERF_BUS_ACCESS + SE_PERF_DEV_WRAM_H * 2 + 1] = 400;    // CPU bus write
    c[SE_PERF_BUS_ACCESS + SE_PERF_DEV_SCU * 2 + 1] = 7;         // SCU registers sit on the CPU bus
    c[SE_PERF_BUS_ACCESS + SE_PERF_DEV_VDP1 * 2 + 1] = 50;       // B-bus
    c[SE_PERF_BUS_ACCESS + SE_PERF_DEV_VDP2 * 2] = 5;            // B-bus
    c[SE_PERF_BUS_ACCESS + SE_PERF_DEV_ABUS_CD * 2] = 30;        // A-bus
    c[SE_PERF_DMA_ROUTE_BYTES + SE_PERF_BUS_CPU * 3 + SE_PERF_BUS_B] = 4096;   // work RAM -> VDP
    c[SE_PERF_DMA_ROUTE_BYTES + SE_PERF_BUS_A * 3 + SE_PERF_BUS_CPU] = 2048;   // CD -> work RAM
    const BusTotals t = ComputeBusTotals(c);
    const int cpu = static_cast<int>(PerfBus::Cpu), a = static_cast<int>(PerfBus::A), b = static_cast<int>(PerfBus::B);
    CHECK(t.reads[cpu] == 1000 && t.writes[cpu] == 407);
    CHECK(t.reads[b] == 5 && t.writes[b] == 50);
    CHECK(t.reads[a] == 30 && t.writes[a] == 0);
    CHECK(t.dmaOut[cpu] == 4096 && t.dmaIn[b] == 4096);
    CHECK(t.dmaOut[a] == 2048 && t.dmaIn[cpu] == 2048);
    CHECK(t.dmaIn[a] == 0 && t.dmaOut[b] == 0);
    // Every device has a name and belongs to a bus.
    for (int d = 0; d < SE_PERF_DEV_COUNT; ++d) CHECK(std::strcmp(PerfDeviceName(d), "?") != 0);
    CHECK(PerfDeviceBus(SE_PERF_DEV_SCSP) == PerfBus::B && PerfDeviceBus(SE_PERF_DEV_ABUS_CS0) == PerfBus::A);
    // v26 layout kept in place, v27 counters appended after it.
    CHECK(SE_PERF_V26_COUNT == SE_PERF_BUS_ACCESS + SE_PERF_DEV_COUNT * 2 && SE_PERF_V26_COUNT == 54);
    CHECK(SE_PERF_RESERVED_54 == SE_PERF_V26_COUNT);   // reserved, never produced: index 5 carries the integrated figure
    CHECK(SE_PERF_VDP1_WORK_CYCLES == 55 && SE_PERF_VDP1_DMA_DISCARDED == 56 && SE_PERF_M68K_IDLE_CYCLES == 57 && SE_PERF_BUS_ACCESS_X == 58);
    CHECK(SE_PERF_COUNT == SE_PERF_BUS_ACCESS_X + SE_PERF_XDEV_COUNT * 2 && SE_PERF_COUNT == 62);
    CHECK(SE_PERF_SSH2_ON_CYCLES == 5 && SE_PERF_RETIRED_6 == 6 && SE_PERF_VDP1_COMMANDS == 7);
}

void TestHistory()
{
    ProfilerModel m(4);
    CHECK(m.Empty());
    for (uint32_t i = 1; i <= 3; ++i) m.Push(MakeFrame(i, i * 100));
    CHECK(m.Count() == 3 && m.At(0).seq == 1 && m.Latest().seq == 3);
    m.Push(MakeFrame(3, 999));                       // the same frame delivered twice
    CHECK(m.Count() == 3 && m.Latest().c[SE_PERF_FRAME_CYCLES] == 300);
    m.Push(MakeFrame(4, 400));
    m.Push(MakeFrame(5, 500));                       // wraps: the oldest goes
    CHECK(m.Count() == 4 && m.At(0).seq == 2 && m.Latest().seq == 5);

    uint64_t sum[SE_PERF_COUNT];
    CHECK(m.Sum(2, sum) == 2 && sum[SE_PERF_FRAME_CYCLES] == 900);     // the newest two
    CHECK(m.Sum(100, sum) == 4 && sum[SE_PERF_FRAME_CYCLES] == 1400);  // capped at what there is
    CHECK(m.Sum(1, sum) == 1 && sum[SE_PERF_FRAME_CYCLES] == 500);

    m.Push(MakeFrame(1, 50));                        // the count went backwards: a new emulator run
    CHECK(m.Count() == 1 && m.Latest().c[SE_PERF_FRAME_CYCLES] == 50);
    m.Clear();
    CHECK(m.Empty() && m.Sum(10, sum) == 0 && sum[SE_PERF_FRAME_CYCLES] == 0);
}

void TestFunctions()
{
    ProfilerModel m(8);
    const uint32_t fnA = 0x06004000u, fnB = 0x06010000u;
    const uint32_t slaveB = fnB | SE_PERF_FN_SLAVE;
    const uint32_t raw = SE_PERF_FN_RAW_PC | 0x06020040u;

    PerfFrame f1 = MakeFrame(1, 1000);
    f1.functions = true;
    f1.fns = { { fnA, 300 }, { fnB, 100 } };
    f1.fnOther = 10;
    PerfFrame f2 = MakeFrame(2, 1000);               // attribution off: must not dilute anything
    PerfFrame f3 = MakeFrame(3, 1000);
    f3.functions = true;
    f3.fns = { { fnB, 500 }, { slaveB, 200 }, { raw, 50 } };
    m.Push(f1); m.Push(f2); m.Push(f3);

    uint64_t total = 0;
    size_t used = 0;
    std::vector<PerfFunction> hot = m.HotFunctions(10, &total, &used);
    CHECK(used == 2);
    CHECK(total == 300 + 100 + 10 + 500 + 200 + 50);
    CHECK(hot.size() == 4);
    CHECK(hot[0].key == fnB && hot[0].cycles == 600);      // summed across frames
    CHECK(hot[1].key == fnA && hot[1].cycles == 300);
    CHECK(hot[2].key == slaveB && hot[2].Slave() && !hot[2].RawPc() && hot[2].Address() == fnB);
    CHECK(hot[3].RawPc() && !hot[3].Slave() && hot[3].Address() == 0x06020040u);

    hot = m.HotFunctions(1, &total, &used);                // the newest frame alone
    CHECK(used == 1 && total == 750 && hot.size() == 3 && hot[0].key == fnB && hot[0].cycles == 500);
    hot = m.HotFunctions(2, &total, &used);                // f3 and the unattributed f2
    CHECK(used == 1 && total == 750);
}

void TestPools()
{
    MemoryPoolTracker t;
    // 4 KiB: the minimum block size applies -> 256 blocks of 16 bytes.
    std::vector<uint8_t> ram(4096, 0x00);
    for (int i = 0; i < 16; ++i) ram[32 + i] = static_cast<uint8_t>(i);   // block 2 holds data
    std::memset(ram.data() + 64, 0xFF, 16);                               // block 4: one value repeated = empty
    t.Sample(0, "Colour RAM", 0x05F00000u, ram.data(), 4096);
    const PoolUsage* u = &t.Pool(0);
    CHECK(u->valid && u->blockSize == 16 && u->cells.size() == 256);
    CHECK(u->usedBlocks == 1 && u->changedBlocks == 0);                   // nothing to compare with yet
    CHECK(u->cells[2] == 1 && u->cells[4] == 0 && u->cells[0] == 0);
    CHECK(Near(u->UsedFraction(), 1.0 / 256.0));

    ram[160] = 0x12;                                                      // block 10 gains data
    t.Sample(0, "Colour RAM", 0x05F00000u, ram.data(), 4096);
    u = &t.Pool(0);
    CHECK(u->usedBlocks == 2 && u->changedBlocks == 1);
    CHECK(u->cells[10] == 255 && u->cells[2] == 1);

    t.Sample(0, "Colour RAM", 0x05F00000u, ram.data(), 4096);            // unchanged: the mark fades
    u = &t.Pool(0);
    CHECK(u->changedBlocks == 0 && u->cells[10] < 255 && u->cells[10] > 1);
    for (int i = 0; i < 40; ++i) t.Sample(0, "Colour RAM", 0x05F00000u, ram.data(), 4096);
    CHECK(t.Pool(0).cells[10] == 1);                                      // ...down to "holds data", no further

    ram[160] = 0x00;                                                      // cleared again
    t.Sample(0, "Colour RAM", 0x05F00000u, ram.data(), 4096);
    CHECK(t.Pool(0).changedBlocks == 1 && t.Pool(0).usedBlocks == 1 && t.Pool(0).cells[10] == 255);
    t.Sample(0, "Colour RAM", 0x05F00000u, ram.data(), 4096);
    CHECK(t.Pool(0).cells[10] == 0);                                      // empty and settled

    // 1 MiB caps at kMaxBlocks blocks.
    std::vector<uint8_t> big(0x100000, 0);
    big[0x100000 - 1] = 1;
    t.Sample(1, "High work RAM", 0x06000000u, big.data(), 0x100000);
    CHECK(t.Pool(1).cells.size() == MemoryPoolTracker::kMaxBlocks && t.Pool(1).blockSize == 1024);
    CHECK(t.Pool(1).usedBlocks == 1 && t.Pool(1).cells.back() == 1);

    // A region the capture does not have is reported as such, not as empty.
    t.MarkMissing(2, "Sound RAM", 0x05A00000u, 0x80000);
    CHECK(!t.Pool(2).valid && t.Pool(2).name == "Sound RAM" && t.Count() == 3);
    // ...and coming back, it starts with no "changed" blocks.
    t.Sample(2, "Sound RAM", 0x05A00000u, big.data(), 0x80000);
    CHECK(t.Pool(2).valid && t.Pool(2).changedBlocks == 0);
    t.Reset();
    CHECK(t.Count() == 0);
}
PerfFrame Seq64(uint64_t seq, uint64_t cycles = 1)
{
    PerfFrame f = MakeFrame(static_cast<uint32_t>(seq), cycles);
    f.wireVersion = 27;
    f.sampleSeq = seq;
    return f;
}
PerfFrame GapOnly(uint64_t first, uint64_t last, uint64_t newest, bool evict = false)
{
    PerfFrame g;
    g.gapOnly = true;
    g.wireVersion = 27;
    (evict ? g.evictFirst : g.gapFirst) = first;
    (evict ? g.evictLast : g.gapLast) = last;
    (evict ? g.evictCount : g.gapCount) = last - first + 1;
    g.newestSeq = newest;
    return g;
}

// Lost records: where they are, what caused them, and the real denominator of a window.
void TestGaps()
{
    ProfilerModel m(16);
    m.Push(Seq64(1));
    // A header-only report (nothing to attach it to) is remembered, not dropped, and rides on the
    // next frame: the plot breaks there.
    m.Push(GapOnly(2, 5, 8));
    CHECK(m.Count() == 1);                                   // not a frame
    CHECK(!m.PendingServerGap().Empty() && m.PendingServerGap().first == 2 && m.PendingServerGap().last == 5);
    CHECK(m.NewestSeq() == 8 && m.TotalServerLost() == 4 && m.TotalDriverEvicted() == 0);
    PerfFrame f6 = Seq64(6);
    m.Push(f6);
    CHECK(m.Count() == 2 && m.PendingServerGap().Empty());
    CHECK(m.Latest().gapFirst == 2 && m.Latest().gapLast == 5 && m.Latest().HasGap());
    CHECK(m.TotalServerLost() == 4);                         // counted once, not again on attach
    // The driver's own eviction is a different cause and stays apart from the server's.
    PerfFrame f7 = Seq64(7);
    f7.evictFirst = 1; f7.evictLast = 3; f7.evictCount = 3;
    m.Push(f7);
    CHECK(m.TotalDriverEvicted() == 3 && m.TotalServerLost() == 4);
    CHECK(m.Latest().evictFirst == 1 && m.Latest().evictLast == 3 && m.Latest().gapLast == 0);
    m.Push(GapOnly(9, 11, 12, true));                        // evicted, header-only
    CHECK(m.PendingDriverEviction().first == 9 && m.PendingServerGap().Empty());
    m.Push(Seq64(12));
    CHECK(m.Latest().evictFirst == 9 && m.Latest().evictLast == 11 && m.Latest().gapLast == 0);
    CHECK(m.TotalDriverEvicted() == 6 && m.TotalServerLost() == 4);

    // A window reports what it is missing: the gaps of every frame but its oldest (whose gap lies
    // before the window), so the figure matches the frames' span.
    uint64_t srv = 99, drv = 99;
    m.LostIn(100, &srv, &drv);                               // frames 1, 6, 7, 12: gaps 4 (on 6), 3 (on 7), 3 (on 12)
    CHECK(srv == 4 && drv == 6);
    m.LostIn(2, &srv, &drv);                                 // frames 7, 12: only 12's gap is inside
    CHECK(srv == 0 && drv == 3);
    m.LostIn(1, &srv, &drv);
    CHECK(srv == 0 && drv == 0);
    uint64_t sum[SE_PERF_COUNT];
    CHECK(m.Sum(100, sum) == 4);                             // the denominator is the frames really held

    // Spans enclose, counts count. A frame whose eviction span 1..5 encloses a server gap 3..4 (the
    // exact repro: records 1, 2 and 5 evicted, 3..4 lost by the server) is 3 evicted + 2 lost, not 5.
    {
        ProfilerModel e(8);
        PerfFrame f = Seq64(6);
        f.evictFirst = 1; f.evictLast = 5; f.evictCount = 3;
        f.gapFirst = 3; f.gapLast = 4; f.gapCount = 2;
        e.Push(f);
        CHECK(e.TotalServerLost() == 2 && e.TotalDriverEvicted() == 3);
        e.Push(Seq64(7));
        e.Push(Seq64(8));
        uint64_t s2 = 99, d2 = 99;
        e.LostIn(8, &s2, &d2);                   // the first frame's own loss precedes the window
        CHECK(s2 == 0 && d2 == 0);
        e.Push(Seq64(9));
        // Two separate server gaps reported header-only, with delivered records between them in the
        // sequence space: the frame that receives them carries the sum, not the enclosing span.
        ProfilerModel g(8);
        g.Push(Seq64(1));
        g.Push(GapOnly(2, 3, 12));
        g.Push(GapOnly(10, 11, 12));
        CHECK(g.PendingServerGap().first == 2 && g.PendingServerGap().last == 11 && g.PendingServerGap().Count() == 4);
        g.Push(Seq64(12));
        CHECK(g.Latest().gapFirst == 2 && g.Latest().gapLast == 11 && g.Latest().gapCount == 4);
        CHECK(g.TotalServerLost() == 4);
        uint64_t s3 = 99, d3 = 99;
        g.LostIn(2, &s3, &d3);
        CHECK(s3 == 4 && d3 == 0);
    }

    // 64-bit ordering: crossing 2^32 is not a restart, an older record is not a new run's first.
    ProfilerModel w(8);
    w.Push(Seq64(0xFFFFFFFEull));
    w.Push(Seq64(0xFFFFFFFFull));
    w.Push(Seq64(0x100000000ull));
    w.Push(Seq64(0x100000001ull));
    CHECK(w.Count() == 4);
    w.Push(Seq64(0x100000001ull));                            // the same record again
    CHECK(w.Count() == 4);
    w.Push(Seq64(3));                                         // genuinely backwards: a new run
    CHECK(w.Count() == 1 && w.Latest().sampleSeq == 3 && w.TotalServerLost() == 0 && w.NewestSeq() == 3);

    // Hand-built frames with only a 32-bit seq: wrap-aware too.
    ProfilerModel h(8);
    h.Push(MakeFrame(0xFFFFFFFEu, 1));
    h.Push(MakeFrame(0xFFFFFFFFu, 1));
    h.Push(MakeFrame(1u, 1));                                 // wrapped past 0
    CHECK(h.Count() == 3 && h.Latest().seq == 1);
    h.Push(MakeFrame(1u, 9));
    CHECK(h.Count() == 3);

    // Clear forgets pending ranges and totals.
    m.Push(GapOnly(20, 25, 30));
    m.Clear();
    CHECK(m.PendingServerGap().Empty() && m.TotalServerLost() == 0 && m.NewestSeq() == 0);
}
}  // namespace

int main()
{
    TestShares();
    TestBuses();
    TestHistory();
    TestGaps();
    TestFunctions();
    TestPools();
    if (gFailures) { std::printf("%d check(s) failed\n", gFailures); return 1; }
    std::printf("ProfilerModelTests passed\n");
    return 0;
}
