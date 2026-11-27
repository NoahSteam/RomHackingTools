// The profiler's arithmetic, with no emulator and no UI: a profiler that divides by the wrong
// total still draws a plausible bar, so the shares, the history window, the function totals and
// the memory-pool classification are pinned here against hand-worked counters.
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
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

// ---- Window validity (review finding P2) ----
// A window averages several records; a v27-only quantity is summed over the records whose valid
// bit is set, against THEIR frame cycles, and says how many that was.

// A v27 frame of 400,000 cycles.
PerfFrame V27Frame(uint64_t seq, uint32_t valid)
{
    PerfFrame f = Seq64(seq, 400000);
    f.valid = valid;
    f.c[SE_PERF_SSH2_ON_CYCLES] = 400000;
    return f;
}

// The repro: four equal 400,000-cycle frames, three WITHOUT SE_PERF_VALID_VDP1_WORK and one with
// 120,000 work cycles. Averaged over the whole window that is 7.5%; the only measurement is 30%.
void CheckVdp1WorkWindow(bool validFirst)
{
    ProfilerModel m(16);
    for (uint64_t i = 1; i <= 4; ++i)
    {
        const bool valid = validFirst ? i == 1 : i == 4;
        PerfFrame f = V27Frame(i, valid ? SE_PERF_VALID_VDP1_WORK : 0);
        if (valid) { f.c[SE_PERF_VDP1_WORK_CYCLES] = 120000; f.c[SE_PERF_VDP1_DMA_DISCARDED] = 40000; }
        // Garbage in a frame that did not measure it must not leak in: not available means not read.
        else { f.c[SE_PERF_VDP1_WORK_CYCLES] = 999999; f.c[SE_PERF_VDP1_DMA_DISCARDED] = 999999; }
        m.Push(f);
    }
    const WindowShares w = m.Shares(4);
    CHECK(w.frames == 4 && w.vdp1Work.covered == 1 && w.vdp1Work.frames == 4);
    CHECK(w.vdp1Work.Any() && !w.vdp1Work.Full());
    CHECK(Near(w.shares.vdp1Work, 0.30));
    CHECK(Near(w.shares.vdp1DmaDiscarded, 0.10));
    // A one-frame window sees only the newest frame: it is valid in one order, not in the other.
    const WindowShares one = m.Shares(1);
    CHECK(one.vdp1Work.Any() == !validFirst);
    if (!validFirst) CHECK(Near(one.shares.vdp1Work, 0.30));
    else CHECK(Near(one.shares.vdp1Work, 0.0));   // and "not available" is the coverage, not this zero
}

void TestWindowValidityVdp1()
{
    CheckVdp1WorkWindow(false);   // valid last (the review's repro)
    CheckVdp1WorkWindow(true);    // valid first
    // No sample valid: not available at all.
    ProfilerModel none(8);
    for (uint64_t i = 1; i <= 3; ++i) none.Push(V27Frame(i, 0));
    CHECK(!none.Shares(3).vdp1Work.Any());
    // All valid: full coverage.
    ProfilerModel all(8);
    for (uint64_t i = 1; i <= 3; ++i)
    {
        PerfFrame f = V27Frame(i, SE_PERF_VALID_VDP1_WORK);
        f.c[SE_PERF_VDP1_WORK_CYCLES] = i == 3 ? 800000 : 100000;   // the last one is instant-draw: 200%
        all.Push(f);
    }
    const WindowShares a = all.Shares(3);
    CHECK(a.vdp1Work.Full() && a.vdp1OverFrameFrames == 1 && a.shares.vdp1WorkOverFrame);
    CHECK(Near(a.shares.vdp1Work, 1000000.0 / 1200000.0));
}

// The same rule for every other valid-gated quantity, in both orders.
void CheckOtherGated(bool validFirst)
{
    ProfilerModel m(16);
    for (uint64_t i = 1; i <= 4; ++i)
    {
        const bool v = validFirst ? i == 1 : i == 4;
        uint32_t valid = v ? (SE_PERF_VALID_SSH2_INT | SE_PERF_VALID_M68K_IDLE | SE_PERF_VALID_BUS_X |
                              SE_PERF_VALID_CACHE | SE_PERF_VALID_MODE) : 0;
        // The identity bit is not part of this mix: a window now ends at a timeline boundary, so a
        // window whose newest record states an identity and whose older ones do not would be cut to
        // one frame and stop testing the other gates. Identity coverage has its own test below.
        PerfFrame f = V27Frame(i, valid);
        f.c[SE_PERF_SSH2_ON_CYCLES] = v ? 200000 : 400000;          // unintegrated frames claim 100% on
        f.c[SE_PERF_SH2_SLEEP + 1] = v ? 100000 : 0;                // integrated: on half, a quarter busy
        f.c[SE_PERF_M68K_TOTAL_CYCLES] = 1000;
        f.c[SE_PERF_M68K_IDLE_CYCLES] = v ? 600 : 0;
        f.c[SE_PERF_M68K_CYCLES] = v ? 400 : 1000;
        f.c[SE_PERF_BUS_ACCESS_X + SE_PERF_XDEV_ABUS_DUMMY * 2] = v ? 12 : 777;
        f.c[SE_PERF_BUS_ACCESS_X + SE_PERF_XDEV_UNMAPPED * 2 + 1] = v ? 5 : 777;
        f.cacheMode = SE_PERF_CACHE_ON;
        f.mode = v ? 1u : 0u;
        f.frameNo = 100 + i; f.timelineId = 3;
        m.Push(f);
    }
    const WindowShares w = m.Shares(4);
    CHECK(w.slave.covered == 1 && w.slave.frames == 4);
    CHECK(Near(w.shares.cpu[1].off, 0.5) && Near(w.shares.cpu[1].sleep, 0.25) && Near(w.shares.cpu[1].busy, 0.25));
    CHECK(w.m68kIdle.covered == 1 && Near(w.m68kIdleShare, 0.6));
    CHECK(w.busX.covered == 1 && w.busXTotals.dummyReads == 12 && w.busXTotals.unmappedWrites == 5 &&
          w.busXTotals.dummyWrites == 0 && w.busXTotals.unmappedReads == 0);
    CHECK(w.cache.covered == 1 && w.cacheMode == SE_PERF_CACHE_ON && !w.cacheMixed);
    CHECK(w.mode.covered == 1 && w.modeFunctionsOn == 1);
    CHECK(!w.identity.Any());
    // None of them is "available" in a window of only the frames without the bit.
    ProfilerModel n(8);
    for (uint64_t i = 1; i <= 3; ++i) n.Push(V27Frame(i, 0));
    const WindowShares z = n.Shares(3);
    CHECK(!z.slave.Any() && !z.m68kIdle.Any() && !z.busX.Any() && !z.cache.Any() && !z.mode.Any() && !z.identity.Any());
}

