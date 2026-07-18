// FrameRecorder — a rolling, in-memory recording of the live Saturn memory
// state so the user can pause and scrub back through recent frames. Each frame's
// regions (VDP1/VDP2 VRAM, CRAM, work RAM, register files) are read out through
// the host ABI, LZ-compressed (FrameLz), and pushed into a ring buffer bounded by
// a memory budget and a wall-time window; the oldest frames drop out as new ones
// arrive. Select() rebuilds a se_data_source over a chosen frame so the core can
// re-render it exactly like a live snapshot. Native only (paired with live mode).
#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <vector>

#include "saturnexplorer/SaturnExplorer.h"

#include "FrameLz.h"

namespace sfe
{

class FrameRecorder
{
public:
    // One captured region: its LZ blob plus the original (decompressed) length.
    struct Region
    {
        std::vector<uint8_t> lz;
        size_t               rawSize = 0;
    };
    struct Frame
    {
        uint64_t frameNumber = 0;
        Region   vdp1Vram, vdp2Vram, cram, wramLow, wramHigh;
        std::vector<uint16_t> vdp1Regs;   // by hw offset >> 1
        std::vector<uint16_t> vdp2Regs;
        size_t   bytes = 0;               // compressed footprint of this frame
    };

    // Cap the ring to at most 'maxFrames' frames. (A fixed internal byte ceiling
    // also guards against runaway memory; the frame cap is the effective limit.)
    void Configure(size_t maxFrames);

    // Capture the context's current frame. 'frameNumber' dedups (a repeat frame
    // number — e.g. while paused — is skipped). Reads regions via se_read_vram /
    // se_get_*_register, so it works for any live source.
    void Capture(se_context* ctx, uint64_t frameNumber);

    size_t   Count() const { return mFrames.size(); }
    uint64_t FrameNumber(size_t i) const { return mFrames[i].frameNumber; }
    size_t   BytesUsed() const { return mBytes; }

    // Build a data source over frame i. The decompressed regions live in this
    // recorder's scratch and stay valid until the next Select() call. Returns
    // false if i is out of range. The caller creates a context from *out.
    bool Select(size_t i, se_data_source* out);

    void Clear();

private:
    void Evict();

    static constexpr size_t kMaxBytes = 4ull * 1024u * 1024u * 1024u;  // 4 GiB ceiling

    std::deque<Frame> mFrames;
    size_t            mBytes = 0;
    size_t            mMaxFrames = 5 * 60;   // ring length in frames (App sets this)
    uint64_t          mLastFrame = ~0ull;
    bool              mHaveLast = false;

    // Reused across frames so the per-frame capture path allocates nothing steady
    // state: the VRAM read buffer and the LZ match-finder's hash chains.
    std::vector<uint8_t> mReadScratch;
    FrameLzScratch       mLzScratch;

    // Scratch holding the currently-selected decompressed frame (for the data
    // source callbacks below). Owned here so it outlives the created context.
    std::vector<uint8_t>  mSelVdp1, mSelVdp2, mSelCram, mSelWramLow, mSelWramHigh;
    std::vector<uint16_t> mSelVdp1Regs, mSelVdp2Regs;

    // Data-source callbacks (static; 'user' is this recorder).
    static size_t CbVdp1(void* u, uint32_t off, void* dst, size_t size);
    static size_t CbVdp2(void* u, uint32_t off, void* dst, size_t size);
    static size_t CbCram(void* u, uint32_t off, void* dst, size_t size);
    static size_t CbMain(void* u, uint32_t addr, void* dst, size_t size);
    static uint16_t CbVdp1Reg(void* u, uint32_t reg);
    static uint16_t CbVdp2Reg(void* u, uint32_t reg);
};

}  // namespace sfe
