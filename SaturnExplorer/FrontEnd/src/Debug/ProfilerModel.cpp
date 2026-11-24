#include "ProfilerModel.h"

#include "SeLiveProtocol.h"

#include <algorithm>
#include <cstdio>
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

const char* PerfXDeviceName(int xdev)
{
    switch (xdev)
    {
        case SE_PERF_XDEV_ABUS_DUMMY: return "A-bus dummy area";
        case SE_PERF_XDEV_UNMAPPED:   return "Unmapped";
        default:                      return "?";
    }
}

const char* PerfBusUnitLabel() { return "bus requests"; }

const char* PerfCacheModeName(uint32_t cacheMode)
{
    switch (cacheMode)
    {
        case SE_PERF_CACHE_ON:  return "instruction cache emulated";
        case SE_PERF_CACHE_OFF: return "instruction cache not emulated";
        default:                return "cache mode unknown";
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
    // F8: neither bucket is a CPU-bus (or any bus's) traffic.
    t.dummyReads     = c[SE_PERF_BUS_ACCESS_X + SE_PERF_XDEV_ABUS_DUMMY * 2];
    t.dummyWrites    = c[SE_PERF_BUS_ACCESS_X + SE_PERF_XDEV_ABUS_DUMMY * 2 + 1];
    t.unmappedReads  = c[SE_PERF_BUS_ACCESS_X + SE_PERF_XDEV_UNMAPPED * 2];
    t.unmappedWrites = c[SE_PERF_BUS_ACCESS_X + SE_PERF_XDEV_UNMAPPED * 2 + 1];
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
    mServerLost = mDriverEvicted = mNewestSeq = 0;
    mPendServer = mPendEvict = SeqRange();
}

const PerfFrame& ProfilerModel::At(size_t i) const
{
    return mFrames[(mHead + i) % mFrames.size()];
}

namespace
{
void AddRange(ProfilerModel::SeqRange& r, uint64_t first, uint64_t last, uint64_t count)
{
    r.count += count;
    if (last == 0) return;
    if (r.last == 0) { r.first = first; r.last = last; return; }
    r.first = (std::min)(r.first, first);
    r.last = (std::max)(r.last, last);
}
}  // namespace

void ProfilerModel::Push(PerfFrame frame)
{
    if (mCount && !frame.gapOnly)
    {
        const PerfFrame& newest = Latest();
        if (frame.sampleSeq != 0 && newest.sampleSeq != 0)
        {
            if (frame.sampleSeq == newest.sampleSeq) return;
            if (frame.sampleSeq < newest.sampleSeq) Clear();   // the emulator restarted its count
        }
        else
        {
            // No 64-bit sequence (a hand-built frame): the 32-bit one, wrap-aware.
            const int32_t delta = static_cast<int32_t>(frame.seq - newest.seq);
            if (delta == 0) return;
            if (delta < 0) Clear();
        }
    }
    if (frame.newestSeq > mNewestSeq) mNewestSeq = frame.newestSeq;
    if (frame.sampleSeq > mNewestSeq) mNewestSeq = frame.sampleSeq;
    mServerLost += frame.gapCount;
    mDriverEvicted += frame.evictCount;
    if (frame.gapOnly)
    {
        AddRange(mPendServer, frame.gapFirst, frame.gapLast, frame.gapCount);
        AddRange(mPendEvict, frame.evictFirst, frame.evictLast, frame.evictCount);
        return;
    }
    // What a gap-only entry reported rides on this frame, so the plot breaks right before it.
    if (!mPendServer.Empty())
    {
        SeqRange r{ frame.gapFirst, frame.gapLast, frame.gapCount };
        AddRange(r, mPendServer.first, mPendServer.last, mPendServer.count);
        frame.gapFirst = r.first; frame.gapLast = r.last; frame.gapCount = r.count;
        mPendServer = SeqRange();
    }
    if (!mPendEvict.Empty())
    {
        SeqRange r{ frame.evictFirst, frame.evictLast, frame.evictCount };
        AddRange(r, mPendEvict.first, mPendEvict.last, mPendEvict.count);
        frame.evictFirst = r.first; frame.evictLast = r.last; frame.evictCount = r.count;
        mPendEvict = SeqRange();
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

void ProfilerModel::LostIn(size_t window, uint64_t* serverLost, uint64_t* driverEvicted) const
{
    uint64_t srv = 0, drv = 0;
    const size_t n = std::min(window, mCount);
    for (size_t k = 0; k + 1 < n; ++k)   // the oldest frame's own gap lies before the window
    {
        const PerfFrame& f = At(mCount - 1 - k);
        srv += f.gapCount;
        drv += f.evictCount;
    }
    if (serverLost) *serverLost = srv;
    if (driverEvicted) *driverEvicted = drv;
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

// ---- Windowed figures ----

std::vector<HistoryRun> SplitHistoryRuns(const std::vector<HistoryPoint>& points)
{
    std::vector<HistoryRun> runs;
    bool open = false;
    for (size_t i = 0; i < points.size(); ++i)
    {
        const HistoryPoint& p = points[i];
        if (!p.available) { open = false; continue; }
        if (open && !p.breakBefore) { ++runs.back().count; continue; }
        runs.push_back({ i, 1 });
        open = true;
    }
    return runs;
}

std::string PerfFrameLabel(const PerfFrame& frame)
{
    char buf[96];
    if (!frame.LegacyVdp1() && frame.Has(SE_PERF_VALID_IDENTITY))
        std::snprintf(buf, sizeof(buf), "Frame %llu (timeline %u)",
                      static_cast<unsigned long long>(frame.frameNo), frame.timelineId);
    else
        std::snprintf(buf, sizeof(buf), "Frame: not available (this emulator does not number its frames)");
    return buf;
}

namespace
{
// Counters of the frames a predicate selected, and how many that was.
struct Part
{
    size_t covered = 0;
    uint64_t c[SE_PERF_COUNT] = {};
    void Add(const PerfFrame& f)
    {
        ++covered;
        for (int i = 0; i < SE_PERF_COUNT; ++i) c[i] += f.c[i];
    }
};
bool V27(const PerfFrame& f) { return !f.LegacyVdp1(); }
// A v26 record's slave on-time is its producer's all-or-nothing figure; a v27 one's is meaningful
// only with the integration bit.
bool SlaveKnown(const PerfFrame& f) { return f.SlaveOnTimeKnown(); }
}  // namespace

WindowShares ProfilerModel::Shares(size_t window) const
{
    WindowShares out;
    const size_t n = std::min(window, mCount);
    out.frames = n;
    Part all, slave, draw, work, idle, busx;
    size_t cacheOff = 0, cacheOn = 0;
    for (size_t k = 0; k < n; ++k)
    {
        const PerfFrame& f = At(mCount - 1 - k);
        all.Add(f);
        if (V27(f)) ++out.v27Frames;
        if (SlaveKnown(f)) slave.Add(f);
        if (f.LegacyVdp1()) draw.Add(f);
        if (V27(f) && f.Has(SE_PERF_VALID_VDP1_WORK))
        {
            work.Add(f);
            if (f.c[SE_PERF_VDP1_WORK_CYCLES] > f.c[SE_PERF_FRAME_CYCLES]) ++out.vdp1OverFrameFrames;
        }
        if (V27(f) && f.Has(SE_PERF_VALID_M68K_IDLE)) idle.Add(f);
        if (V27(f) && f.Has(SE_PERF_VALID_BUS_X)) busx.Add(f);
        if (V27(f) && f.Has(SE_PERF_VALID_CACHE))
        {
            ++out.cache.covered;
            (f.cacheMode == SE_PERF_CACHE_ON ? cacheOn : cacheOff)++;
            out.cacheMode = f.cacheMode;
        }
        if (V27(f) && f.Has(SE_PERF_VALID_MODE))
        {
            ++out.mode.covered;
            if (f.mode & SE_LIVE_PROF_FUNCTIONS) ++out.modeFunctionsOn;
        }
        if (V27(f) && f.Has(SE_PERF_VALID_IDENTITY)) ++out.identity.covered;
    }
    out.cacheMixed = cacheOn && cacheOff;
    for (Coverage* c : { &out.slave, &out.vdp1Draw, &out.vdp1Work, &out.m68kIdle, &out.busX, &out.cache, &out.mode, &out.identity })
        c->frames = n;
    out.slave.covered = slave.covered;
    out.vdp1Draw.covered = draw.covered;
    out.vdp1Work.covered = work.covered;
    out.m68kIdle.covered = idle.covered;
    out.busX.covered = busx.covered;

    out.shares = ComputePerfShares(all.c, static_cast<double>(n ? n : 1));
    // The gated fields start from nothing and take only their own frames' figures.
    out.shares.cpu[1] = CpuShare();
    out.shares.vdp1Draw = out.shares.vdp1Work = out.shares.vdp1DmaDiscarded = 0.0;
    out.shares.vdp1WorkOverFrame = false;
    if (slave.covered) out.shares.cpu[1] = ComputePerfShares(slave.c, static_cast<double>(slave.covered)).cpu[1];
    if (draw.covered) out.shares.vdp1Draw = ComputePerfShares(draw.c, static_cast<double>(draw.covered)).vdp1Draw;
    if (work.covered)
    {
        const PerfShares w = ComputePerfShares(work.c, static_cast<double>(work.covered));
        out.shares.vdp1Work = w.vdp1Work;
        out.shares.vdp1DmaDiscarded = w.vdp1DmaDiscarded;
        out.shares.vdp1WorkOverFrame = w.vdp1WorkOverFrame || out.vdp1OverFrameFrames > 0;
    }
    if (idle.covered && idle.c[SE_PERF_M68K_TOTAL_CYCLES] > 0)
    {
        out.m68kIdleExecShare = Clamp01(static_cast<double>(idle.c[SE_PERF_M68K_CYCLES]) /
                                        static_cast<double>(idle.c[SE_PERF_M68K_TOTAL_CYCLES]));
        out.m68kIdleShare = Clamp01(static_cast<double>(idle.c[SE_PERF_M68K_IDLE_CYCLES]) /
                                    static_cast<double>(idle.c[SE_PERF_M68K_TOTAL_CYCLES]));
    }
    if (busx.covered) out.busXTotals = ComputeBusTotals(busx.c);
    return out;
}

bool ProfilerModel::LatestIdentity(uint32_t* timelineId, uint64_t* frameNo) const
{
    for (size_t k = 0; k < mCount; ++k)
    {
        const PerfFrame& f = At(mCount - 1 - k);
        if (V27(f) && f.Has(SE_PERF_VALID_IDENTITY))
        {
            if (timelineId) *timelineId = f.timelineId;
            if (frameNo) *frameNo = f.frameNo;
            return true;
        }
    }
    return false;
}

FunctionWindow ProfilerModel::Functions(size_t window, FnCpu cpu) const
{
    FunctionWindow out;
    const size_t n = std::min(window, mCount);
    out.frames = n;
    std::unordered_map<uint32_t, uint64_t> sum;
    bool busyKnown = true;
    for (size_t k = 0; k < n; ++k)
    {
        const PerfFrame& f = At(mCount - 1 - k);
        if (!f.functions) continue;
        ++out.attributed;
        const bool split = V27(f) && f.Has(SE_PERF_VALID_OTHER);
        if (cpu != FnCpu::Both && !split) continue;   // a v26 remainder has no CPU
        ++out.used;
        if (split) ++out.splitFrames;
        if (f.satFlags & SE_PERF_SAT_TABLE_FULL && split) ++out.tableFullFrames;
        if (f.satFlags & SE_PERF_SAT_TRUNCATED && split) ++out.truncatedFrames;
        const bool wantMaster = cpu != FnCpu::Slave, wantSlave = cpu != FnCpu::Master;
        for (const PerfFrame::Fn& fn : f.fns)
        {
            const bool slaveKey = (fn.key & SE_PERF_FN_SLAVE) != 0;
            if (slaveKey ? !wantSlave : !wantMaster) continue;
            sum[fn.key] += fn.cycles;
            out.listed += fn.cycles;
        }
        if (split)
        {
            for (int c = 0; c < 2; ++c)
            {
                if (c ? !wantSlave : !wantMaster) continue;
                out.otherNotAdmitted += f.otherNotAdmitted[c];
                out.otherEvicted += f.otherEvicted[c];
            }
        }
        else
        {
            out.otherUnsplit += f.fnOther;
        }
        // Busy time of the selected CPUs over this frame, from its own counters.
        if (wantMaster)
        {
            const uint64_t idle = f.c[SE_PERF_SH2_SLEEP] + f.c[SE_PERF_SH2_STALL];
            out.busy += f.c[SE_PERF_FRAME_CYCLES] > idle ? f.c[SE_PERF_FRAME_CYCLES] - idle : 0;
        }
        if (wantSlave)
        {
            if (SlaveKnown(f))
            {
                const uint64_t idle = f.c[SE_PERF_SH2_SLEEP + 1] + f.c[SE_PERF_SH2_STALL + 1];
                out.busy += f.c[SE_PERF_SSH2_ON_CYCLES] > idle ? f.c[SE_PERF_SSH2_ON_CYCLES] - idle : 0;
            }
            else busyKnown = false;
        }
    }
    out.busyKnown = busyKnown && out.used > 0;
    out.rows.reserve(sum.size());
    for (const auto& kv : sum)
    {
        PerfFunction pf;
        pf.key = kv.first;
        pf.cycles = kv.second;
        out.rows.push_back(pf);
    }
    std::sort(out.rows.begin(), out.rows.end(), [](const PerfFunction& a, const PerfFunction& b)
    {
        return a.cycles != b.cycles ? a.cycles > b.cycles : a.key < b.key;
    });
    return out;
}

// ---- MemoryPoolTracker ----

void MemoryPoolTracker::Reset()
{
    mPools.clear();
}

void MemoryPoolTracker::SetSource(uint64_t sourceId, uint64_t instance, uint32_t timelineId, uint64_t frame)
{
    // Scrubbing back shows an OLDER state than the previous sample: the difference is not a change
    // the machine made, so it re-primes like any other jump. A frame of 0 is "unknown" and never counts.
    const bool backwards = frame != 0 && mFrame != 0 && frame < mFrame;
    if (!mHaveSource || sourceId != mSourceId || instance != mInstance || timelineId != mTimeline || backwards)
        Reset();
    mHaveSource = true;
    mSourceId = sourceId;
    mInstance = instance;
    mTimeline = timelineId;
    mFrame = frame;
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
