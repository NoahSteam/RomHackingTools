// HardwareSnapshot — pulls the raw Saturn graphics state for one frame through
// Seam A into owned buffers, so the rest of the core reads an immutable copy
// rather than re-hitting the driver per query. See ARCHITECTURE.md §8.
#pragma once

#include <cstdint>
#include <vector>

#include "saturnexplorer/se_data_source.h"

namespace se {

// Saturn region sizes (Docs/Saturn/MemoryLayout.txt).
constexpr uint32_t kVdp1VramSize = 512 * 1024;
constexpr uint32_t kVdp2VramSize = 512 * 1024;
constexpr uint32_t kCramSize     = 4 * 1024;

class HardwareSnapshot {
public:
    // Reads whatever the driver's capabilities allow into the buffers below.
    // Absent capabilities leave the corresponding buffer empty. Returns true if
    // at least the VDP1 VRAM was captured.
    bool Capture(const se_data_source& ds);

    bool Valid() const { return valid_; }

    const std::vector<uint8_t>& Vdp1Vram() const { return vdp1_vram_; }
    const std::vector<uint8_t>& Vdp2Vram() const { return vdp2_vram_; }
    const std::vector<uint8_t>& Cram()     const { return cram_; }

private:
    std::vector<uint8_t> vdp1_vram_;
    std::vector<uint8_t> vdp2_vram_;
    std::vector<uint8_t> cram_;
    bool valid_ = false;
};

}  // namespace se
