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
    // ---- v27 (Step 0). 'valid' is SE_PERF_VALID_*: a field whose bit is clear is "not available",
    // not a measured zero. wireVersion 0 (a hand-built frame) is treated like a v26 record.
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
    // Lost sampleSeq ranges just before this record (0, 0 = none), kept apart because they mean
    // different things: gap* is what the server's ring dropped for this connection (the client
    // never had it), evict* is what the driver's bounded queue evicted because the application
    // did not drain it in time (the client had it and let it go).
    uint64_t gapFirst = 0, gapLast = 0;
    uint64_t evictFirst = 0, evictLast = 0;
    // The spans above only say where to break the plot; they merge separate losses into their
    // enclosing interval, which can contain delivered records. How many frames are missing is
    // these exact counts, by cause, never derived from a span.
    uint64_t gapCount = 0, evictCount = 0;
    // Not a frame: a reply that carried only a lost range and/or a newer newest sequence. It
    // never enters the history; its ranges attach to the next frame pushed (and are counted at once).
    bool     gapOnly = false;
    uint64_t newestSeq = 0;    // the server's newest sampleSeq when this was sent (0 = unknown)
    bool HasGap() const { return gapLast != 0 || evictLast != 0 || gapCount != 0 || evictCount != 0; }
    bool Has(uint32_t bit) const { return (valid & bit) != 0; }
    // True for a v26 server's record: counters[6] is the old elapsed VDP1 figure.
    bool LegacyVdp1() const { return wireVersion < 27; }
    // Counter 5 means something: a v26 record's all-or-nothing figure, or a v27 record's integrated
    // one (SE_PERF_VALID_SSH2_INT). A v27 record without the bit has no slave on-time.
    bool SlaveOnTimeKnown() const { return LegacyVdp1() || Has(SE_PERF_VALID_SSH2_INT); }
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

// ---- Windowed figures ----
// A record carries a v27-only quantity only when its 'valid' bit says so, and a window averages
// SEVERAL records. A quantity is therefore summed over exactly the frames that carry it, against
// the matching frame cycles of those frames -- never against the whole window, which would read
// "no measurement" as a measured zero -- and the coverage says how many frames that was.
struct Coverage
{
    size_t covered = 0;   // frames in the window that carry the quantity
    size_t frames = 0;    // frames in the window
    bool Any() const { return covered > 0; }
    bool Full() const { return frames > 0 && covered == frames; }
};

// Everything the Frame and Buses tabs show for one averaging window. 'shares' holds, for every
// gated quantity, the value over its OWN covered frames (zero when the coverage is empty: read the
// coverage first); the ungated ones (frame length, the master SH-2, the SCU DSP, the 68000 executed
// share, VDP1 command / draw / overrun counts) are over every frame in the window.
struct WindowShares
{
    size_t frames = 0;
    size_t v27Frames = 0;               // frames from a v27 server (the rest are v26 or hand-built)
    PerfShares shares;
    // v26 servers' records and v27 records with SE_PERF_VALID_SSH2_INT: shares.cpu[1]. (A v26
    // record's figure is all-or-nothing per frame; it is still its producer's measurement.)
    Coverage slave;
    // The two VDP1 figures never mix: elapsed drawing exists only in v26 records, command work only
    // in v27 records carrying SE_PERF_VALID_VDP1_WORK. shares.vdp1Draw / vdp1Work /
    // vdp1DmaDiscarded / vdp1WorkOverFrame.
    Coverage vdp1Draw;
    Coverage vdp1Work;
    size_t   vdp1OverFrameFrames = 0;   // covered frames whose own work exceeded the frame (instant draw)
    // 68000 stopped, as a share of its own clock (SE_PERF_VALID_M68K_IDLE).
    Coverage m68kIdle;
    double   m68kIdleShare = 0;
    // The executing share of THE SAME frames (those carrying the idle bit), so idle + executing is one
    // subset of the 68000 clock. shares.m68k is executing over every frame in the window.
    double   m68kIdleExecShare = 0;
    // SE_PERF_VALID_BUS_X: dummy / unmapped request counts, SUMMED over the covered frames.
    Coverage busX;
    BusTotals busXTotals;
    // SE_PERF_VALID_CACHE: the cache mode of the covered frames; cacheMixed when they disagree.
    Coverage cache;
    uint32_t cacheMode = 0;
    bool     cacheMixed = false;
    // SE_PERF_VALID_MODE: how many covered frames ran with function attribution on.
    Coverage mode;
    size_t   modeFunctionsOn = 0;
    // SE_PERF_VALID_IDENTITY.
    Coverage identity;
};

