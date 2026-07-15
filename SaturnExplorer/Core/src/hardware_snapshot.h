// HardwareSnapshot — pulls the raw Saturn graphics state for one frame through
// Seam A into owned buffers, so the rest of the core reads an immutable copy
// rather than re-hitting the driver per query. See ARCHITECTURE.md §8.
#pragma once

#include <cstdint>
#include <vector>

#include "saturnexplorer/se_data_source.h"

namespace se
{

// Saturn region sizes (Docs/Saturn/MemoryLayout.txt).
constexpr uint32_t kVdp1VramSize = 512 * 1024;
constexpr uint32_t kVdp2VramSize = 512 * 1024;
constexpr uint32_t kCramSize     = 4 * 1024;

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
    se_cram_mode CramMode() const { return mCramMode; }

    // True if the driver supplied VDP2 registers (needed for the NBG compositor).
    bool HasVdp2Regs() const { return mbHasVdp2Regs; }

    // One VDP2 register as a big-endian 16-bit value, addressed by its hardware
    // byte offset (e.g. 0x0E for RAMCTL). Returns 0 if unavailable.
    uint16_t Vdp2Reg(uint32_t hwOffset) const
    {
        const size_t i = hwOffset >> 1;
        return i < mVdp2Regs.size() ? mVdp2Regs[i] : 0;
    }

private:
    std::vector<uint8_t>  mVdp1Vram;
    std::vector<uint8_t>  mVdp2Vram;
    std::vector<uint8_t>  mCram;
    std::vector<uint16_t> mVdp2Regs;   // indexed by (hw offset >> 1)
    se_cram_mode          mCramMode = SE_CRAM_RGB555_1024;
    bool mbHasVdp2Regs = false;
    bool mbValid = false;
};

}  // namespace se
