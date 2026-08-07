#include "FrameRecorder.h"

#include <cstring>
#include <utility>

#include "SaturnRegions.h"
#include "SeLiveProtocol.h"   // SE_LIVE_STATE_KIND_* for savestate reconstruction
#include "SeStateCodec.h"     // XOR + RLE codec (shared with the emulator exporter)

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
    // Skip frame 0 and any non-advancing frame number. This filters the transient frame-0
    // window right after a rewind (the server briefly has no fresh frame) and any stale
    // frame the emulator re-serves, keeping the ring strictly monotonic. UI-thread only.
    if (frameNumber == 0 || frameNumber <= mLastCaptured)
    {
        return;
    }
    mLastCaptured = frameNumber;
    // UI thread: only the raw reads happen here (fast memcpys); the worker does
    // the expensive compression. Registers are small, so read them here too.
    RawFrame raw;
    raw.generation = mGeneration.load(std::memory_order_acquire);
    raw.frameNumber = frameNumber;
    ReadRegion(ctx, SE_VRAM_KIND_VDP1_VRAM, kVdp1VramSize, raw.vdp1Vram);
    ReadRegion(ctx, SE_VRAM_KIND_VDP2_VRAM, kVdp2VramSize, raw.vdp2Vram);
    ReadRegion(ctx, SE_VRAM_KIND_CRAM,      kCramSize,     raw.cram);
    ReadRegion(ctx, SE_VRAM_KIND_WRAM_LOW,  kWramSize,     raw.wramLow);
    ReadRegion(ctx, SE_VRAM_KIND_WRAM_HIGH, kWramSize,     raw.wramHigh);
    ReadRegion(ctx, SE_VRAM_KIND_VDP1_FB,   kVdp1FbSize,   raw.vdp1Fb);
    ReadRegion(ctx, SE_VRAM_KIND_SOUND_RAM, kSoundRamSize, raw.soundRam);   // 68000 code + samples

    // Decoded SCSP voices, so the Sound panel + per-voice Play/Export keep working while
    // scrubbing (0 when the source has no v14 sound tap).
    raw.slotCount = se_get_scsp_slots(ctx, raw.slots);

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

    // SH-2 registers, so the Assembly panel (and status-bar PC) keep working while
    // paused/scrubbing — reading a recorded frame is otherwise just like live.
    for (int cpu = 0; cpu < 2; ++cpu)
    {
        raw.hasSh2[cpu] = se_get_sh2_regs(ctx, cpu, &raw.sh2[cpu]) == SE_OK;
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
        CompressRegion(raw.soundRam, f.soundRam, mLzScratch);
        f.vdp1Regs = std::move(raw.vdp1Regs);
        f.vdp2Regs = std::move(raw.vdp2Regs);
        f.sh2[0] = raw.sh2[0]; f.sh2[1] = raw.sh2[1];
        f.hasSh2[0] = raw.hasSh2[0]; f.hasSh2[1] = raw.hasSh2[1];
        f.slotCount = raw.slotCount;
        std::memcpy(f.slots, raw.slots, sizeof(f.slots));
        f.bytes = f.vdp1Vram.lz.size() + f.vdp2Vram.lz.size() + f.cram.lz.size() +
                  f.wramLow.lz.size() + f.wramHigh.lz.size() + f.vdp1Fb.lz.size() +
                  f.soundRam.lz.size() +
                  f.vdp1Regs.size() * 2 + f.vdp2Regs.size() * 2 + sizeof(f.sh2) + sizeof(f.slots);

        std::lock_guard<std::mutex> lk(mRingMtx);
        // Clear() starts a new recording generation. A frame that was already
        // being compressed when that happened belongs to the old session and
        // must not be appended after the new ring has been cleared.
        if (raw.generation != mGeneration.load(std::memory_order_acquire))
        {
            continue;
        }
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
    mGeneration.fetch_add(1, std::memory_order_acq_rel);
    {
        std::lock_guard<std::mutex> lk(mQMtx);
        mQueue.clear();
    }
    std::lock_guard<std::mutex> lk(mRingMtx);
    mFrames.clear();
    mBytes = 0;
    mLastCaptured = 0;
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
        DecompressRegion(f.soundRam, mSelSoundRam);
        mSelVdp1Regs = f.vdp1Regs;
        mSelVdp2Regs = f.vdp2Regs;
        mSelSh2[0] = f.sh2[0]; mSelSh2[1] = f.sh2[1];
        mSelHasSh2[0] = f.hasSh2[0]; mSelHasSh2[1] = f.hasSh2[1];
        mSelSlotCount = f.slotCount;
        std::memcpy(mSelSlots, f.slots, sizeof(mSelSlots));
    }

    std::memset(out, 0, sizeof(*out));
    out->abi_version = SE_ABI_VERSION;
    // Sound caps are advertised unconditionally, like SE_CAP_SH2_REGS above and the live
    // driver: when the recorded frame carried no sound data the CbSoundRam/CbScspSlots
    // callbacks return 0/empty, which HardwareSnapshot treats the same as an absent cap. (The
    // scrub context latches its capabilities at se_create, so an unconditional set also avoids
    // a first-frame-empty frame turning the sound panels off for the whole session.)
    out->capabilities = SE_CAP_VDP1_VRAM | SE_CAP_VDP2_VRAM | SE_CAP_CRAM |
                        SE_CAP_MAIN_RAM | SE_CAP_VDP1_REGS | SE_CAP_VDP2_REGS |
                        SE_CAP_VDP1_FB | SE_CAP_SH2_REGS | SE_CAP_SOUND_RAM | SE_CAP_SCSP_SLOTS;
    out->user = this;
    out->read_vdp1_vram = CbVdp1;
    out->read_vdp2_vram = CbVdp2;
    out->read_cram      = CbCram;
    out->read_main_ram  = CbMain;
    out->read_vdp1_fb   = CbVdp1Fb;
    out->read_sound_ram = CbSoundRam;
    out->read_vdp1_reg  = CbVdp1Reg;
    out->read_vdp2_reg  = CbVdp2Reg;
    out->read_sh2_regs  = CbSh2Regs;
    out->read_scsp_slots = CbScspSlots;
    // When an edit sink is set (rewind supported), make the scrub source writable: edits to
    // Work/Sound RAM update the visible snapshot (Context::WriteVram) and are forwarded to the
    // App as pending pokes, to be replayed on top of the restored state when Play rewinds here.
    if (mEditCb)
    {
        out->capabilities |= SE_CAP_MEM_WRITE;
        out->write_main_ram  = CbEditMain;
        out->write_sound_ram = CbEditSound;
    }
    // No close callback: the scratch is owned by this recorder, not the context.
    return true;
}

