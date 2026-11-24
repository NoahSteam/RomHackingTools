#include "ProfilerModel.h"

#include <algorithm>
#include <unordered_map>

namespace sfe
{

namespace
{
double Clamp01(double v) { return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v); }

// One CPU's split of 'frame' cycles, of which it was switched on for 'on'.
CpuShare ShareOf(double frame, double on, double sleep, double stall)
{
    CpuShare s;
    if (frame <= 0.0) return s;
    on = std::min(on, frame);
    // Idle time cannot exceed the time the CPU was on; sleep is trimmed last because the stall
    // counter is the one counted in coarser steps.
    stall = std::min(stall, on);
    sleep = std::min(sleep, on - stall);
    s.off = Clamp01((frame - on) / frame);
    s.stall = Clamp01(stall / frame);
    s.sleep = Clamp01(sleep / frame);
    s.busy = Clamp01((on - stall - sleep) / frame);
    return s;
}
}  // namespace

PerfShares ComputePerfShares(const uint64_t c[SE_PERF_COUNT], double frames)
{
    PerfShares out;
    if (frames <= 0.0) frames = 1.0;
    const double frame = static_cast<double>(c[SE_PERF_FRAME_CYCLES]);
    if (frame <= 0.0) return out;
    out.frameCycles = frame / frames;
    out.cpu[0] = ShareOf(frame, frame, static_cast<double>(c[SE_PERF_SH2_SLEEP]),
                         static_cast<double>(c[SE_PERF_SH2_STALL]));
    out.cpu[1] = ShareOf(frame, static_cast<double>(c[SE_PERF_SSH2_ON_CYCLES]),
                         static_cast<double>(c[SE_PERF_SH2_SLEEP + 1]),
                         static_cast<double>(c[SE_PERF_SH2_STALL + 1]));
    out.vdp1Draw = Clamp01(static_cast<double>(c[SE_PERF_V26_VDP1_ELAPSED]) / frame);
    // F4: command work is not a share of elapsed time. Instant-draw mode (and a table the VDP1 was
    // slowed down on) can exceed the frame, and clamping would hide exactly that.
    out.vdp1Work = static_cast<double>(c[SE_PERF_VDP1_WORK_CYCLES]) / frame;
    out.vdp1WorkOverFrame = out.vdp1Work > 1.0;
    out.vdp1DmaDiscarded = static_cast<double>(c[SE_PERF_VDP1_DMA_DISCARDED]) / frame;
    out.dsp = Clamp01(static_cast<double>(c[SE_PERF_DSP_CYCLES]) / frame);
    const double m68kTotal = static_cast<double>(c[SE_PERF_M68K_TOTAL_CYCLES]);
    out.m68k = m68kTotal > 0.0 ? Clamp01(static_cast<double>(c[SE_PERF_M68K_CYCLES]) / m68kTotal) : 0.0;
    out.vdp1DrawsPerFrame = static_cast<double>(c[SE_PERF_VDP1_DRAW_STARTS]) / frames;
    out.vdp1Commands = static_cast<double>(c[SE_PERF_VDP1_COMMANDS]) / frames;
    out.vdp1Overruns = static_cast<double>(c[SE_PERF_VDP1_OVERRUNS]) / frames;
    return out;
}

const char* PerfDeviceName(int device)
{
    switch (device)
    {
        case SE_PERF_DEV_BIOS:      return "Boot ROM";
        case SE_PERF_DEV_SMPC:      return "SMPC";
        case SE_PERF_DEV_BACKUP:    return "Backup RAM";
        case SE_PERF_DEV_WRAM_L:    return "Low work RAM";
        case SE_PERF_DEV_CS0_OTHER: return "Other (CS0)";
        case SE_PERF_DEV_ABUS_CS0:  return "Cartridge (CS0)";
        case SE_PERF_DEV_ABUS_CS1:  return "Cartridge (CS1)";
        case SE_PERF_DEV_ABUS_CD:   return "CD block";
        case SE_PERF_DEV_SCSP:      return "SCSP / sound RAM";
        case SE_PERF_DEV_VDP1:      return "VDP1";
        case SE_PERF_DEV_VDP2:      return "VDP2";
        case SE_PERF_DEV_SCU:       return "SCU registers";
        case SE_PERF_DEV_WRAM_H:    return "High work RAM";
        default:                    return "?";
    }
}

