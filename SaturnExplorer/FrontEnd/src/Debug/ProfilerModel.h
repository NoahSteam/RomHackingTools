// Profiler model: the emulator's per-frame hardware counters (SePerfCounters.h, delivered by
// the live driver) turned into the figures the Profiler panel shows -- where each frame's time
// went, what crossed the buses, and which functions the SH-2s were in -- plus the occupancy of
// the Saturn's fixed memory pools, computed from the captured memory itself.
//
// UI-free and driver-free on purpose: it takes plain counter arrays, so the arithmetic (which is
// where a profiler is wrong without looking wrong) is unit-tested with no emulator and no ImGui.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "SePerfCounters.h"

namespace sfe
{

// One completed emulated frame, as the emulator counted it.
struct PerfFrame
{
    uint32_t seq = 0;          // the emulator's own frame count (see se_live_perf_frame)
    uint32_t frame = 0;        // snapshot frame number it arrived with
    // ---- v27 (Step 0). 'valid' is SE_PERF_VALID_* | SE_LIVE_PERF_V26_VDP1_ELAPSED (0x80000000):
    // a field whose bit is clear is "not available", not a measured zero. wireVersion 0 (a
    // hand-built frame) is treated like a v26 record.
    uint32_t wireVersion = 0;
    uint32_t valid = 0;
    uint64_t sampleSeq = 0;
    uint32_t timelineId = 0;   // SE_PERF_VALID_IDENTITY
    uint64_t frameNo = 0;      // SE_PERF_VALID_IDENTITY
    uint32_t mode = 0;         // SE_PERF_VALID_MODE
    uint32_t cacheMode = 0;    // SE_PERF_VALID_CACHE
    uint32_t satFlags = 0;     // SE_PERF_VALID_OTHER
    uint64_t otherNotAdmitted[2] = {};   // SE_PERF_VALID_OTHER
    uint64_t otherEvicted[2] = {};       // SE_PERF_VALID_OTHER
    uint64_t gapFirst = 0, gapLast = 0;  // lost sampleSeq range before this record (0, 0 = none)
    bool Has(uint32_t bit) const { return (valid & bit) != 0; }
    // True for a v26 server's record: counters[6] is the old elapsed VDP1 figure.
    bool LegacyVdp1() const { return wireVersion < 27; }
    bool     functions = false;   // function attribution was on for this frame
    uint64_t c[SE_PERF_COUNT] = {};
    struct Fn { uint32_t key; uint32_t cycles; };
    std::vector<Fn> fns;       // hottest first
    uint32_t fnOther = 0;      // attributed cycles outside 'fns'
};

// Where one SH-2's share of a frame went, as fractions of the frame that sum to 1.
struct CpuShare
{
    double busy = 0;    // executing instructions (including waiting on slow bus cycles)
    double sleep = 0;   // in SLEEP / standby, waiting for an interrupt
    double stall = 0;   // held off the bus by a DMA burst or an external halt
    double off = 0;     // switched off (the slave, until the game starts it)
};

struct PerfShares
{
    double   frameCycles = 0;     // SH-2 clock cycles in the frame; 0 = no data
    CpuShare cpu[2];              // master, slave
    double   vdp1Draw = 0;        // v26 servers only: fraction of the frame VDP1 was drawing (elapsed)
    double   vdp1Work = 0;        // v27: fraction of the frame in VDP1 command work (needs SE_PERF_VALID_VDP1_WORK).
                                  // NOT clamped: instant-draw mode does a whole table's work in no time, so it can pass 1.
    bool     vdp1WorkOverFrame = false;   // vdp1Work > 1: more command work than the frame held (instant draw)
    double   vdp1DmaDiscarded = 0;        // v27: draw budget a DMA discarded, as a fraction of the frame (budget, not time)
    double   dsp = 0;             // fraction of the frame the SCU DSP was executing
    double   m68k = 0;            // fraction of ITS OWN clock the 68000 was executing
    double   vdp1DrawsPerFrame = 0;
    double   vdp1Commands = 0;
    double   vdp1Overruns = 0;
};

// Derive the shares of one frame (or of an average: the counters are linear, so the shares of
// summed counters are the cycle-weighted mean of the frames' shares). Every fraction is clamped
// to [0, 1] -- the counters are sampled at slightly different instants of the frame boundary, so
// a sleeping CPU can be credited a few cycles more than the frame held.
PerfShares ComputePerfShares(const uint64_t counters[SE_PERF_COUNT], double frames = 1.0);

// ---- Buses ----
enum class PerfBus { Cpu, A, B };

const char* PerfDeviceName(int device);       // "High work RAM", ...
PerfBus     PerfDeviceBus(int device);
const char* PerfBusName(PerfBus bus);         // "CPU bus", "A-bus", "B-bus"
// Name of an SE_PERF_XDEV_* bucket (F8). The unit of every bus count is a request to the SH-2's
// bus state controller (a split 32-bit CS0 access is one request, a cache line fill is four), so
// the count depends on the cache emulation mode (SE_PERF_CACHE_*).
const char* PerfXDeviceName(int xdev);
const char* PerfBusUnitLabel();               // "bus requests"
const char* PerfCacheModeName(uint32_t cacheMode);   // "instruction cache emulated", ...
// Name of an SCU DMA bus index (SE_PERF_BUS_*), which orders the buses differently.
const char* PerfDmaBusName(int dmaBus);

struct BusTotals
{
    uint64_t reads[3] = {};      // indexed by PerfBus
    uint64_t writes[3] = {};
    uint64_t dmaIn[3] = {};      // SCU DMA bytes written to the bus
    uint64_t dmaOut[3] = {};     // SCU DMA bytes read from the bus
    // v27 (F8): requests that belong to no device -- the A-bus dummy area and the holes the SCU does not
    // decode. Kept out of the three bus totals above.
    uint64_t dummyReads = 0, dummyWrites = 0, unmappedReads = 0, unmappedWrites = 0;
};
BusTotals ComputeBusTotals(const uint64_t counters[SE_PERF_COUNT]);

// ---- Functions ----
struct PerfFunction
{
    uint32_t key = 0;        // SE_PERF_FN_* packed key
    uint64_t cycles = 0;     // summed over the window
    uint32_t Address() const { return PerfFunctionAddress(key); }
    bool     Slave() const   { return (key & SE_PERF_FN_SLAVE) != 0; }
    bool     RawPc() const   { return (key & SE_PERF_FN_RAW_PC) != 0; }
    // The SH-2 address the key stands for. The key keeps only the 27 bus bits; code runs from
    // work RAM, so the cached mirror (top bits clear) is the address the rest of the tool uses.
    static uint32_t PerfFunctionAddress(uint32_t key) { return key & SE_PERF_FN_ADDR_MASK; }
};

// ---- History ----
class ProfilerModel
{
public:
    explicit ProfilerModel(size_t capacity = 600);   // ten seconds at 60 fps

