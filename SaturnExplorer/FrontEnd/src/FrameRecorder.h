// FrameRecorder — a rolling, in-memory recording of live Saturn memory so the
// user can pause and scrub back through recent frames. Capture() runs on the UI
// thread and only does the fast raw region reads, then hands the frame to a
// background worker that LZ-compresses it (FrameLz) and pushes it into a ring
// buffer bounded by a memory budget + frame count. Keeping compression off the UI
// thread is essential: compressing ~3 MB/frame inline stalls the live view.
// Select() rebuilds a se_data_source over a chosen frame so the core can re-render
// it exactly like a live snapshot. Native only (paired with live mode).
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

#include "saturnexplorer/SaturnExplorer.h"

#include "FrameLz.h"

namespace sfe
{

class FrameRecorder
{
public:
    FrameRecorder();
    ~FrameRecorder();

    FrameRecorder(const FrameRecorder&) = delete;
    FrameRecorder& operator=(const FrameRecorder&) = delete;

    // One captured region: its LZ blob plus the original (decompressed) length.
    struct Region
    {
        std::vector<uint8_t> lz;
        size_t               rawSize = 0;
    };
    struct Frame
    {
        uint64_t frameNumber = 0;
        Region   vdp1Vram, vdp2Vram, cram, wramLow, wramHigh, vdp1Fb, soundRam;
        std::vector<uint16_t> vdp1Regs;   // by hw offset >> 1
        std::vector<uint16_t> vdp2Regs;
        se_sh2_regs sh2[2] = {};          // [0] master, [1] slave (Assembly panel)
        bool        hasSh2[2] = { false, false };
        se_scsp_slot slots[SE_SCSP_SLOT_COUNT] = {};   // decoded voices (Sound panel)
        int          slotCount = 0;
        // Savestate rewind (v16): the delta/keyframe block for this frame, attached later (the
        // emulator's diff worker lags). A delta needs its keyframe frame (baseKeyframe) still
        // resident to reconstruct. Empty until attached.
        std::vector<uint8_t> state;
        uint8_t   stateKind = 0;          // SE_LIVE_STATE_KIND_*
        uint64_t  baseKeyframe = 0;
        uint32_t  stateFullLen = 0;       // decoded full-state size
        bool      hasState = false;
        size_t   bytes = 0;               // compressed footprint of this frame (incl. state blob)
    };

    // Cap the ring to at most 'maxFrames' frames. (A fixed internal byte ceiling
    // also guards against runaway memory; the frame cap is the effective limit.)
    void Configure(size_t maxFrames);

    // Read the context's current frame (raw) and queue it for background
    // compression. Fast: only the region reads happen on the calling (UI) thread.
    // If the compressor is behind, the frame is dropped rather than blocking.
    void Capture(se_context* ctx, uint64_t frameNumber);

    size_t   Count() const;
    uint64_t FrameNumber(size_t i) const;
    size_t   BytesUsed() const;

    // Build a data source over frame i. The decompressed regions live in this
    // recorder's scratch and stay valid until the next Select() call. Returns
    // false if i is out of range. The caller creates a context from *out.
    // When an edit sink is set (SetEditSink), the source is writable: edits made
    // against the scrubbed frame are forwarded to the sink as pending pokes.
    bool Select(size_t i, se_data_source* out);

    // --- Savestate rewind (v16) ---
    // Attach a received savestate block to the frame it belongs to (matched by number).
    // Lagging: the frame was captured earlier. No-op if that frame isn't resident.
    void AttachStateBlock(uint64_t frameNumber, uint8_t kind, uint64_t baseKeyframe,
                          uint32_t fullLen, const uint8_t* payload, size_t len);
    // Reconstruct the full emulator savestate for frame i into 'out' (keyframe, or keyframe +
    // delta). Returns false if frame i has no block yet, or its keyframe was evicted.
    bool ReconstructState(size_t i, std::vector<uint8_t>& out) const;
    // True if frame i can currently be resumed-from (its block + keyframe are resident).
    bool CanReconstruct(size_t i) const;
    // Drop every frame after index i (used on rewind: the future is re-simulated). Also
    // re-arms Capture() to accept the next frame after this one.
    void TruncateAfter(size_t i);
    // Route edits made against a scrubbed frame to a sink (App) as pending pokes: the sink cb
    // gets (user, isSound, addr/offset, bytes, len). Call once at setup; makes Select writable.
    void SetEditSink(void* user, void (*cb)(void* user, int isSound, uint32_t addr,
                                            const uint8_t* bytes, size_t len));

