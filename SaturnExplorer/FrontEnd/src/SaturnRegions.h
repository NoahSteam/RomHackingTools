// Saturn memory region sizes shared by the frontend's memory consumers (the frame
// recorder and the memory dumper) so they agree on one set of hardware constants
// instead of each hardcoding its own copy. Bus addresses + region names that only
// the dumper needs stay local to it. (A single ABI-level region table spanning
// Core + drivers would be the fuller source of truth, but that is a larger change.)
#pragma once

#include <cstdint>

namespace sfe
{
constexpr uint32_t kVdp1VramSize = 0x80000;   // VDP1 VRAM
constexpr uint32_t kVdp2VramSize = 0x80000;   // VDP2 VRAM
constexpr uint32_t kCramSize     = 0x1000;    // VDP2 color RAM
constexpr uint32_t kWramSize     = 0x100000;  // work RAM, low and high (each)
constexpr uint32_t kVdp1FbSize   = 0x40000;   // VDP1 frame buffer (drawn output)
constexpr uint32_t kVdp1RegBytes = 0x18;      // VDP1 register image (TVMR..MODR)
constexpr uint32_t kVdp2RegBytes = 0x120;     // VDP2 register file

// Expand a Saturn RGB555 word to 8-bit-per-channel (the one place the 5->8 bit
// replication lives, shared by the palette/FB/watch views).
inline void DecodeRgb555(uint16_t w, uint8_t& r, uint8_t& g, uint8_t& b)
{
    const uint8_t r5 = (w >> 10) & 0x1F, g5 = (w >> 5) & 0x1F, b5 = w & 0x1F;
    r = static_cast<uint8_t>((r5 << 3) | (r5 >> 2));
    g = static_cast<uint8_t>((g5 << 3) | (g5 >> 2));
    b = static_cast<uint8_t>((b5 << 3) | (b5 >> 2));
}
}  // namespace sfe