// Identity coverage in a window where only one record states one. The newest record decides: with no
// identity on it there is no timeline to hold the others to (the window is whole and says "1 of 4");
// with one on it, the records before it that state none cannot be shown to share its timeline.
void TestIdentityCoverageInMixedWindow()
{
    for (int validFirst = 0; validFirst < 2; ++validFirst)
    {
        ProfilerModel m(16);
        for (uint64_t i = 1; i <= 4; ++i)
        {
            const bool v = validFirst ? i == 1 : i == 4;
            PerfFrame f = V27Frame(i, v ? SE_PERF_VALID_IDENTITY : 0u);
            f.frameNo = 100 + i; f.timelineId = 3;
            m.Push(f);
        }
        const WindowShares w = m.Shares(4);
        if (validFirst) CHECK(w.frames == 4 && w.identity.covered == 1 && w.identity.frames == 4 && !w.timelineCut);
        else            CHECK(w.frames == 1 && w.identity.covered == 1 && w.timelineCut);
    }
}

// The paired 68000 figures are one subset: idle and executing over the frames that carry the idle
// bit sum to 100% of that subset's clock; the all-frame executing share is a different quantity.
void CheckIdlePair(bool validFirst)
{
    ProfilerModel m(8);
    for (uint64_t i = 1; i <= 4; ++i)
    {
        const bool v = validFirst ? i == 1 : i == 4;
        PerfFrame f = V27Frame(i, v ? SE_PERF_VALID_M68K_IDLE : 0u);
        f.c[SE_PERF_M68K_TOTAL_CYCLES] = 100;
        f.c[SE_PERF_M68K_CYCLES] = v ? 0 : 100;
        f.c[SE_PERF_M68K_IDLE_CYCLES] = v ? 100 : 0;
        m.Push(f);
    }
    const WindowShares w = m.Shares(4);
    CHECK(w.m68kIdle.covered == 1 && w.m68kIdle.frames == 4);
    CHECK(Near(w.m68kIdleShare, 1.0) && Near(w.m68kIdleExecShare, 0.0));
    CHECK(Near(w.m68kIdleShare + w.m68kIdleExecShare, 1.0));
    CHECK(Near(w.shares.m68k, 0.75));   // every frame: a separate figure, not the pair's partner
}

void TestIdlePairSubset()
{
    CheckIdlePair(false);
    CheckIdlePair(true);
}

void TestWindowValidityOtherQuantities()
{
    CheckOtherGated(false);
    CheckOtherGated(true);
    // Two cache modes in one window are reported as mixed, not averaged into one of them.
    ProfilerModel m(8);
    PerfFrame a = V27Frame(1, SE_PERF_VALID_CACHE); a.cacheMode = SE_PERF_CACHE_ON;
    PerfFrame b = V27Frame(2, SE_PERF_VALID_CACHE); b.cacheMode = SE_PERF_CACHE_OFF;
    m.Push(a); m.Push(b);
    CHECK(m.Shares(2).cacheMixed && m.Shares(2).cache.covered == 2);
    CHECK(!m.Shares(1).cacheMixed);
}

// A window mixing a v26 server's frames and a v27 server's must not blend the elapsed VDP1 figure
// with command work, whichever came first.
void CheckMixedWire(bool v26First)
{
    ProfilerModel m(16);
    for (uint64_t i = 1; i <= 4; ++i)
    {
        const bool legacy = v26First ? i <= 2 : i > 2;
        PerfFrame f;
        if (legacy)
        {
            f = MakeFrame(static_cast<uint32_t>(i), 400000);
            f.sampleSeq = i;
            f.wireVersion = 26;
            f.c[SE_PERF_V26_VDP1_ELAPSED] = 100000;             // 25%
            f.c[SE_PERF_SSH2_ON_CYCLES] = 400000;
        }
        else
        {
            f = V27Frame(i, SE_PERF_VALID_VDP1_WORK | SE_PERF_VALID_SSH2_INT);
            f.c[SE_PERF_VDP1_WORK_CYCLES] = 200000;             // 50%
        }
        m.Push(f);
    }
    const WindowShares w = m.Shares(4);
    CHECK(w.v27Frames == 2);
    CHECK(w.vdp1Draw.covered == 2 && Near(w.shares.vdp1Draw, 0.25));
    CHECK(w.vdp1Work.covered == 2 && Near(w.shares.vdp1Work, 0.50));
    CHECK(w.slave.covered == 4);
}