size_t FrameRecorder::CbEditMain(void* u, uint32_t address, const void* src, size_t size)
{
    FrameRecorder* r = static_cast<FrameRecorder*>(u);
    if (r->mEditCb && src && size)
        r->mEditCb(r->mEditUser, 0, address, static_cast<const uint8_t*>(src), size);
    return size;
}
size_t FrameRecorder::CbEditSound(void* u, uint32_t offset, const void* src, size_t size)
{
    FrameRecorder* r = static_cast<FrameRecorder*>(u);
    if (r->mEditCb && src && size)
        r->mEditCb(r->mEditUser, 1, offset, static_cast<const uint8_t*>(src), size);
    return size;
}

void FrameRecorder::SetEditSink(void* user, void (*cb)(void*, int, uint32_t, const uint8_t*, size_t))
{
    mEditUser = user;
    mEditCb = cb;
}

void FrameRecorder::AttachStateBlock(uint64_t frameNumber, uint8_t kind, uint64_t baseKeyframe,
                                     uint32_t fullLen, const uint8_t* payload, size_t len)
{
    std::lock_guard<std::mutex> lk(mRingMtx);
    // Blocks lag their frame by a few frames, so search from the back (recent frames first).
    for (auto it = mFrames.rbegin(); it != mFrames.rend(); ++it)
    {
        if (it->frameNumber != frameNumber) continue;
        if (it->hasState) return;              // already attached (ignore a duplicate)
        it->state.assign(payload, payload + len);
        it->stateKind = kind;
        it->baseKeyframe = baseKeyframe;
        it->stateFullLen = fullLen;
        it->hasState = true;
        it->bytes += len;
        mBytes += len;
        return;
    }
    // Frame not resident (evicted or never captured): drop the block.
}

