// HardwareSnapshot — pulls the raw Saturn graphics state for one frame through
// Seam A into owned buffers, so the rest of the core reads an immutable copy
// rather than re-hitting the driver per query. See ARCHITECTURE.md §8.
#pragma once

#include <cstdint>
#include <cstring>
#include <vector>

#include "saturnexplorer/SeDataSource.h"

namespace se
{

// Saturn region sizes / bus addresses (Docs/Saturn/MemoryLayout.txt).
constexpr uint32_t kVdp1VramSize = 512 * 1024;
constexpr uint32_t kVdp2VramSize = 512 * 1024;
constexpr uint32_t kCramSize     = 4 * 1024;
constexpr uint32_t kWramSize     = 1024 * 1024;
constexpr uint32_t kVdp1FbSize   = 256 * 1024;   /* VDP1 frame buffer (drawn output) */
constexpr uint32_t kSoundRamSize = 512 * 1024;   /* SCSP sound RAM (0x25A00000) */
constexpr uint32_t kWramLowBase  = 0x00200000;
constexpr uint32_t kWramHighBase = 0x06000000;
constexpr uint32_t kSoundRamBase = 0x25A00000;

class HardwareSnapshot
{
public:
    // Reads whatever the driver's capabilities allow into the buffers below.
    // Absent capabilities leave the corresponding buffer empty. Returns true if
    // at least the VDP1 VRAM was captured.
    bool Capture(const se_data_source& dataSource);

    bool Valid() const { return mbValid; }

    const std::vector<uint8_t>& Vdp1Vram() const { return mVdp1Vram; }
    const std::vector<uint8_t>& Vdp2Vram() const { return mVdp2Vram; }
    const std::vector<uint8_t>& Cram() const { return mCram; }
    const std::vector<uint8_t>& WramLow() const { return mWramLow; }
    const std::vector<uint8_t>& WramHigh() const { return mWramHigh; }
    const std::vector<uint8_t>& Vdp1Fb() const { return mVdp1Fb; }
    const std::vector<uint8_t>& SoundRam() const { return mSoundRam; }
    se_cram_mode CramMode() const { return mCramMode; }

    // Overwrite bytes in a region's captured buffer (Hex Editor edits). Returns the number
    // written (clamped to the buffer). Covers every captured region — VDP1/VDP2 VRAM, CRAM,
    // and the VDP1 framebuffer edits feed straight back into the reconstructed image.
    size_t WriteRegion(se_vram_kind kind, uint32_t offset, const void* src, size_t size)
    {
        std::vector<uint8_t>* dst = nullptr;
        switch (kind)
        {
        case SE_VRAM_KIND_WRAM_LOW:  dst = &mWramLow;  break;
        case SE_VRAM_KIND_WRAM_HIGH: dst = &mWramHigh; break;
        case SE_VRAM_KIND_SOUND_RAM: dst = &mSoundRam; break;
        case SE_VRAM_KIND_VDP1_VRAM: dst = &mVdp1Vram; break;
        case SE_VRAM_KIND_VDP2_VRAM: dst = &mVdp2Vram; break;
        case SE_VRAM_KIND_CRAM:      dst = &mCram;      break;
        case SE_VRAM_KIND_VDP1_FB:   dst = &mVdp1Fb;    break;
        default: return 0;
        }
        if (!src || offset >= dst->size()) return 0;
        const size_t avail = dst->size() - offset;
        const size_t n = size < avail ? size : avail;
        std::memcpy(dst->data() + offset, src, n);
        return n;
    }

    // Overwrite one VDP register (16-bit, addressed by its hardware byte offset). Returns true
    // if the offset is in range. Register edits also feed the reconstruction (resolution,
    // colour math, etc.). Only within the captured register file — no effect if absent.
    bool SetVdp1Reg(uint32_t hwOffset, uint16_t value)
    {
        const size_t i = hwOffset >> 1;
        if (i >= mVdp1Regs.size()) return false;
        mVdp1Regs[i] = value;
        return true;
    }
    bool SetVdp2Reg(uint32_t hwOffset, uint16_t value)
    {
        const size_t i = hwOffset >> 1;
        if (i >= mVdp2Regs.size()) return false;
        mVdp2Regs[i] = value;
        return true;
    }

    // True if the driver supplied VDP1 / VDP2 registers.
    bool HasVdp1Regs() const { return mbHasVdp1Regs; }
    bool HasVdp2Regs() const { return mbHasVdp2Regs; }

    // SH-2 register file per CPU (0 = master, 1 = slave), if the driver supplied it.
    bool HasSh2Regs(int cpu) const { return cpu >= 0 && cpu < 2 && mbHasSh2[cpu]; }
    const se_sh2_regs& Sh2Regs(int cpu) const { return mSh2[cpu & 1]; }

    // Decoded SCSP voices (empty unless the source supplied SE_CAP_SCSP_SLOTS).
    const std::vector<se_scsp_slot>& ScspSlots() const { return mScspSlots; }

    // Live CD-block state (valid only when HasCdStatus() — SE_CAP_CD_STATUS sources).
    bool                 HasCdStatus() const { return mHasCdStatus; }
    const se_cd_status&  CdStatus()    const { return mCdStatus; }

    // One register as a big-endian 16-bit value, addressed by its hardware byte
    // offset (e.g. VDP2 0x0E for RAMCTL). Returns 0 if unavailable.
    uint16_t Vdp1Reg(uint32_t hwOffset) const
    {
        const size_t i = hwOffset >> 1;
        return i < mVdp1Regs.size() ? mVdp1Regs[i] : 0;
    }
    uint16_t Vdp2Reg(uint32_t hwOffset) const
    {
        const size_t i = hwOffset >> 1;
        return i < mVdp2Regs.size() ? mVdp2Regs[i] : 0;
    }

private:
    std::vector<uint8_t>  mVdp1Vram;
    std::vector<uint8_t>  mVdp2Vram;
    std::vector<uint8_t>  mCram;
    std::vector<uint8_t>  mWramLow;    // 0x00200000 (present if SE_CAP_MAIN_RAM)
    std::vector<uint8_t>  mWramHigh;   // 0x06000000
    std::vector<uint8_t>  mVdp1Fb;     // VDP1 frame buffer (present if SE_CAP_VDP1_FB)
    std::vector<uint8_t>  mSoundRam;   // SCSP sound RAM (present if SE_CAP_SOUND_RAM)
    std::vector<uint16_t> mVdp1Regs;   // indexed by (hw offset >> 1)
    std::vector<uint16_t> mVdp2Regs;
    se_sh2_regs           mSh2[2] = {};   // [0] master, [1] slave
    std::vector<se_scsp_slot> mScspSlots; // decoded SCSP voices (SE_CAP_SCSP_SLOTS)
    se_cd_status          mCdStatus = {};  // live CD-block state (SE_CAP_CD_STATUS)
    bool                  mHasCdStatus = false;
    se_cram_mode          mCramMode = SE_CRAM_RGB555_1024;
    bool mbHasVdp1Regs = false;
    bool mbHasVdp2Regs = false;
    bool mbHasSh2[2] = { false, false };
    bool mbValid = false;
};

}  // namespace se