void TestMixedV26V27Window()
{
    CheckMixedWire(true);
    CheckMixedWire(false);
}

// History: a frame that did not measure a value is a gap, never a point at 0.
void TestHistoryRuns()
{
    auto P = [](bool avail, bool brk = false) { HistoryPoint p; p.available = avail; p.value = 0.0; p.breakBefore = brk; return p; };
    // available, available, MISSING, available, available(break), available
    const std::vector<HistoryRun> r = SplitHistoryRuns({ P(true), P(true), P(false), P(true), P(true, true), P(true) });
    CHECK(r.size() == 3);
    if (r.size() == 3)
    {
        CHECK(r[0].first == 0 && r[0].count == 2);   // the missing frame ends the first run
        CHECK(r[1].first == 3 && r[1].count == 1);   // a break (lost records) ends the second
        CHECK(r[2].first == 4 && r[2].count == 2);
    }
    // All missing: nothing to draw, in particular no run of zeros.
    CHECK(SplitHistoryRuns({ P(false), P(false), P(false) }).empty());
    CHECK(SplitHistoryRuns({}).empty());
    // A zero that WAS measured is a point.
    HistoryPoint z; z.available = true; z.value = 0.0;
    CHECK(SplitHistoryRuns({ z, z }).size() == 1);
}

void TestFrameLabel()
{
    PerfFrame f = Seq64(1);
    f.frame = 4242;   // the snapshot number the batch arrived with: never what the label says
    CHECK(PerfFrameLabel(f).find("not available") != std::string::npos);
    CHECK(PerfFrameLabel(f).find("4242") == std::string::npos);
    f.valid = SE_PERF_VALID_IDENTITY; f.timelineId = 7; f.frameNo = 987654321012ull;
    CHECK(PerfFrameLabel(f) == "Frame 987654321012 (timeline 7)");
    PerfFrame v26 = MakeFrame(1, 1);
    v26.wireVersion = 26; v26.valid = SE_PERF_VALID_IDENTITY;   // a v26 record has no identity whatever its bits say
    v26.frameNo = 5;
    CHECK(PerfFrameLabel(v26).find("not available") != std::string::npos);
}

// ---- F7: function shares and the remainder ----

PerfFrame FnFrame(uint64_t seq, bool split)
{
    PerfFrame f = V27Frame(seq, split ? SE_PERF_VALID_OTHER | SE_PERF_VALID_SSH2_INT : SE_PERF_VALID_SSH2_INT);
    f.functions = true;
    return f;
}
const uint32_t kM1 = 0x06004000u, kM2 = 0x06005000u, kS1 = 0x06010000u | SE_PERF_FN_SLAVE;

void TestFunctionShares()
{
    // The plan's case: one function with 30,000 of 100,000 attributed cycles, 40,000 of them in Other.
    // (30,000 + 30,000 listed + 25,000 not admitted + 15,000 evicted.) Its share is 30%, not 50%.
    ProfilerModel m(8);
    PerfFrame f = FnFrame(1, true);
    f.fns = { { kM1, 30000 }, { kM2, 30000 } };
    f.otherNotAdmitted[0] = 25000; f.otherEvicted[0] = 15000;
    f.fnOther = 40000;
    m.Push(f);
    const FunctionWindow w = m.Functions(1, FnCpu::Both);
    CHECK(w.rows.size() == 2 && w.rows[0].cycles == 30000);
    CHECK(w.listed == 60000 && w.Other() == 40000 && w.Attributed() == 100000);
    CHECK(Near(static_cast<double>(w.rows[0].cycles) / static_cast<double>(w.Attributed()), 0.30));
    CHECK(w.otherNotAdmitted == 25000 && w.otherEvicted == 15000 && w.otherUnsplit == 0);

    // Separate master and slave: each filter's denominator is that CPU's own listed + Other.
    PerfFrame g = FnFrame(2, true);
    g.fns = { { kM1, 30000 }, { kS1, 10000 } };
    g.otherNotAdmitted[0] = 10000; g.otherEvicted[0] = 0;       // master attributed 40,000
    g.otherNotAdmitted[1] = 0;     g.otherEvicted[1] = 5000;    // slave attributed 15,000
    g.fnOther = 15000;
    ProfilerModel m2(8);
    m2.Push(g);
    const FunctionWindow master = m2.Functions(1, FnCpu::Master);
    const FunctionWindow slave = m2.Functions(1, FnCpu::Slave);
    const FunctionWindow both = m2.Functions(1, FnCpu::Both);
    CHECK(master.rows.size() == 1 && master.Attributed() == 40000 && master.Other() == 10000);
    CHECK(Near(static_cast<double>(master.rows[0].cycles) / static_cast<double>(master.Attributed()), 0.75));
    CHECK(slave.rows.size() == 1 && slave.Attributed() == 15000 && slave.otherEvicted == 5000);
    CHECK(both.Attributed() == 55000 && both.Attributed() == master.Attributed() + slave.Attributed());
    // listed + Other == attributed, per CPU.
    CHECK(master.listed + master.Other() == 40000 && slave.listed + slave.Other() == 15000);

    // A 65th key and a late dominant 3,073rd key: what the producer folded into Other stays in the
    // denominator and the saturation flags are reported by the frames that set them.
    PerfFrame h = FnFrame(3, true);
    h.fns = { { kM1, 500 } };
    h.satFlags = SE_PERF_SAT_TRUNCATED;                       // 65th key: the smallest evicted
    h.otherEvicted[0] = 7;
    ProfilerModel m3(8);
    m3.Push(h);
    PerfFrame k = FnFrame(4, true);
    k.fns = { { kM2, 100 } };
    k.satFlags = SE_PERF_SAT_TABLE_FULL | SE_PERF_SAT_TRUNCATED;   // a late dominant key was not admitted
    k.otherNotAdmitted[0] = 90000;
    m3.Push(k);
    const FunctionWindow s3 = m3.Functions(2, FnCpu::Both);
    CHECK(s3.tableFullFrames == 1 && s3.truncatedFrames == 2);
    CHECK(s3.otherNotAdmitted == 90000 && s3.otherEvicted == 7);
    CHECK(s3.Attributed() == 500 + 100 + 90000 + 7);
    CHECK(Near(static_cast<double>(s3.rows[0].cycles) / static_cast<double>(s3.Attributed()), 500.0 / 90607.0));
}

