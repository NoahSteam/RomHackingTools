#include "FrameRecorder.h"

#include <cstring>
#include <utility>

#include "SaturnRegions.h"

namespace sfe
{

namespace
{
// Read a whole VRAM region via the ABI into 'out' (sized to what the source
// returned; 0 = region absent for this source). Runs on the UI thread.
void ReadRegion(se_context* ctx, se_vram_kind kind, uint32_t maxSize,
                std::vector<uint8_t>& out)
{
    out.resize(maxSize);
    const size_t got = se_read_vram(ctx, kind, 0, out.data(), maxSize);
    out.resize(got);
}

// Compress a raw region into 'r' (worker thread), reusing the match-finder scratch.
void CompressRegion(const std::vector<uint8_t>& raw, FrameRecorder::Region& r,
                    FrameLzScratch& lz)
{
    r.rawSize = raw.size();
    if (raw.empty())
    {
        r.lz.clear();
        return;
    }
    FrameLzCompress(raw.data(), raw.size(), r.lz, lz);
}

void DecompressRegion(const FrameRecorder::Region& r, std::vector<uint8_t>& out)
{
    out.resize(r.rawSize);
    if (r.rawSize == 0)
    {
        return;
    }
    if (!FrameLzDecompress(r.lz.data(), r.lz.size(), out.data(), r.rawSize))
    {
        std::memset(out.data(), 0, out.size());   // corrupt blob -> blank, don't crash
    }
}

size_t CopyOut(const std::vector<uint8_t>& buf, uint32_t off, void* dst, size_t size)
{
    if (off >= buf.size())
    {
        return 0;
    }
    const size_t avail = buf.size() - off;
    const size_t n = size < avail ? size : avail;
    std::memcpy(dst, buf.data() + off, n);
    return n;
}
}  // namespace

FrameRecorder::FrameRecorder()
{
    mWorker = std::thread(&FrameRecorder::Worker, this);
}

FrameRecorder::~FrameRecorder()
{
    mStop.store(true);
    mQCv.notify_all();
    if (mWorker.joinable())
    {
        mWorker.join();
    }
}

void FrameRecorder::Configure(size_t maxFrames)
{
    std::lock_guard<std::mutex> lk(mRingMtx);
    mMaxFrames = maxFrames;
    Evict();
}

void FrameRecorder::Capture(se_context* ctx, uint64_t frameNumber)
{
    if (!ctx)
    {
        return;
    }
    // UI thread: only the raw reads happen here (fast memcpys); the worker does
    // the expensive compression. Registers are small, so read them here too.
    RawFrame raw;
    raw.frameNumber = frameNumber;
    ReadRegion(ctx, SE_VRAM_KIND_VDP1_VRAM, kVdp1VramSize, raw.vdp1Vram);
    ReadRegion(ctx, SE_VRAM_KIND_VDP2_VRAM, kVdp2VramSize, raw.vdp2Vram);
    ReadRegion(ctx, SE_VRAM_KIND_CRAM,      kCramSize,     raw.cram);
    ReadRegion(ctx, SE_VRAM_KIND_WRAM_LOW,  kWramSize,     raw.wramLow);
    ReadRegion(ctx, SE_VRAM_KIND_WRAM_HIGH, kWramSize,     raw.wramHigh);
    ReadRegion(ctx, SE_VRAM_KIND_VDP1_FB,   kVdp1FbSize,   raw.vdp1Fb);

    raw.vdp1Regs.resize(kVdp1RegBytes / 2);
    for (uint32_t o = 0; o < kVdp1RegBytes; o += 2)
    {
        raw.vdp1Regs[o / 2] = se_get_vdp1_register(ctx, o);
    }
    raw.vdp2Regs.resize(kVdp2RegBytes / 2);
    for (uint32_t o = 0; o < kVdp2RegBytes; o += 2)
    {
        raw.vdp2Regs[o / 2] = se_get_vdp2_register(ctx, o);
    }

    {
        std::lock_guard<std::mutex> lk(mQMtx);
        if (mQueue.size() >= kMaxQueued)
        {
            return;   // compressor is behind: drop this frame rather than stall
        }
        mQueue.push_back(std::move(raw));
    }
    mQCv.notify_one();
}

void FrameRecorder::Worker()
{
    for (;;)
    {
        RawFrame raw;
        {
            std::unique_lock<std::mutex> lk(mQMtx);
            mQCv.wait(lk, [this] { return mStop.load() || !mQueue.empty(); });
            if (mStop.load() && mQueue.empty())
            {
                return;
            }
            if (mQueue.empty())
            {
                continue;
            }
            raw = std::move(mQueue.front());
            mQueue.pop_front();
        }

        Frame f;
        f.frameNumber = raw.frameNumber;
        CompressRegion(raw.vdp1Vram, f.vdp1Vram, mLzScratch);
        CompressRegion(raw.vdp2Vram, f.vdp2Vram, mLzScratch);
        CompressRegion(raw.cram,     f.cram,     mLzScratch);
        CompressRegion(raw.wramLow,  f.wramLow,  mLzScratch);
        CompressRegion(raw.wramHigh, f.wramHigh, mLzScratch);
        CompressRegion(raw.vdp1Fb,   f.vdp1Fb,   mLzScratch);
        f.vdp1Regs = std::move(raw.vdp1Regs);
        f.vdp2Regs = std::move(raw.vdp2Regs);
        f.bytes = f.vdp1Vram.lz.size() + f.vdp2Vram.lz.size() + f.cram.lz.size() +
                  f.wramLow.lz.size() + f.wramHigh.lz.size() + f.vdp1Fb.lz.size() +
                  f.vdp1Regs.size() * 2 + f.vdp2Regs.size() * 2;

        std::lock_guard<std::mutex> lk(mRingMtx);
        mBytes += f.bytes;
        mFrames.push_back(std::move(f));
        Evict();
    }
}

void FrameRecorder::Evict()
{
    // Always keep at least the newest frame, even if a single frame exceeds the
    // byte budget — otherwise scrubbing would have nothing to show.
    while (mFrames.size() > 1 && (mBytes > kMaxBytes || mFrames.size() > mMaxFrames))
    {
        mBytes -= mFrames.front().bytes;
        mFrames.pop_front();
    }
}

void FrameRecorder::Clear()
{
    {
        std::lock_guard<std::mutex> lk(mQMtx);
        mQueue.clear();
    }
    std::lock_guard<std::mutex> lk(mRingMtx);
    mFrames.clear();
    mBytes = 0;
}

size_t FrameRecorder::Count() const
{
    std::lock_guard<std::mutex> lk(mRingMtx);
    return mFrames.size();
}

uint64_t FrameRecorder::FrameNumber(size_t i) const
{
    std::lock_guard<std::mutex> lk(mRingMtx);
    return i < mFrames.size() ? mFrames[i].frameNumber : 0;
}

size_t FrameRecorder::BytesUsed() const
{
    std::lock_guard<std::mutex> lk(mRingMtx);
    return mBytes;
}

bool FrameRecorder::Select(size_t i, se_data_source* out)
{
    if (!out)
    {
        return false;
    }
    {
        std::lock_guard<std::mutex> lk(mRingMtx);
        if (i >= mFrames.size())
        {
            return false;
        }
        const Frame& f = mFrames[i];
        DecompressRegion(f.vdp1Vram, mSelVdp1);
        DecompressRegion(f.vdp2Vram, mSelVdp2);
        DecompressRegion(f.cram, mSelCram);
        DecompressRegion(f.wramLow, mSelWramLow);
        DecompressRegion(f.wramHigh, mSelWramHigh);
        DecompressRegion(f.vdp1Fb, mSelVdp1Fb);
        mSelVdp1Regs = f.vdp1Regs;
        mSelVdp2Regs = f.vdp2Regs;
    }

    std::memset(out, 0, sizeof(*out));
    out->abi_version = SE_ABI_VERSION;
    out->capabilities = SE_CAP_VDP1_VRAM | SE_CAP_VDP2_VRAM | SE_CAP_CRAM |
                        SE_CAP_MAIN_RAM | SE_CAP_VDP1_REGS | SE_CAP_VDP2_REGS |
                        SE_CAP_VDP1_FB;
    out->user = this;
    out->read_vdp1_vram = CbVdp1;
    out->read_vdp2_vram = CbVdp2;
    out->read_cram      = CbCram;
    out->read_main_ram  = CbMain;
    out->read_vdp1_fb   = CbVdp1Fb;
    out->read_vdp1_reg  = CbVdp1Reg;
    out->read_vdp2_reg  = CbVdp2Reg;
    // No close callback: the scratch is owned by this recorder, not the context.
    return true;
}

size_t FrameRecorder::CbVdp1(void* u, uint32_t off, void* dst, size_t size)
{
    return CopyOut(static_cast<FrameRecorder*>(u)->mSelVdp1, off, dst, size);
}
size_t FrameRecorder::CbVdp2(void* u, uint32_t off, void* dst, size_t size)
{
    return CopyOut(static_cast<FrameRecorder*>(u)->mSelVdp2, off, dst, size);
}
size_t FrameRecorder::CbCram(void* u, uint32_t off, void* dst, size_t size)
{
    return CopyOut(static_cast<FrameRecorder*>(u)->mSelCram, off, dst, size);
}
size_t FrameRecorder::CbMain(void* u, uint32_t addr, void* dst, size_t size)
{
    FrameRecorder* r = static_cast<FrameRecorder*>(u);
    if (addr >= 0x06000000u)
    {
        return CopyOut(r->mSelWramHigh, addr - 0x06000000u, dst, size);
    }
    if (addr >= 0x00200000u)
    {
        return CopyOut(r->mSelWramLow, addr - 0x00200000u, dst, size);
    }
    return 0;
}
size_t FrameRecorder::CbVdp1Fb(void* u, uint32_t off, void* dst, size_t size)
{
    return CopyOut(static_cast<FrameRecorder*>(u)->mSelVdp1Fb, off, dst, size);
}
uint16_t FrameRecorder::CbVdp1Reg(void* u, uint32_t reg)
{
    const std::vector<uint16_t>& v = static_cast<FrameRecorder*>(u)->mSelVdp1Regs;
    const size_t i = reg >> 1;
    return i < v.size() ? v[i] : 0;
}
uint16_t FrameRecorder::CbVdp2Reg(void* u, uint32_t reg)
{
    const std::vector<uint16_t>& v = static_cast<FrameRecorder*>(u)->mSelVdp2Regs;
    const size_t i = reg >> 1;
    return i < v.size() ? v[i] : 0;
}

}  // namespace sfe