// Which SH-2 the Functions tab lists.
enum class FnCpu { Both = 0, Master = 1, Slave = 2 };

// What the Functions tab shows for one window and one CPU filter. Shares divide by Attributed():
// the listed rows plus Other, so a row's percentage does not grow because rows were dropped.
struct FunctionWindow
{
    size_t frames = 0;                  // frames in the window
    size_t attributed = 0;              // frames of the window that ran with function attribution
    size_t used = 0;                    // frames that contributed (attribution on; for a CPU filter,
                                        // also carrying the per-CPU Other split)
    size_t splitFrames = 0;             // of those, frames with SE_PERF_VALID_OTHER
    std::vector<PerfFunction> rows;     // the retained keys of the filter, hottest first
    uint64_t listed = 0;                // sum of rows
    uint64_t otherNotAdmitted = 0;      // the per-frame table refused the key (3072 admitted)
    uint64_t otherEvicted = 0;          // admitted, but below the top 64 of its frame
    uint64_t otherUnsplit = 0;          // Both only: a frame without the split reports one remainder
    size_t   tableFullFrames = 0;       // frames that hit SE_PERF_SAT_TABLE_FULL
    size_t   truncatedFrames = 0;       // frames that hit SE_PERF_SAT_TRUNCATED
    // Cycles the filter's CPUs were busy for, from the counters of the used frames (frame - sleep
    // - stall; the slave from its on-time). Unknown when a used frame has no slave on-time.
    bool     busyKnown = false;
    uint64_t busy = 0;
    uint64_t Other() const { return otherNotAdmitted + otherEvicted + otherUnsplit; }
    uint64_t Attributed() const { return listed + Other(); }
    // Busy time no sample accounted for (a baseline lost to a reset, the first interval after
    // attribution began): kept apart from Other, which IS measured cycles. 0 when unknown.
    uint64_t Unobserved() const { return busyKnown && busy > Attributed() ? busy - Attributed() : 0; }
};

// One point of a history strip: a value, or "no data". A strip never draws "no data" as 0.
struct HistoryPoint
{
    bool   available = false;
    double value = 0;
    bool   breakBefore = false;   // records are missing just before this point
};
struct HistoryRun { size_t first = 0; size_t count = 0; };
// The runs a polyline may join: consecutive available points with no break between them.
std::vector<HistoryRun> SplitHistoryRuns(const std::vector<HistoryPoint>& points);

// "Frame 1234 (timeline 2)" from a record that carries its identity (SE_PERF_VALID_IDENTITY),
// otherwise a statement that it has none; never the snapshot number a batch was delivered with.
std::string PerfFrameLabel(const PerfFrame& frame);

// ---- History ----
// Records the App's profiler keeps: one per frame of the longest scrub history (30 s at 60 fps), so
// a scrubbed frame still has its record. The model's own default stays ten seconds.
constexpr size_t kProfilerScrubCapacity = 30 * 60;

class ProfilerModel
{
public:
    explicit ProfilerModel(size_t capacity = 600);   // ten seconds at 60 fps

    void Clear();
    // Add a frame. Ordering is by the 64-bit sampleSeq when the frame has one, else by the 32-bit
    // seq compared wrap-aware (a wrap is not a restart). A frame that is the newest again is
    // ignored (the same numbers delivered twice); one that goes BACKWARDS means a new emulator
    // run, and starts over. A gap-only entry (PerfFrame::gapOnly) adds no frame: it records its
    // lost ranges, to be attached to the next frame, and the newest sequence it reports.
    void Push(PerfFrame frame);