// Frames from an emulator without the split: Both keeps their single remainder; a CPU filter
// cannot assign it, so it uses only the frames that carry the split -- in either order.
void CheckSplitMix(bool splitFirst)
{
    ProfilerModel m(8);
    for (uint64_t i = 1; i <= 2; ++i)
    {
        const bool split = splitFirst ? i == 1 : i == 2;
        PerfFrame f = FnFrame(i, split);
        f.fns = { { kM1, 1000 } };
        f.fnOther = 4000;
        if (split) f.otherNotAdmitted[0] = 4000;
        m.Push(f);
    }
    const FunctionWindow both = m.Functions(2, FnCpu::Both);
    CHECK(both.used == 2 && both.splitFrames == 1);
    CHECK(both.otherNotAdmitted == 4000 && both.otherUnsplit == 4000 && both.Attributed() == 10000);
    const FunctionWindow master = m.Functions(2, FnCpu::Master);
    CHECK(master.attributed == 2 && master.used == 1);
    CHECK(master.listed == 1000 && master.Other() == 4000 && master.Attributed() == 5000);   // only the split frame
    // A window with no split frame has nothing for a CPU filter.
    ProfilerModel old(8);
    PerfFrame o = FnFrame(1, false); o.fns = { { kM1, 5 } }; o.fnOther = 9;
    old.Push(o);
    CHECK(old.Functions(1, FnCpu::Master).used == 0 && old.Functions(1, FnCpu::Both).used == 1);
}

void TestFunctionSplitCoverage()
{
    CheckSplitMix(true);
    CheckSplitMix(false);
}

// Unobserved time is busy time nobody was charged for. It is kept apart from Other (measured cycles).
void TestUnobserved()
{
    ProfilerModel m(4);
    PerfFrame f = FnFrame(1, true);
    f.c[SE_PERF_SH2_SLEEP] = 100000;              // master busy 300,000
    f.c[SE_PERF_SSH2_ON_CYCLES] = 0;              // slave off: busy 0
    f.fns = { { kM1, 200000 } };
    f.otherEvicted[0] = 50000;
    m.Push(f);
    const FunctionWindow w = m.Functions(1, FnCpu::Both);
    CHECK(w.busyKnown && w.busy == 300000 && w.Attributed() == 250000 && w.Unobserved() == 50000);
    CHECK(w.Other() == 50000);                    // measured Other, not the same 50,000
    // Slave time with no integrated on-time (v27 without the bit): the total is unknown, not zero.
    ProfilerModel u(4);
    PerfFrame g = V27Frame(1, SE_PERF_VALID_OTHER);
    g.functions = true; g.fns = { { kM1, 1 } };
    u.Push(g);
    CHECK(!u.Functions(1, FnCpu::Both).busyKnown && u.Functions(1, FnCpu::Both).Unobserved() == 0);
    CHECK(u.Functions(1, FnCpu::Master).busyKnown);
}

// ---- F10: the pool tracker only compares samples of the same capture ----
void TestPoolRePrime()
{
    std::vector<uint8_t> a(4096, 0), b(4096, 0);
    for (size_t i = 0; i < b.size(); ++i) b[i] = static_cast<uint8_t>(i * 7 + 1);   // unrelated, same size
    MemoryPoolTracker t;
    t.SetSource(1, 1, 1, 0);
    t.Sample(0, "Colour RAM", 0x05F00000u, a.data(), 4096);
    t.Sample(0, "Colour RAM", 0x05F00000u, a.data(), 4096);
    CHECK(t.Pool(0).changedBlocks == 0);
    // A real change within one source is reported.
    std::vector<uint8_t> a2 = a; a2[100] = 9;
    t.SetSource(1, 1, 1, 0);
    t.Sample(0, "Colour RAM", 0x05F00000u, a2.data(), 4096);
    CHECK(t.Pool(0).changedBlocks == 1);
    // Another source with different bytes: the first sample after the switch changed nothing.
    t.SetSource(2, 1, 1, 0);
    t.Sample(0, "Colour RAM", 0x05F00000u, b.data(), 4096);
    CHECK(t.Pool(0).changedBlocks == 0 && t.Pool(0).valid);
    t.Sample(0, "Colour RAM", 0x05F00000u, b.data(), 4096);
    CHECK(t.Pool(0).changedBlocks == 0);
    // A new emulator instance, with the same source id, re-primes too.
    t.SetSource(2, 2, 1, 0);
    t.Sample(0, "Colour RAM", 0x05F00000u, a.data(), 4096);
    CHECK(t.Pool(0).changedBlocks == 0);
    // ...and so does a new timeline (rewind, load).
    t.SetSource(2, 2, 2, 0);
    t.Sample(0, "Colour RAM", 0x05F00000u, b.data(), 4096);
    CHECK(t.Pool(0).changedBlocks == 0);
    // Unchanged identity: a change is a change again.
    t.SetSource(2, 2, 2, 0);
    t.Sample(0, "Colour RAM", 0x05F00000u, a.data(), 4096);
    CHECK(t.Pool(0).changedBlocks == 256);
}

