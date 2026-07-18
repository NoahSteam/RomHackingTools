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
constexpr uint32_t kVdp1RegBytes = 0x18;      // VDP1 register image (TVMR..MODR)
constexpr uint32_t kVdp2RegBytes = 0x120;     // VDP2 register file
}  // namespace sfe