    void Clear();
    // Add a frame. A frame whose seq is the one already newest is ignored (the same numbers
    // delivered twice); a seq that goes BACKWARDS means a new emulator run, and starts over.
    void Push(PerfFrame frame);

    size_t Count() const { return mCount; }
    size_t Capacity() const { return mFrames.size(); }
    bool   Empty() const { return mCount == 0; }
    // i = 0 is the oldest retained frame, Count()-1 the newest.
    const PerfFrame& At(size_t i) const;
    const PerfFrame& Latest() const { return At(mCount - 1); }

    // Counters summed over the newest min(window, Count()) frames; returns how many were summed.
    size_t Sum(size_t window, uint64_t out[SE_PERF_COUNT]) const;

    // Functions over the same window, hottest first, and the cycles attributed in total
    // (including what fell outside the per-frame lists). Frames without attribution are skipped;
    // 'framesUsed' is how many contributed.
    std::vector<PerfFunction> HotFunctions(size_t window, uint64_t* totalCycles,
                                           size_t* framesUsed) const;

private:
    std::vector<PerfFrame> mFrames;   // ring
    size_t mHead = 0;                 // index of the oldest
    size_t mCount = 0;
};

// ---- Memory pools ----
// The Saturn has no allocator the hardware knows about: its "pools" are the fixed RAMs (two work
// RAMs, the two VRAMs, colour RAM, sound RAM), and what a game does inside them is its own
// affair. What CAN be read off a capture is, per block of a pool, whether it holds anything (a
// block that is one byte value repeated -- cleared, or never touched -- holds nothing) and
// whether it changed since the previous look. That answers "how full is VRAM" and "who is still
// being written every frame" without knowing any game's heap layout.
struct PoolUsage
{
    std::string name;
    uint32_t base = 0;           // CPU-visible address of the pool
    uint32_t size = 0;           // bytes
    uint32_t blockSize = 0;      // bytes per block
    bool     valid = false;      // the capture had this region
    uint32_t usedBlocks = 0;     // blocks holding data
    uint32_t changedBlocks = 0;  // blocks that differed from the previous sample
    // Per block: 0 = empty, otherwise 1 + recency of the last change (255 = changed in this
    // sample, decaying to 1 = holding data, unchanged for a while).
    std::vector<uint8_t> cells;
    double UsedFraction() const    { return cells.empty() ? 0.0 : (double)usedBlocks / cells.size(); }
    double ChangedFraction() const { return cells.empty() ? 0.0 : (double)changedBlocks / cells.size(); }
};

class MemoryPoolTracker
{
public:
    // Blocks per pool are capped so every pool draws as a map of comparable resolution.
    static constexpr uint32_t kMaxBlocks = 1024;
    static constexpr uint32_t kMinBlockSize = 16;

    // Start (or restart) tracking: forget every pool and every previous sample.
    void Reset();
    // Feed one pool's current bytes. 'index' is the caller's stable slot for the pool. The first
    // sample of a pool has nothing to compare with, so it reports no changed blocks.
    void Sample(size_t index, const char* name, uint32_t base, const uint8_t* data, uint32_t size);
    // The pool was not in this capture.
    void MarkMissing(size_t index, const char* name, uint32_t base, uint32_t size);

    size_t Count() const { return mPools.size(); }
    const PoolUsage& Pool(size_t index) const { return mPools[index].usage; }

private:
    struct Tracked
    {
        PoolUsage usage;
        std::vector<uint32_t> hashes;   // previous sample's per-block hash
        bool primed = false;
    };
    std::vector<Tracked> mPools;
    Tracked& Slot(size_t index);
};

}  // namespace sfe