// F10 follow-up: a live capture that states its own timeline is judged by THAT, not by the perf stream.
void TestPoolCaptureTimeline()
{
    std::vector<uint8_t> a(4096, 1), b(4096, 2);
    for (size_t i = 0; i < a.size(); ++i) { a[i] = static_cast<uint8_t>(i * 3); b[i] = static_cast<uint8_t>(i * 5 + 9); }
    MemoryPoolTracker t;
    PoolCaptureSource s;
    s.sourceId = 1; s.instance = 1; s.live = true; s.captureTimeline = 1; s.streamTimeline = 1; s.captureRevision = 1;
    CHECK(t.BeginSample(s));
    t.Sample(0, "High work RAM", 0x06000000u, a.data(), 4096);
    const uint32_t run1 = t.Run();
    // The stream is already on timeline 2, the capture still timeline 1: same capture, no re-prime.
    s.streamTimeline = 2; s.captureRevision = 2;
    CHECK(t.BeginSample(s) && t.Timeline() == 1 && t.Run() == run1);
    t.Sample(0, "High work RAM", 0x06000000u, a.data(), 4096);
    CHECK(t.Pool(0).changedBlocks == 0);
    // The capture reaches timeline 2: a new run, nothing changed against the other timeline's bytes.
    s.captureTimeline = 2; s.captureRevision = 3;
    CHECK(t.BeginSample(s) && t.Timeline() == 2 && t.Run() == run1 + 1 && t.Count() == 0);
    t.Sample(0, "High work RAM", 0x06000000u, b.data(), 4096);
    CHECK(t.Pool(0).changedBlocks == 0);
    // And back to a change within the timeline being reported as one.
    s.captureRevision = 4;
    CHECK(t.BeginSample(s));
    t.Sample(0, "High work RAM", 0x06000000u, a.data(), 4096);
    CHECK(t.Pool(0).changedBlocks == 256);
}

// A server that does not stamp its captures: a timeline change seen on the stream re-primes and then
// nothing is sampled until the capture is provably past the server's ring.
void TestPoolUnstampedCapture()
{
    MemoryPoolTracker t;
    PoolCaptureSource s;
    s.sourceId = 1; s.instance = 1; s.live = true; s.captureTimeline = 0; s.streamTimeline = 1; s.captureRevision = 10;
    CHECK(t.BeginSample(s) && t.SettlingCaptures() == 0);
    std::vector<uint8_t> a(4096, 7);
    t.Sample(0, "High work RAM", 0x06000000u, a.data(), 4096);
    s.streamTimeline = 2;   // a load, seen on the stream
    CHECK(!t.BeginSample(s) && t.Count() == 0 && t.SettlingCaptures() == MemoryPoolTracker::kUnverifiedSettleCaptures);
    CHECK(!t.BeginSample(s) && t.SettlingCaptures() == MemoryPoolTracker::kUnverifiedSettleCaptures);   // the same capture again counts for nothing
    for (uint32_t i = 1; i <= MemoryPoolTracker::kUnverifiedSettleCaptures; ++i)
    {
        s.captureRevision = 10 + i;
        const bool ok = t.BeginSample(s);
        CHECK(ok == (i == MemoryPoolTracker::kUnverifiedSettleCaptures));
    }
    CHECK(t.SettlingCaptures() == 0);
    // Another change while settled starts the wait again; a file or scrub never waits.
    s.streamTimeline = 3;
    CHECK(!t.BeginSample(s));
    PoolCaptureSource f;
    f.sourceId = 9; f.instance = 1; f.live = false; f.streamTimeline = 4;
    CHECK(t.BeginSample(f) && t.SettlingCaptures() == 0);
}

// Scrubbing: forward steps within one source keep comparing (frame 10 -> 11), scrubbing backward
// is a jump (11 -> 5) and re-primes; an unknown frame (0) never counts as backward.
void TestPoolScrubBackward()
{
    std::vector<uint8_t> a(4096, 0), b(4096, 0);
    b[0] = 1;
    MemoryPoolTracker t;
    t.SetSource(5, 1, 1, 10);
    t.Sample(0, "VDP2 VRAM", 0x05E00000u, a.data(), 4096);
    t.SetSource(5, 1, 1, 11);
    t.Sample(0, "VDP2 VRAM", 0x05E00000u, b.data(), 4096);
    CHECK(t.Pool(0).changedBlocks == 1);                   // forward, same source: a change
    t.SetSource(5, 1, 1, 5);                               // scrubbed back
    t.Sample(0, "VDP2 VRAM", 0x05E00000u, a.data(), 4096);
    CHECK(t.Pool(0).changedBlocks == 0);
    t.SetSource(5, 1, 1, 0);                               // frame unknown: not backward
    t.Sample(0, "VDP2 VRAM", 0x05E00000u, b.data(), 4096);
    CHECK(t.Pool(0).changedBlocks == 1);
}
// ---- Scrubbed view: find a record by identity, windows that end at it ----