    // ---- Lost records. The plot breaks wherever a frame HasGap(); an averaging window reports
    // how many frames it is missing, so its denominator is the frames it really holds.
    // Totals are since the last Clear().
    // A span to break the plot at plus the exact number of frames missing inside it (the span may
    // enclose delivered records, so Count is stored, not derived).
    struct SeqRange
    {
        uint64_t first = 0, last = 0;
        uint64_t count = 0;
        bool     Empty() const { return last == 0 && count == 0; }
        uint64_t Count() const { return count; }
    };
    uint64_t TotalServerLost() const { return mServerLost; }     // dropped by the server's ring
    uint64_t TotalDriverEvicted() const { return mDriverEvicted; }   // evicted from the driver's queue
    // A range reported by a gap-only entry and not yet attached to a frame.
    SeqRange PendingServerGap() const { return mPendServer; }
    SeqRange PendingDriverEviction() const { return mPendEvict; }
    // The newest sampleSeq the server reported (via a frame or a gap-only entry); 0 = unknown.
    uint64_t NewestSeq() const { return mNewestSeq; }
    // Frames missing from the newest min(window, Count()) frames' span, by cause: the gaps carried
    // by every one of them except the oldest (its gap precedes the window).
    void LostIn(size_t window, uint64_t* serverLost, uint64_t* driverEvicted) const;
    // The same for the window of min(window, end) frames that ENDS at At(end - 1): 'end' is a count
    // from the oldest retained frame (anchor index + 1), clamped to Count(). The overloads below
    // taking 'end' anchor their window the same way; the ones without it end at the newest frame.
    void LostIn(size_t window, size_t end, uint64_t* serverLost, uint64_t* driverEvicted) const;

    size_t Count() const { return mCount; }
    size_t Capacity() const { return mFrames.size(); }
    bool   Empty() const { return mCount == 0; }
    // i = 0 is the oldest retained frame, Count()-1 the newest.
    const PerfFrame& At(size_t i) const;
    const PerfFrame& Latest() const { return At(mCount - 1); }

    // Counters summed over the newest min(window, Count()) frames; returns how many were summed.
    size_t Sum(size_t window, uint64_t out[SE_PERF_COUNT]) const;
    size_t Sum(size_t window, size_t end, uint64_t out[SE_PERF_COUNT]) const;

    // Functions over the same window, hottest first, and the cycles attributed in total
    // (including what fell outside the per-frame lists). Frames without attribution are skipped;
    // 'framesUsed' is how many contributed.
    std::vector<PerfFunction> HotFunctions(size_t window, uint64_t* totalCycles,
                                           size_t* framesUsed) const;

