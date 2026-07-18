#include "FrameRecorder.h"

#include <cstring>

#include "FrameLz.h"

namespace sfe
{

namespace
{
constexpr uint32_t kVdp1Vram = 0x80000;
constexpr uint32_t kVdp2Vram = 0x80000;
constexpr uint32_t kCram     = 0x1000;
constexpr uint32_t kWram     = 0x100000;
constexpr uint32_t kVdp1RegBytes = 0x18;    // TVMR..MODR image
constexpr uint32_t kVdp2RegBytes = 0x120;   // full VDP2 register file

// Read a VRAM region via the ABI, compress it into 'r'. Sizes to the bytes the
// source actually returned (0 = region absent for this source).
void CaptureRegion(se_context* ctx, se_vram_kind kind, uint32_t maxSize,
                   FrameRecorder::Region& r, std::vector<uint8_t>& scratch,
                   FrameLzScratch& lz)
{
    scratch.resize(maxSize);
    const size_t got = se_read_vram(ctx, kind, 0, scratch.data(), maxSize);
    r.rawSize = got;
    if (got == 0)
    {
        r.lz.clear();
        return;
    }
    FrameLzCompress(scratch.data(), got, r.lz, lz);
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
        std::memset(out.data(), 0, out.size());   // corrupt blob → blank, don't crash
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

void FrameRecorder::Configure(size_t maxFrames)
{
    mMaxFrames = maxFrames;
    Evict();
}

void FrameRecorder::Capture(se_context* ctx, uint64_t frameNumber)
{
    if (!ctx)
    {
        return;
    }
    if (mHaveLast && frameNumber == mLastFrame)
    {
        return;   // same emulated frame (e.g. paused): don't duplicate
    }
    mLastFrame = frameNumber;
    mHaveLast = true;

    Frame f;
    f.frameNumber = frameNumber;
    std::vector<uint8_t>& scratch = mReadScratch;   // reused across frames
    CaptureRegion(ctx, SE_VRAM_KIND_VDP1_VRAM, kVdp1Vram, f.vdp1Vram, scratch, mLzScratch);
    CaptureRegion(ctx, SE_VRAM_KIND_VDP2_VRAM, kVdp2Vram, f.vdp2Vram, scratch, mLzScratch);
    CaptureRegion(ctx, SE_VRAM_KIND_CRAM,      kCram,     f.cram,     scratch, mLzScratch);
    CaptureRegion(ctx, SE_VRAM_KIND_WRAM_LOW,  kWram,     f.wramLow,  scratch, mLzScratch);
    CaptureRegion(ctx, SE_VRAM_KIND_WRAM_HIGH, kWram,     f.wramHigh, scratch, mLzScratch);

    f.vdp1Regs.resize(kVdp1RegBytes / 2);
    for (uint32_t o = 0; o < kVdp1RegBytes; o += 2)
    {
        f.vdp1Regs[o / 2] = se_get_vdp1_register(ctx, o);
    }
    f.vdp2Regs.resize(kVdp2RegBytes / 2);
    for (uint32_t o = 0; o < kVdp2RegBytes; o += 2)
    {
        f.vdp2Regs[o / 2] = se_get_vdp2_register(ctx, o);
    }

    f.bytes = f.vdp1Vram.lz.size() + f.vdp2Vram.lz.size() + f.cram.lz.size() +
              f.wramLow.lz.size() + f.wramHigh.lz.size() +
              f.vdp1Regs.size() * 2 + f.vdp2Regs.size() * 2;

    mBytes += f.bytes;
    mFrames.push_back(std::move(f));
    Evict();
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
    mFrames.clear();
    mBytes = 0;
    mHaveLast = false;
}

bool FrameRecorder::Select(size_t i, se_data_source* out)
{
    if (i >= mFrames.size() || !out)
    {
        return false;
    }
    const Frame& f = mFrames[i];
    DecompressRegion(f.vdp1Vram, mSelVdp1);
    DecompressRegion(f.vdp2Vram, mSelVdp2);
    DecompressRegion(f.cram, mSelCram);
    DecompressRegion(f.wramLow, mSelWramLow);
    DecompressRegion(f.wramHigh, mSelWramHigh);
    mSelVdp1Regs = f.vdp1Regs;
    mSelVdp2Regs = f.vdp2Regs;

    std::memset(out, 0, sizeof(*out));
    out->abi_version = SE_ABI_VERSION;
    out->capabilities = SE_CAP_VDP1_VRAM | SE_CAP_VDP2_VRAM | SE_CAP_CRAM |
                        SE_CAP_MAIN_RAM | SE_CAP_VDP1_REGS | SE_CAP_VDP2_REGS;
    out->user = this;
    out->read_vdp1_vram = CbVdp1;
    out->read_vdp2_vram = CbVdp2;
    out->read_cram      = CbCram;
    out->read_main_ram  = CbMain;
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