    void Clear();

private:
    // A raw (uncompressed) frame staged from the UI thread for the worker.
    struct RawFrame
    {
        uint64_t generation = 0;
        uint64_t frameNumber = 0;
        std::vector<uint8_t>  vdp1Vram, vdp2Vram, cram, wramLow, wramHigh, vdp1Fb, soundRam;
        std::vector<uint16_t> vdp1Regs, vdp2Regs;
        se_sh2_regs sh2[2] = {};
        bool        hasSh2[2] = { false, false };
        se_scsp_slot slots[SE_SCSP_SLOT_COUNT] = {};
        int          slotCount = 0;
    };

    void Worker();
    void Evict();   // caller holds mRingMtx

    // Highest frame number Capture() has accepted; skips stale/duplicate frames (esp. the
    // transient frame-0 window right after a rewind). UI-thread only.
    uint64_t mLastCaptured = 0;

    // Edit sink (App) for writes made against a scrubbed frame (SetEditSink). UI-thread only.
    void* mEditUser = nullptr;
    void (*mEditCb)(void*, int, uint32_t, const uint8_t*, size_t) = nullptr;
    static size_t CbEditMain(void* u, uint32_t address, const void* src, size_t size);
    static size_t CbEditSound(void* u, uint32_t offset, const void* src, size_t size);

    static constexpr size_t kMaxBytes = 4ull * 1024u * 1024u * 1024u;  // 4 GiB ceiling
    static constexpr size_t kMaxQueued = 4;   // staged raw frames before we drop

    // Compressed ring (shared: UI reads via Count/Select, worker appends/evicts).
    mutable std::mutex mRingMtx;
    std::deque<Frame>  mFrames;
    size_t             mBytes = 0;
    size_t             mMaxFrames = 5 * 60;   // ring length in frames (App sets this)

    // Raw staging queue (UI pushes, worker pops) + the worker thread.
    std::mutex               mQMtx;
    std::condition_variable  mQCv;
    std::deque<RawFrame>     mQueue;
    std::atomic<uint64_t>    mGeneration{0};
    std::atomic<bool>        mStop{false};
    std::thread              mWorker;
    FrameLzScratch           mLzScratch;   // worker-owned match-finder scratch

    // Scratch holding the currently-selected decompressed frame (UI thread only;
    // read by the data-source callbacks below). Outlives the created context.
    std::vector<uint8_t>  mSelVdp1, mSelVdp2, mSelCram, mSelWramLow, mSelWramHigh, mSelVdp1Fb;
    std::vector<uint8_t>  mSelSoundRam;
    std::vector<uint16_t> mSelVdp1Regs, mSelVdp2Regs;
    se_sh2_regs           mSelSh2[2] = {};
    bool                  mSelHasSh2[2] = { false, false };
    se_scsp_slot          mSelSlots[SE_SCSP_SLOT_COUNT] = {};
    int                   mSelSlotCount = 0;

    // Data-source callbacks (static; 'user' is this recorder).
    static size_t CbVdp1(void* u, uint32_t off, void* dst, size_t size);
    static size_t CbVdp2(void* u, uint32_t off, void* dst, size_t size);
    static size_t CbCram(void* u, uint32_t off, void* dst, size_t size);
    static size_t CbMain(void* u, uint32_t addr, void* dst, size_t size);
    static size_t CbVdp1Fb(void* u, uint32_t off, void* dst, size_t size);
    static size_t CbSoundRam(void* u, uint32_t off, void* dst, size_t size);
    static uint16_t CbVdp1Reg(void* u, uint32_t reg);
    static uint16_t CbVdp2Reg(void* u, uint32_t reg);
    static int      CbSh2Regs(void* u, int cpu, se_sh2_regs* out);
    static int      CbScspSlots(void* u, se_scsp_slot out[SE_SCSP_SLOT_COUNT]);
};

}  // namespace sfe
