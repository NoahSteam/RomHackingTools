// SpriteLayer -- how VDP2 reads the VDP1 framebuffer: the SPCTL sprite type, which bits of a
// framebuffer word are priority, colour-calculation ratio, dot colour and shadow, and what each
// pixel therefore is (colour, priority, colour-calculation, shadow, sprite-window bit). Ported from
// Mednafen's vdp2_render.cpp T_DrawSpriteData and checked against the VDP2 manual (ch.9 sprite
// data, ch.14 shadow). See ARCHITECTURE.md §7.
#pragma once

#include <cstdint>
#include <vector>

#include "saturnexplorer/SeTypes.h"
#include "Vdp1Color.h"

namespace se
{

// One displayed sprite pixel as VDP2 sees it. 'visible' is false for a transparent pixel and for
// one whose priority maps to 0 (not displayed); a shadow marker is visible but draws no colour of
// its own -- it darkens the layer beneath.
struct SpritePixel
{
    bool    visible = false;
    Rgba    color{ 0, 0, 0, 255 };
    uint8_t prio = 0;
    bool    shadowMarker = false;   // normal / transparent shadow: darkens what is under it
    bool    shadowSelf = false;     // MSB (sprite) shadow: this pixel is darkened itself
    bool    ccEn = false;           // colour calculation applies to this pixel
    uint8_t ccRatio = 0;
    bool    swBit = false;          // the sprite-window bit (shadow bit under SPWINEN)
    bool    isRgb = false;          // the colour is RGB data in the word, not a CRAM lookup
};

// How a VDP1 sprite pixel's priority is decided.
//
// The VDP2 sprite-priority registers (PRISA..PRISD) hold eight priority slots. A sprite pixel
// selects one of them with a "priority number" encoded in the framebuffer word itself, and which
// bits carry it depends on the SPCTL sprite type. So priority is a property of the *pixel*, not
// of the command: a single sprite whose CLUT or colour bank spans several numbers has pixels at
// several priorities, and hardware interleaves each of them with the VDP2 layers separately.
//
// Resolving one priority per sprite -- the front-most its pixels could reach -- was accurate for
// the common case where every pixel lands on the same number, and put the rest in front of VDP2
// layers that should have covered them (review findings VDP1-02 and VDP2-01).
struct SpritePriorityTable
{
    uint8_t slot[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };   // PRISA..PRISD, by priority number
    int     type = 0;                               // SPCTL sprite type (0..F)
    bool    spclmd = false;                         // SPCLMD: mixed RGB/palette data
    bool    valid = false;                          // false with no VDP2 registers captured
    uint32_t cramOffset = 0;                        // CRAOFB sprite offset, pre-shifted (<< 8)
    bool    spriteWindow = false;                   // SPCTL SPWINEN: the shadow bit is a window bit
    bool    transparentShadow = false;              // SDCTL TPSDSL: a transparent shadow draws one
    bool    ccEnable = false;                       // CCCTL SPCCEN
    uint8_t ccCond = 0;                             // SPCTL SPCCCS: 0 <=, 1 ==, 2 >=, 3 data MSB
    uint8_t ccNum = 0;                              // SPCTL SPCCN
    uint8_t ccRatio[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };   // CCRSA..CCRSD, by colour-calc number
    bool    ccAdd = false;                          // CCCTL CCMD: add instead of ratio-blend

    // Where the priority number sits in a framebuffer word, per SPCTL sprite type (VDP1 manual;
    // mirrors Yabause's Vdp1GetSpritePixelInfo). A zero mask means the type has no priority bits.
    //
    // Expressed as a field rather than folded into the extraction, because a caller that wants to
    // know which numbers a sprite *can* select -- not just the one a given pixel does -- needs to
    // know which bits are free. Types B and F carry no number and always use slot 0.
    struct Field
    {
        int      shift = 0;
        uint16_t mask = 0;
    };

    static Field FieldFor(int type)
    {
        switch (type)
        {
        case 0x0: return { 14, 0x3 };
        case 0x1: return { 13, 0x7 };
        case 0x2: return { 14, 0x1 };
        case 0x3: return { 13, 0x3 };
        case 0x4: return { 13, 0x3 };
        case 0x5: case 0x6: case 0x7: return { 12, 0x7 };
        case 0x8: case 0x9: return { 7, 0x1 };
        case 0xA:           return { 6, 0x3 };
        case 0xC: case 0xD: return { 7, 0x1 };
        case 0xE:           return { 6, 0x3 };
        default:            return { 0, 0x0 };
        }
    }

    // The priority NUMBER a framebuffer word encodes. A direct-RGB pixel (MSB set, mixed-colour
    // mode) carries no number and uses slot 0.
    int NumberOf(uint16_t word) const
    {
        if (spclmd && (word & 0x8000))
        {
            return 0;
        }
        const Field f = FieldFor(type);
        return (word >> f.shift) & f.mask;
    }

    // The VDP2 priority (0..7) for a framebuffer word.
    uint8_t Of(uint16_t word) const
    {
        return valid ? slot[NumberOf(word) & 0x7] : 0;
    }

    using Pixel = SpritePixel;

    // What VDP2 makes of one framebuffer word. This is the only place a framebuffer word becomes a
    // colour: the framebuffer holds the packed word the hardware would, and what the word means
    // depends on the SPCTL sprite type and SPCLMD, not on what drew it. A word with the MSB set is
    // an RGB555 colour only in mixed mode (SPCLMD); otherwise, and always when the MSB is clear, it
    // is a palette code: the type picks the dot-colour bits out of it, and CRAM is read at CRAOFB's
    // sprite offset plus that.
    Pixel Resolve(uint16_t word, const std::vector<uint8_t>& cram, se_cram_mode cramMode) const;
};

}  // namespace se