// Decode one block's RLE payload into 'out' sized to its full length. Caller holds mRingMtx.
static bool DecodeStateBlock(const FrameRecorder::Frame& b, std::vector<uint8_t>& out)
{
    if (!b.hasState) return false;
    out.resize(b.stateFullLen);
    const size_t n = se_state_rle_decode(out.data(), out.size(), b.state.data(), b.state.size());
    return n == b.stateFullLen;
}

bool FrameRecorder::ReconstructState(size_t i, std::vector<uint8_t>& out) const
{
    std::lock_guard<std::mutex> lk(mRingMtx);
    if (i >= mFrames.size()) return false;
    const Frame& f = mFrames[i];
    if (!f.hasState) return false;
    if (f.stateKind == SE_LIVE_STATE_KIND_KEYFRAME)
        return DecodeStateBlock(f, out);
    // Delta: reconstruct full = keyframe_full XOR delta. Its keyframe must still be resident.
    const Frame* kf = nullptr;
    for (const Frame& g : mFrames)
        if (g.frameNumber == f.baseKeyframe && g.hasState &&
            g.stateKind == SE_LIVE_STATE_KIND_KEYFRAME) { kf = &g; break; }
    if (!kf) return false;
    std::vector<uint8_t> base, delta;
    if (!DecodeStateBlock(*kf, base) || !DecodeStateBlock(f, delta)) return false;
    if (base.size() != delta.size()) return false;
    out.resize(base.size());
    se_state_xor(out.data(), base.data(), delta.data(), out.size());
    return true;
}

bool FrameRecorder::CanReconstruct(size_t i) const
{
    std::lock_guard<std::mutex> lk(mRingMtx);
    if (i >= mFrames.size()) return false;
    const Frame& f = mFrames[i];
    if (!f.hasState) return false;
    if (f.stateKind == SE_LIVE_STATE_KIND_KEYFRAME) return true;
    for (const Frame& g : mFrames)
        if (g.frameNumber == f.baseKeyframe && g.hasState &&
            g.stateKind == SE_LIVE_STATE_KIND_KEYFRAME) return true;
    return false;
}

void FrameRecorder::TruncateAfter(size_t i)
{
    // Discard any in-flight (staged / compressing) frames from the pre-rewind timeline so the
    // worker can't re-append a truncated frame, then drop the ring tail after i.
    mGeneration.fetch_add(1, std::memory_order_acq_rel);
    {
        std::lock_guard<std::mutex> lk(mQMtx);
        mQueue.clear();
    }
    std::lock_guard<std::mutex> lk(mRingMtx);
    if (i >= mFrames.size()) return;
    while (mFrames.size() > i + 1)
    {
        mBytes -= mFrames.back().bytes;
        mFrames.pop_back();
    }
    mLastCaptured = mFrames[i].frameNumber;   // Capture() resumes accepting at N+1
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
size_t FrameRecorder::CbSoundRam(void* u, uint32_t off, void* dst, size_t size)
{
    return CopyOut(static_cast<FrameRecorder*>(u)->mSelSoundRam, off, dst, size);
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
int FrameRecorder::CbSh2Regs(void* u, int cpu, se_sh2_regs* out)
{
    if (cpu < 0 || cpu > 1 || !out) { return 0; }
    FrameRecorder* r = static_cast<FrameRecorder*>(u);
    if (!r->mSelHasSh2[cpu]) { return 0; }   // frame predates SH-2 capture / absent
    *out = r->mSelSh2[cpu];
    return 1;
}
int FrameRecorder::CbScspSlots(void* u, se_scsp_slot out[SE_SCSP_SLOT_COUNT])
{
    FrameRecorder* r = static_cast<FrameRecorder*>(u);
    if (!out || r->mSelSlotCount <= 0) { return 0; }
    std::memcpy(out, r->mSelSlots, sizeof(se_scsp_slot) * r->mSelSlotCount);
    return r->mSelSlotCount;
}

}  // namespace sfe