// A v27 frame with an identity and a distinguishing cycle count (its frame number), so a window's
// summed cycles say exactly which frames it held.
PerfFrame IdFrame(uint64_t seq, uint32_t timeline, uint64_t frameNo, bool identity = true)
{
    PerfFrame f = Seq64(seq, frameNo);
    f.valid = SE_PERF_VALID_SSH2_INT | (identity ? SE_PERF_VALID_IDENTITY : 0u);
    f.timelineId = timeline;
    f.frameNo = frameNo;
    return f;
}

void TestFindIdentity()
{
    ProfilerModel m(16);
    // Timeline 1 frames 10..14, then a rewind: timeline 2 replays 12..14.
    for (uint64_t n = 10; n <= 14; ++n) m.Push(IdFrame(n, 1, n));
    for (uint64_t n = 12; n <= 14; ++n) m.Push(IdFrame(100 + n, 2, n));
    size_t at = 99;
    CHECK(m.FindIdentity(1, 12, &at) && at == 2);        // exact match
    CHECK(m.FindIdentity(2, 12, &at) && at == 5);        // same frameNo, the other timeline: its own record
    CHECK(m.FindIdentity(2, 14, &at) && at == 7);
    CHECK(!m.FindIdentity(2, 11, &at));                  // 11 exists only on timeline 1: never a match
    CHECK(!m.FindIdentity(3, 12, &at));                  // unknown timeline
    CHECK(!m.FindIdentity(1, 15, &at));                  // newer than anything stored
    CHECK(!m.FindIdentity(1, 9, &at));                   // older than anything stored
    CHECK(m.FindIdentity(1, 14, nullptr));               // the index is optional

    // A record without SE_PERF_VALID_IDENTITY has no identity to match, whatever its fields hold; and
    // neither does a v26 record whose identity bit happens to be set.
    ProfilerModel n(16);
    n.Push(IdFrame(1, 1, 50, false));
    PerfFrame v26 = IdFrame(2, 1, 51);
    v26.wireVersion = 26;
    n.Push(v26);
    CHECK(!n.FindIdentity(1, 50, &at));
    CHECK(!n.FindIdentity(1, 51, &at));
    CHECK(!n.FindIdentity(0, 0, &at));                   // a zeroed record is not "timeline 0, frame 0"
}

void TestFindIdentityEvictedAndWrapped()
{
    ProfilerModel m(8);
    for (uint64_t n = 1; n <= 20; ++n) m.Push(IdFrame(n, 1, n));   // the ring wrapped twice: 13..20 kept
    size_t at = 99;
    CHECK(m.Count() == 8);
    CHECK(!m.FindIdentity(1, 12, &at));                  // evicted
    CHECK(m.FindIdentity(1, 13, &at) && at == 0);        // the oldest retained
    CHECK(m.FindIdentity(1, 20, &at) && at == 7);        // the newest
    CHECK(m.FindIdentity(1, 16, &at) && at == 3 && m.At(at).frameNo == 16);   // through the wrap
}

void TestWindowEndingAtAnchor()
{
    ProfilerModel m(16);
    for (uint64_t n = 1; n <= 10; ++n) m.Push(IdFrame(n, 1, n));   // cycles of frame n are n
    size_t at = 0;
    CHECK(m.FindIdentity(1, 6, &at) && at == 5);
    // Four frames ending at frame 6: 3+4+5+6, not the newest four (7..10).
    const WindowShares w = m.Shares(4, at + 1);
    CHECK(w.frames == 4 && Near(w.shares.frameCycles, (3 + 4 + 5 + 6) / 4.0));
    uint64_t sum[SE_PERF_COUNT];
    CHECK(m.Sum(4, at + 1, sum) == 4 && sum[SE_PERF_FRAME_CYCLES] == 18);
    // The unanchored forms are the newest window, unchanged.
    CHECK(m.Shares(4).frames == 4 && Near(m.Shares(4).shares.frameCycles, (7 + 8 + 9 + 10) / 4.0));
    CHECK(Near(m.Shares(4, m.Count()).shares.frameCycles, m.Shares(4).shares.frameCycles));

    // Near the start of the ring: asking for ten frames ending at frame 3 holds only 1, 2, 3.
    CHECK(m.FindIdentity(1, 3, &at));
    const WindowShares early = m.Shares(10, at + 1);
    CHECK(early.frames == 3 && Near(early.shares.frameCycles, 2.0));
    CHECK(m.Sum(10, at + 1, sum) == 3);
    // The oldest record alone.
    CHECK(m.FindIdentity(1, 1, &at) && m.Shares(5, at + 1).frames == 1);

    // An anchor past the end clamps to what is stored rather than reading beyond it.
    CHECK(m.Shares(3, 1000).frames == 3 && Near(m.Shares(3, 1000).shares.frameCycles, 9.0));
    CHECK(m.Shares(3, 0).frames == 0);
}