PerfBus PerfDeviceBus(int device)
{
    switch (device)
    {
        case SE_PERF_DEV_ABUS_CS0:
        case SE_PERF_DEV_ABUS_CS1:
        case SE_PERF_DEV_ABUS_CD:   return PerfBus::A;
        case SE_PERF_DEV_SCSP:
        case SE_PERF_DEV_VDP1:
        case SE_PERF_DEV_VDP2:      return PerfBus::B;
        default:                    return PerfBus::Cpu;
    }
}

const char* PerfBusName(PerfBus bus)
{
    switch (bus)
    {
        case PerfBus::A: return "A-bus";
        case PerfBus::B: return "B-bus";
        default:         return "CPU bus";
    }
}

const char* PerfDmaBusName(int dmaBus)
{
    switch (dmaBus)
    {
        case SE_PERF_BUS_A: return "A-bus";
        case SE_PERF_BUS_B: return "B-bus";
        default:            return "CPU bus";
    }
}

namespace
{
PerfBus DmaBusToPerfBus(int dmaBus)
{
    return dmaBus == SE_PERF_BUS_A ? PerfBus::A : (dmaBus == SE_PERF_BUS_B ? PerfBus::B : PerfBus::Cpu);
}
}  // namespace

BusTotals ComputeBusTotals(const uint64_t c[SE_PERF_COUNT])
{
    BusTotals t;
    for (int d = 0; d < SE_PERF_DEV_COUNT; ++d)
    {
        const int b = static_cast<int>(PerfDeviceBus(d));
        t.reads[b]  += c[SE_PERF_BUS_ACCESS + d * 2];
        t.writes[b] += c[SE_PERF_BUS_ACCESS + d * 2 + 1];
    }
    for (int src = 0; src < SE_PERF_BUS_COUNT; ++src)
        for (int dst = 0; dst < SE_PERF_BUS_COUNT; ++dst)
        {
            const uint64_t bytes = c[SE_PERF_DMA_ROUTE_BYTES + src * 3 + dst];
            t.dmaOut[static_cast<int>(DmaBusToPerfBus(src))] += bytes;
            t.dmaIn[static_cast<int>(DmaBusToPerfBus(dst))] += bytes;
        }
    return t;
}

// ---- ProfilerModel ----

ProfilerModel::ProfilerModel(size_t capacity) : mFrames(capacity ? capacity : 1) {}

void ProfilerModel::Clear()
{
    mHead = 0;
    mCount = 0;
}

const PerfFrame& ProfilerModel::At(size_t i) const
{
    return mFrames[(mHead + i) % mFrames.size()];
}

void ProfilerModel::Push(PerfFrame frame)
{
    if (mCount)
    {
        const uint32_t newest = Latest().seq;
        if (frame.seq == newest) return;
        if (frame.seq < newest) Clear();   // the emulator restarted its count: a different run
    }
    const size_t cap = mFrames.size();
    if (mCount == cap)
    {
        mFrames[mHead] = std::move(frame);
        mHead = (mHead + 1) % cap;
    }
    else
    {
        mFrames[(mHead + mCount) % cap] = std::move(frame);
        ++mCount;
    }
}

size_t ProfilerModel::Sum(size_t window, uint64_t out[SE_PERF_COUNT]) const
{
    for (int i = 0; i < SE_PERF_COUNT; ++i) out[i] = 0;
    const size_t n = std::min(window, mCount);
    for (size_t k = 0; k < n; ++k)
    {
        const PerfFrame& f = At(mCount - 1 - k);
        for (int i = 0; i < SE_PERF_COUNT; ++i) out[i] += f.c[i];
    }
    return n;
}