    // The figures of the newest min(window, Count()) frames, each over the frames that carry it.
    WindowShares Shares(size_t window) const;
    WindowShares Shares(size_t window, size_t end) const;
    // The Functions tab's view: rows, Other and the denominators for one CPU filter (see
    // FunctionWindow). A CPU filter uses only frames that carry the per-CPU Other split, because a
    // v26 remainder cannot be assigned to a CPU; "Both" uses every attributed frame.
    FunctionWindow Functions(size_t window, FnCpu cpu) const;
    FunctionWindow Functions(size_t window, size_t end, FnCpu cpu) const;
    // The identity of the newest frame that has one (false if none): what the memory tab keys on.
    bool LatestIdentity(uint32_t* timelineId, uint64_t* frameNo) const;
    // The retained record whose identity is EXACTLY (timelineId, frameNo): never a position, never a
    // frame number alone (the same number on another timeline is a different frame), and never a
    // record without SE_PERF_VALID_IDENTITY. On success *index is its At() index.
    bool FindIdentity(uint32_t timelineId, uint64_t frameNo, size_t* index) const;

private:
    std::vector<PerfFrame> mFrames;   // ring
    size_t mHead = 0;                 // index of the oldest
    size_t mCount = 0;
    uint64_t mServerLost = 0, mDriverEvicted = 0, mNewestSeq = 0;
    SeqRange mPendServer, mPendEvict;
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

// Where one Memory-tab sample's bytes come from, and what is known about the machine-state
// timeline they were captured on. The bytes of a live capture and the profiler's perf stream reach
// the client by different routes (the perf records are drained from their own queue; the capture
// is a ring slot a lagging GET may still be serving), so the stream's newest timeline is NOT the
// capture's: after a state load the stream can already be on the new timeline while the capture is
// still an old one.
struct PoolCaptureSource
{
    uint64_t sourceId = 0;        // the displayed capture (ContextBackend::SourceId)
    uint64_t instance = 0;        // the emulator process
    uint64_t frame = 0;           // displayed frame number of a scrub, 0 = unknown
    bool     live = false;        // the bytes are the live emulator's newest capture (not a file or a scrub)
    uint32_t captureTimeline = 0; // timeline the SERVER stamped on the capture's bytes, 0 = not stated
    uint64_t captureRevision = 0; // content revision of the capture (moves when a different snapshot is shown)
    uint32_t streamTimeline = 0;  // newest perf record's timeline (a different route: only a fallback)
};

class MemoryPoolTracker
{
public:
    // A live capture whose timeline the server does not state (an emulator older than the field)
    // may be a slot of the timeline a load abandoned: the server's ring holds this many slots, so
    // after a timeline change seen on the perf stream, the capture is trustworthy only once
    // kRingDepth + 1 DISTINCT captures have followed -- a ring of kRingDepth slots cannot hold more
    // old ones than that. Until then nothing is sampled (and nothing is guessed).
    static constexpr uint32_t kUnverifiedSettleCaptures = 5;

    // Blocks per pool are capped so every pool draws as a map of comparable resolution.
    static constexpr uint32_t kMaxBlocks = 1024;
    static constexpr uint32_t kMinBlockSize = 16;

    // Start (or restart) tracking: forget every pool and every previous sample.
    void Reset();
    // What the samples that follow are samples OF. The tracker compares each sample with the
    // previous one, which means something only when both came from the same place; when any of
    // these differs from the previous call, or the displayed frame went BACKWARDS (scrubbing back),
    // it re-primes: forgets every pool, so the first sample afterwards reports no changed blocks.
    // sourceId: the displayed capture (a different file, savestate or scrubbed frame is another);
    // instance: the emulator process; timelineId: the machine-state timeline (rewind, load);
    // frame: the displayed frame number, 0 = unknown.
    void SetSource(uint64_t sourceId, uint64_t instance, uint32_t timelineId, uint64_t frame);
    // The live-capture-aware form of SetSource. Re-primes on a change of the CAPTURE's own timeline
    // when the server states one, so the tracker never compares bytes of one timeline with bytes of
    // another and never files a capture under an identity that is not its own. When the capture does
    // not state its timeline it falls back to the perf stream's, and a change of that timeline
    // re-primes and then withholds sampling until the capture is provably past it (see
    // kUnverifiedSettleCaptures). Returns whether the caller may Sample now.
    bool BeginSample(const PoolCaptureSource& source);
    // Captures still to wait out after a timeline change the capture could not confirm (0 = sampling).
    uint32_t SettlingCaptures() const { return mSettleLeft; }
    // How many times the tracker has started over: the Memory tab's "run" of consecutive samples,
    // the counterpart of the Frame tab's history runs (SplitHistoryRuns). Moves on every re-prime.
    uint32_t Run() const { return mRun; }
    // The timeline the samples since the last re-prime are filed under.
    uint32_t Timeline() const { return mTimeline; }
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
    bool     mHaveSource = false;
    uint64_t mSourceId = 0, mInstance = 0, mFrame = 0;
    uint32_t mTimeline = 0;
    uint32_t mRun = 0;
    uint32_t mSettleLeft = 0;       // distinct captures to wait for after an unconfirmed timeline change
    uint64_t mLastRevision = 0;     // the capture last counted toward mSettleLeft
};

}  // namespace sfe