void TestWindowEndingAtAnchorWrapped()
{
    ProfilerModel m(8);
    for (uint64_t n = 1; n <= 13; ++n) m.Push(IdFrame(n, 1, n));   // holds 6..13, head is mid-array
    size_t at = 0;
    CHECK(m.FindIdentity(1, 9, &at) && at == 3);
    const WindowShares w = m.Shares(3, at + 1);                    // 7, 8, 9
    CHECK(w.frames == 3 && Near(w.shares.frameCycles, 8.0));
    // Asking for more than the ring holds before the anchor: 6, 7, 8, 9.
    CHECK(m.Shares(100, at + 1).frames == 4);
}

// Validity and coverage are the existing rules, just over the anchored frames, and the lost-record
// count of the window is the anchored window's, not the newest one's.
void TestAnchoredWindowCoverageAndLoss()
{
    ProfilerModel m(16);
    for (uint64_t n = 1; n <= 8; ++n)
    {
        PerfFrame f = IdFrame(n, 1, n);
        if (n >= 3 && n <= 4) f.valid |= SE_PERF_VALID_VDP1_WORK;   // only 3 and 4 measured it
        if (n == 4) f.gapCount = 2;                                  // two records lost just before frame 4
        if (n == 8) f.gapCount = 5;
        m.Push(f);
    }
    size_t at = 0;
    CHECK(m.FindIdentity(1, 5, &at));
    const WindowShares w = m.Shares(4, at + 1);                      // frames 2..5
    CHECK(w.vdp1Work.covered == 2 && w.vdp1Work.frames == 4);        // "2 of 4 frames"
    const WindowShares live = m.Shares(4);                           // frames 5..8
    CHECK(live.vdp1Work.covered == 0);
    uint64_t srv = 0, drv = 0;
    m.LostIn(4, at + 1, &srv, &drv);
    CHECK(srv == 2);                                                 // frame 4's gap, inside frames 2..5
    m.LostIn(4, &srv, &drv);
    CHECK(srv == 5);                                                 // the newest window's: frame 8's
}

// ---- A window never crosses a timeline change ----
// After a rewind the ring holds the replaced timeline's records right before the new one's. A frame's
// cycle count is its frame number here, so a window's sum says which records it held.

// Timeline 1 frames 100..104 (5), then timeline 2 frames 50..52 (3); sampleSeq keeps rising.
void FillRewound(ProfilerModel& m)
{
    uint64_t seq = 1;
    for (uint64_t n = 100; n <= 104; ++n) m.Push(IdFrame(seq++, 1, n));
    for (uint64_t n = 50; n <= 52; ++n)
    {
        PerfFrame f = IdFrame(seq++, 2, n);
        f.functions = true;
        f.valid |= SE_PERF_VALID_OTHER;
        f.fns = { { 0x06000100u, static_cast<uint32_t>(10 * n) } };
        m.Push(f);
    }
}

void TestWindowStopsAtTimelineChange()
{
    ProfilerModel m(16);
    FillRewound(m);
    uint64_t sum[SE_PERF_COUNT];

    // Live view: ten frames asked for, only the three of timeline 2 are used.
    const WindowShares live = m.Shares(10);
    CHECK(live.frames == 3 && live.timelineCut);
    CHECK(Near(live.shares.frameCycles, (50 + 51 + 52) / 3.0));
    CHECK(live.identity.frames == 3 && live.identity.covered == 3);   // "k of n" says 3 of 3
    CHECK(m.Sum(10, sum) == 3 && sum[SE_PERF_FRAME_CYCLES] == 153);
    const FunctionWindow fw = m.Functions(10, FnCpu::Both);
    CHECK(fw.frames == 3 && fw.attributed == 3 && fw.used == 3 && fw.timelineCut);
    CHECK(fw.listed == 10 * (50 + 51 + 52));
    uint64_t total = 0; size_t used = 0;
    m.HotFunctions(10, &total, &used);
    CHECK(used == 3 && total == 10 * (50 + 51 + 52));
    uint64_t srv = 0, drv = 0;
    CHECK(m.UsableFrames(10, m.Count()) == 3);

    // A window that fits inside the new timeline is not "cut".
    const WindowShares two = m.Shares(2);
    CHECK(two.frames == 2 && !two.timelineCut && Near(two.shares.frameCycles, 51.5));

    // Anchored at a timeline-1 record: only timeline 1 (the newer records are not in the window).
    size_t at = 0;
    CHECK(m.FindIdentity(1, 103, &at) && at == 3);
    const WindowShares a = m.Shares(10, at + 1);
    CHECK(a.frames == 4 && !a.timelineCut && Near(a.shares.frameCycles, (100 + 101 + 102 + 103) / 4.0));
    // Anchored at the FIRST record of timeline 2: just itself (the one before is the old timeline).
    CHECK(m.FindIdentity(2, 50, &at) && at == 5);
    const WindowShares first = m.Shares(10, at + 1);
    CHECK(first.frames == 1 && first.timelineCut && Near(first.shares.frameCycles, 50.0));
    CHECK(m.Sum(10, at + 1, sum) == 1 && sum[SE_PERF_FRAME_CYCLES] == 50);
    CHECK(m.Functions(10, at + 1, FnCpu::Both).used == 1);
    // Anchored at the middle of timeline 2: 50 and 51.
    CHECK(m.FindIdentity(2, 51, &at));
    CHECK(m.Shares(10, at + 1).frames == 2);
    // The boundary exactly at the window's edge: two frames asked for ending at 51 are 50, 51 -- all
    // timeline 2, nothing cut; three asked for would reach frame 104 and is cut to two.
    CHECK(m.Shares(2, at + 1).frames == 2 && !m.Shares(2, at + 1).timelineCut);
    CHECK(m.Shares(3, at + 1).frames == 2 && m.Shares(3, at + 1).timelineCut);

    // Lost records: the boundary frame's gap lies before the window, as for any oldest frame, and a
    // gap carried by an old-timeline record is not counted.
    ProfilerModel g(16);
    uint64_t seq = 1;
    PerfFrame o = IdFrame(seq++, 1, 10); o.gapCount = 7; g.Push(o);
    PerfFrame n1 = IdFrame(seq++, 2, 3); n1.gapCount = 4; g.Push(n1);   // first of the new timeline
    PerfFrame n2 = IdFrame(seq++, 2, 4); n2.gapCount = 2; g.Push(n2);
    g.LostIn(10, &srv, &drv);
    CHECK(srv == 2);                       // n2's own gap; n1's precedes the window, o's is not in it
    CHECK(g.Shares(10).frames == 2);
}

