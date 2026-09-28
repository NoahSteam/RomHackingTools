// Vdp1Color — low-level texel and palette decoding shared by the rasterizer
// and the texture viewer. Turns a VDP1 texture pixel (in any of the sprite
// color modes) plus VDP2 color RAM into an RGBA sample. Bit/palette rules per
// Docs/Saturn/VDP1.pdf ch.5-6 and VDP2.pdf §3.4. Validated against a real
// Yabause battle-scene dump (bank16 sprites, RGB555 CRAM).
#pragma once

#include <cstdint>
#include <vector>

#include "saturnexplorer/SeTypes.h"
#include "ByteOrder.h"

namespace se
{

struct Rgba
{
    uint8_t r, g, b, a;   // a == 0 means transparent
};

// Expand a 15-bit RGB555 color (bit15 ignored) to opaque 8-bit RGBA.
inline Rgba Rgb555ToRgba(uint16_t v)
{
    return { static_cast<uint8_t>((v & 0x1F) * 255 / 31),
             static_cast<uint8_t>(((v >> 5) & 0x1F) * 255 / 31),
             static_cast<uint8_t>(((v >> 10) & 0x1F) * 255 / 31), 255 };
}

// How many colour entries a CRAM image holds. The entry width comes from the mode -- 4 bytes in
// RGB888, 2 in the RGB555 layouts -- and the count from the bytes actually captured, which on a
// partial snapshot is neither 1024 nor 2048. Every caller that indexes CRAM needs this, so it
// lives once beside CramWrap instead of four copies of the same conditional divide.
inline uint32_t CramEntryCount(const std::vector<uint8_t>& cram, se_cram_mode mode)
{
    const size_t width = (mode == SE_CRAM_RGB888_1024) ? 4u : 2u;
    return static_cast<uint32_t>(cram.size() / width);
}

// Wrap a CRAM index into 'words' entries. 'words' must be non-zero.
//
// Saturn CRAM holds 1024 or 2048 entries -- both powers of two -- so & (words - 1) is modulo
// and stays one instruction on the compositor's per-pixel path. A snapshot captured from a
// source that returned a short CRAM region can be any length, and there the mask is not modulo
// at all: with 3 words it maps every index onto {0, 2}, so entry 1 is unreachable and the
// colours are silently wrong (in range, just not the ones asked for). Fall back to a real
// modulo for those shapes rather than putting a division in the hot path for the shapes that
// actually occur on hardware.
inline uint32_t CramWrap(uint32_t index, uint32_t words)
{
    return ((words & (words - 1)) == 0) ? (index & (words - 1)) : (index % words);
}

// One CRAM color -> RGB. Index wraps to the physical CRAM size, so this works whether the game
// uses the 1024- or 2048-color RGB555 layout, and on a partially captured CRAM.
inline Rgba CramColor(const std::vector<uint8_t>& cram, se_cram_mode mode, uint32_t index)
{
    if (mode == SE_CRAM_RGB888_1024)
    {
        const uint32_t words = CramEntryCount(cram, mode);
        if (words == 0)
        {
            return { 0, 0, 0, 255 };
        }
        const uint32_t off = CramWrap(index, words) * 4;
        const uint8_t r = (off + 3 < cram.size()) ? cram[off + 1] : 0;
        const uint8_t g = (off + 3 < cram.size()) ? cram[off + 2] : 0;
        const uint8_t b = (off + 3 < cram.size()) ? cram[off + 3] : 0;
        return { r, g, b, 255 };
    }

    // RGB555 (1024 or 2048 colors).
    const uint32_t words = CramEntryCount(cram, mode);
    if (words == 0)
    {
        return { 0, 0, 0, 255 };
    }
    return Rgb555ToRgba(ReadBE16(cram, CramWrap(index, words) * 2));
}

// A decoded texel: its colour, and the 16-bit word the VDP1 would put in the framebuffer for
// it.
//
// That word is what the sprite-priority and colour-calculation bits are defined over: SPCTL
// picks a bit field out of it per sprite type, and which word it is depends on the colour mode --
// the composed CRAM index in the bank modes, the CLUT entry in LUT mode, the colour itself in
// RGB555. The bits differ per texel, which is the whole of finding VDP1-02: one sprite can carry
// pixels at several priorities, and resolving one priority for the command puts some of them in
// front of VDP2 layers that should cover them.
struct Texel
{
    Rgba     color;
    uint16_t word = 0;
};

// Decode one texel (x,y) of a sprite texture, with the framebuffer word it came from. Returns
// color.a == 0 for transparent (the word is then meaningless and set to 0).
// colorBank is CMDCOLR (bank modes); clutAddr is the LUT address (LUT mode).
// spd == true keeps index 0 opaque (transparent-pixel disable).
inline Texel DecodeTexelWord(const std::vector<uint8_t>& vram, const std::vector<uint8_t>& cram,
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
        if (p == 0 && !spd) return { { 0, 0, 0, 0 }, 0 };
        const uint16_t word = static_cast<uint16_t>((colorBank & 0xFFF0) | p);
        return { CramColor(cram, cramMode, word), word };
    }
    case SE_COLOR_LUT_16:
    {
        const uint32_t stride = width / 2;
        const uint32_t off = texAddr + y * stride + x / 2;
        const uint8_t byte = (off < vram.size()) ? vram[off] : 0;
        const uint8_t p = (x & 1) ? (byte & 0x0F) : (byte >> 4);
        if (p == 0 && !spd) return { { 0, 0, 0, 0 }, 0 };
        const uint16_t entry = ReadBE16(vram, clutAddr + p * 2);
        // Saturn color word: MSB (bit 15) set = a direct RGB555 color; MSB clear
        // = a CRAM color-bank index (VDP1 manual §5.x). Games often fill a CLUT
        // with direct RGB colors (all MSB set), so this must not be inverted.
        if (entry & 0x8000)
        {
            return { Rgb555ToRgba(entry), entry };   // direct RGB555
        }
        return { CramColor(cram, cramMode, entry), entry };   // CRAM color-bank index
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
        if (p == 0 && !spd) return { { 0, 0, 0, 0 }, 0 };
        const uint16_t word = static_cast<uint16_t>((colorBank & bankMask) | (p & mask));
        return { CramColor(cram, cramMode, word), word };
    }
    case SE_COLOR_RGB555:
    {
        const uint32_t off = texAddr + (y * width + x) * 2;   // 16 bpp
        const uint16_t v = ReadBE16(vram, off);
        if (v == 0 && !spd) return { { 0, 0, 0, 0 }, 0 };
        return { Rgb555ToRgba(v), v };
    }
    default:
        return { { 0, 0, 0, 0 }, 0 };
    }
}

// The colour alone, for the callers that have no use for the framebuffer word (texture export,
// the palette/texture viewers).
inline Rgba DecodeTexel(const std::vector<uint8_t>& vram, const std::vector<uint8_t>& cram,
                        se_cram_mode cramMode, se_color_mode colorMode, uint32_t texAddr,
                        uint16_t width, int x, int y, uint16_t colorBank,
                        uint32_t clutAddr, bool spd)
{
    return DecodeTexelWord(vram, cram, cramMode, colorMode, texAddr, width, x, y, colorBank,
                           clutAddr, spd).color;
}

}  // namespace se
