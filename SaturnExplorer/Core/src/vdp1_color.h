// vdp1_color — low-level texel and palette decoding shared by the rasterizer
// and the texture viewer. Turns a VDP1 texture pixel (in any of the sprite
// color modes) plus VDP2 color RAM into an RGBA sample. Bit/palette rules per
// Docs/Saturn/VDP1.pdf ch.5-6 and VDP2.pdf §3.4. Validated against a real
// Yabause battle-scene dump (bank16 sprites, RGB555 CRAM).
#pragma once

#include <cstdint>
#include <vector>

#include "saturnexplorer/se_types.h"

namespace se
{

struct Rgba
{
    uint8_t r, g, b, a;   // a == 0 means transparent
};

inline uint16_t ReadBE16(const std::vector<uint8_t>& mem, uint32_t off)
{
    if (off + 1 >= mem.size())
    {
        return 0;
    }
    return static_cast<uint16_t>((mem[off] << 8) | mem[off + 1]);
}

// One CRAM color -> RGB. Index is masked to the physical CRAM size, so this
// works whether the game uses the 1024- or 2048-color RGB555 layout.
inline Rgba CramColor(const std::vector<uint8_t>& cram, se_cram_mode mode, uint32_t index)
{
    if (mode == SE_CRAM_RGB888_1024)
    {
        const uint32_t words = static_cast<uint32_t>(cram.size() / 4);
        if (words == 0)
        {
            return { 0, 0, 0, 255 };
        }
        const uint32_t off = (index & (words - 1)) * 4;
        const uint8_t r = (off + 3 < cram.size()) ? cram[off + 1] : 0;
        const uint8_t g = (off + 3 < cram.size()) ? cram[off + 2] : 0;
        const uint8_t b = (off + 3 < cram.size()) ? cram[off + 3] : 0;
        return { r, g, b, 255 };
    }

    // RGB555 (1024 or 2048 colors).
    const uint32_t words = static_cast<uint32_t>(cram.size() / 2);
    if (words == 0)
    {
        return { 0, 0, 0, 255 };
    }
    const uint16_t v = ReadBE16(cram, (index & (words - 1)) * 2);
    const uint8_t r = static_cast<uint8_t>((v & 0x1F) * 255 / 31);
    const uint8_t g = static_cast<uint8_t>(((v >> 5) & 0x1F) * 255 / 31);
    const uint8_t b = static_cast<uint8_t>(((v >> 10) & 0x1F) * 255 / 31);
    return { r, g, b, 255 };
}

// Decode one texel (x,y) of a sprite texture. Returns a == 0 for transparent.
// colorBank is CMDCOLR (bank modes); clutAddr is the LUT address (LUT mode).
// spd == true keeps index 0 opaque (transparent-pixel disable).
inline Rgba DecodeTexel(const std::vector<uint8_t>& vram, const std::vector<uint8_t>& cram,
                        se_cram_mode cramMode, se_color_mode colorMode, uint32_t texAddr,
                        uint16_t width, int x, int y, uint16_t colorBank,
                        uint32_t clutAddr, bool spd)
{
    switch (colorMode)
    {
    case SE_COLOR_BANK_16:
    {
        const uint32_t stride = width / 2;               // 4 bpp
        const uint32_t off = texAddr + y * stride + x / 2;
        const uint8_t byte = (off < vram.size()) ? vram[off] : 0;
        const uint8_t p = (x & 1) ? (byte & 0x0F) : (byte >> 4);
        if (p == 0 && !spd) return { 0, 0, 0, 0 };
        return CramColor(cram, cramMode, (colorBank & 0xFFF0) | p);
    }
    case SE_COLOR_LUT_16:
    {
        const uint32_t stride = width / 2;
        const uint32_t off = texAddr + y * stride + x / 2;
        const uint8_t byte = (off < vram.size()) ? vram[off] : 0;
        const uint8_t p = (x & 1) ? (byte & 0x0F) : (byte >> 4);
        if (p == 0 && !spd) return { 0, 0, 0, 0 };
        const uint16_t entry = ReadBE16(vram, clutAddr + p * 2);
        if (entry & 0x8000)   // color-bank code -> CRAM
        {
            return CramColor(cram, cramMode, entry & 0x7FF);
        }
        // RGB555 literal
        return { static_cast<uint8_t>((entry & 0x1F) * 255 / 31),
                 static_cast<uint8_t>(((entry >> 5) & 0x1F) * 255 / 31),
                 static_cast<uint8_t>(((entry >> 10) & 0x1F) * 255 / 31), 255 };
    }
    case SE_COLOR_BANK_64:
    case SE_COLOR_BANK_128:
    case SE_COLOR_BANK_256:
    {
        const uint32_t stride = width;                   // 8 bpp
        const uint32_t off = texAddr + y * stride + x;
        const uint8_t p = (off < vram.size()) ? vram[off] : 0;
        uint16_t mask, bankMask;
        if (colorMode == SE_COLOR_BANK_64)  { mask = 0x3F; bankMask = 0xFFC0; }
        else if (colorMode == SE_COLOR_BANK_128) { mask = 0x7F; bankMask = 0xFF80; }
        else                                { mask = 0xFF; bankMask = 0xFF00; }
        if (p == 0 && !spd) return { 0, 0, 0, 0 };
        return CramColor(cram, cramMode, (colorBank & bankMask) | (p & mask));
    }
    case SE_COLOR_RGB555:
    {
        const uint32_t off = texAddr + (y * width + x) * 2;   // 16 bpp
        const uint16_t v = ReadBE16(vram, off);
        if (v == 0 && !spd) return { 0, 0, 0, 0 };
        return { static_cast<uint8_t>((v & 0x1F) * 255 / 31),
                 static_cast<uint8_t>(((v >> 5) & 0x1F) * 255 / 31),
                 static_cast<uint8_t>(((v >> 10) & 0x1F) * 255 / 31), 255 };
    }
    default:
        return { 0, 0, 0, 0 };
    }
}

}  // namespace se
