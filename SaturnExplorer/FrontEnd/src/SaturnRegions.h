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
constexpr uint32_t kSoundRamSize = 0x80000;   // SCSP sound RAM (512 KiB)
constexpr uint32_t kVdp1RegBytes = 0x18;      // VDP1 register image (TVMR..MODR)
constexpr uint32_t kVdp2RegBytes = 0x120;     // VDP2 register file

// CPU-visible base of VDP1 VRAM (canonical mirror). Used to turn a VDP1 VRAM offset
// (e.g. a command's table_address) into a hex-editor address. The MemoryBackend
// region table is the fuller map; this is the one base the panels reach for directly.
constexpr uint32_t kVdp1VramBase = 0x05C00000u;

// Split a Saturn RGB555 word into its 5-bit channels. Saturn stores colour as
// R in bits 0-4, G in 5-9, B in 10-14 (bit 15 ignored) — matches the core's
// se::Rgb555ToRgba, validated against real dumps.
inline void SplitRgb555(uint16_t w, uint8_t& r5, uint8_t& g5, uint8_t& b5)
{
    r5 = w & 0x1F;
    g5 = (w >> 5) & 0x1F;
    b5 = (w >> 10) & 0x1F;
}

// Expand a Saturn RGB555 word to 8-bit-per-channel (the one place the 5->8 bit
// expansion lives, shared by the palette/FB/watch views). Uses the same *255/31
// scaling as the core's se::Rgb555ToRgba so every view agrees.
inline void DecodeRgb555(uint16_t w, uint8_t& r, uint8_t& g, uint8_t& b)
{
    uint8_t r5, g5, b5;
    SplitRgb555(w, r5, g5, b5);
    r = static_cast<uint8_t>(r5 * 255 / 31);
    g = static_cast<uint8_t>(g5 * 255 / 31);
    b = static_cast<uint8_t>(b5 * 255 / 31);
}
}  // namespace sfe