std::vector<PerfFunction> ProfilerModel::HotFunctions(size_t window, uint64_t* totalCycles,
                                                      size_t* framesUsed) const
{
    std::unordered_map<uint32_t, uint64_t> sum;
    uint64_t total = 0;
    size_t used = 0;
    const size_t n = std::min(window, mCount);
    for (size_t k = 0; k < n; ++k)
    {
        const PerfFrame& f = At(mCount - 1 - k);
        if (!f.functions) continue;
        ++used;
        total += f.fnOther;
        for (const PerfFrame::Fn& fn : f.fns)
        {
            sum[fn.key] += fn.cycles;
            total += fn.cycles;
        }
    }
    std::vector<PerfFunction> out;
    out.reserve(sum.size());
    for (const auto& kv : sum)
    {
        PerfFunction pf;
        pf.key = kv.first;
        pf.cycles = kv.second;
        out.push_back(pf);
    }
    // Ties broken by key so the list does not reshuffle between two identical windows.
    std::sort(out.begin(), out.end(), [](const PerfFunction& a, const PerfFunction& b)
    {
        return a.cycles != b.cycles ? a.cycles > b.cycles : a.key < b.key;
    });
    if (totalCycles) *totalCycles = total;
    if (framesUsed) *framesUsed = used;
    return out;
}

// ---- MemoryPoolTracker ----

void MemoryPoolTracker::Reset()
{
    mPools.clear();
}

MemoryPoolTracker::Tracked& MemoryPoolTracker::Slot(size_t index)
{
    if (mPools.size() <= index) mPools.resize(index + 1);
    return mPools[index];
}

void MemoryPoolTracker::MarkMissing(size_t index, const char* name, uint32_t base, uint32_t size)
{
    Tracked& t = Slot(index);
    t = Tracked();
    t.usage.name = name ? name : "";
    t.usage.base = base;
    t.usage.size = size;
}

void MemoryPoolTracker::Sample(size_t index, const char* name, uint32_t base,
                               const uint8_t* data, uint32_t size)
{
    if (!data || !size) { MarkMissing(index, name, base, size); return; }
    Tracked& t = Slot(index);
    uint32_t block = size / kMaxBlocks;
    if (block < kMinBlockSize) block = kMinBlockSize;
    const uint32_t blocks = (size + block - 1) / block;
    // A pool that changed shape (a different capture) starts over rather than comparing blocks
    // that are no longer the same bytes.
    if (t.usage.size != size || t.usage.blockSize != block || t.hashes.size() != blocks ||
        t.usage.base != base)
    {
        t = Tracked();
        t.hashes.assign(blocks, 0);
        t.usage.cells.assign(blocks, 0);
    }
    PoolUsage& u = t.usage;
    u.name = name ? name : "";
    u.base = base;
    u.size = size;
    u.blockSize = block;
    u.valid = true;
    u.usedBlocks = 0;
    u.changedBlocks = 0;
    for (uint32_t b = 0; b < blocks; ++b)
    {
        const uint8_t* p = data + static_cast<size_t>(b) * block;
        const uint32_t len = std::min(block, size - b * block);
        // FNV-1a, and in the same pass whether every byte equals the first.
        uint32_t h = 2166136261u;
        bool uniform = true;
        const uint8_t first = p[0];
        for (uint32_t i = 0; i < len; ++i)
        {
            h = (h ^ p[i]) * 16777619u;
            uniform = uniform && p[i] == first;
        }
        const bool changed = t.primed && h != t.hashes[b];
        t.hashes[b] = h;
        uint8_t cell = u.cells[b];
        if (changed) { cell = 255; ++u.changedBlocks; }
        else if (cell > 1) cell = static_cast<uint8_t>(cell > 17 ? cell - 16 : 1);   // fade
        if (uniform) { if (!changed) cell = 0; }
        else if (cell == 0) cell = 1;
        if (!uniform) ++u.usedBlocks;
        u.cells[b] = cell;
    }
    t.primed = true;
}

}  // namespace sfe