void TestTimelineWindowWithoutIdentity()
{
    // The end record has no identity: no boundary is invented, the old position-only behaviour.
    ProfilerModel m(16);
    uint64_t seq = 1;
    for (uint64_t n = 1; n <= 3; ++n) m.Push(IdFrame(seq++, 1, n));
    for (uint64_t n = 4; n <= 5; ++n) m.Push(IdFrame(seq++, 2, n, false));
    CHECK(m.Shares(10).frames == 5 && !m.Shares(10).timelineCut);
    uint64_t sum[SE_PERF_COUNT];
    CHECK(m.Sum(10, sum) == 5);

    // The end record has identity but one before it does not: that record ends the window.
    ProfilerModel k(16);
    seq = 1;
    k.Push(IdFrame(seq++, 2, 1));
    k.Push(IdFrame(seq++, 2, 2, false));
    k.Push(IdFrame(seq++, 2, 3));
    k.Push(IdFrame(seq++, 2, 4));
    CHECK(k.Shares(10).frames == 2 && k.Shares(10).timelineCut);
    // A v26 record before a v27 one with identity likewise (it cannot state a timeline).
    ProfilerModel v(16);
    seq = 1;
    PerfFrame old = IdFrame(seq++, 2, 1);
    old.wireVersion = 26;
    v.Push(old);
    v.Push(IdFrame(seq++, 2, 2));
    CHECK(v.Shares(10).frames == 1 && v.Shares(10).timelineCut);

    // Ordinary frame-number gaps on one timeline are not a boundary.
    ProfilerModel gap(16);
    seq = 1;
    for (uint64_t n : { 10u, 11u, 40u, 41u }) gap.Push(IdFrame(seq++, 1, n));
    CHECK(gap.Shares(10).frames == 4 && !gap.Shares(10).timelineCut);
}

void TestTimelineWindowWrappedRing()
{
    ProfilerModel m(8);
    uint64_t seq = 1;
    for (uint64_t n = 1; n <= 9; ++n) m.Push(IdFrame(seq++, 1, n));    // wraps: head is mid-array
    for (uint64_t n = 1; n <= 3; ++n) m.Push(IdFrame(seq++, 2, n));    // holds 5 of timeline 1, 3 of 2
    CHECK(m.Count() == 8);
    CHECK(m.Shares(100).frames == 3);
    uint64_t sum[SE_PERF_COUNT];
    CHECK(m.Sum(100, sum) == 3 && sum[SE_PERF_FRAME_CYCLES] == 6);
    // Timeline 1 only: records 5..9 survived.
    size_t at = 0;
    CHECK(m.FindIdentity(1, 9, &at));
    CHECK(m.Shares(100, at + 1).frames == 5 && !m.Shares(100, at + 1).timelineCut);
    // The whole ring on one timeline is unchanged.
    ProfilerModel one(8);
    for (uint64_t n = 1; n <= 12; ++n) one.Push(IdFrame(n, 1, n));
    CHECK(one.Shares(100).frames == 8 && !one.Shares(100).timelineCut);
}

}  // namespace

int main()
{
    TestIdentityCoverageInMixedWindow();
    TestWindowStopsAtTimelineChange();
    TestTimelineWindowWithoutIdentity();
    TestTimelineWindowWrappedRing();
    TestFindIdentity();
    TestFindIdentityEvictedAndWrapped();
    TestWindowEndingAtAnchor();
    TestWindowEndingAtAnchorWrapped();
    TestAnchoredWindowCoverageAndLoss();
    TestShares();
    TestBuses();
    TestHistory();
    TestGaps();
    TestFunctions();
    TestPools();
    TestWindowValidityVdp1();
    TestWindowValidityOtherQuantities();
    TestMixedV26V27Window();
    TestIdlePairSubset();
    TestHistoryRuns();
    TestFrameLabel();
    TestFunctionShares();
    TestFunctionSplitCoverage();
    TestUnobserved();
    TestPoolRePrime();
    TestPoolScrubBackward();
    TestPoolCaptureTimeline();
    TestPoolUnstampedCapture();
    if (gFailures) { std::printf("%d check(s) failed\n", gFailures); return 1; }
    std::printf("ProfilerModelTests passed\n");
    return 0;
}
