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
        size_t   bytes = 0;               // compressed footprint of this frame
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
    bool Select(size_t i, se_data_source* out);

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
