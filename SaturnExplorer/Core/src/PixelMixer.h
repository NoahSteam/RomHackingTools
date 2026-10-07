// PixelMixer — the per-pixel VDP2 mixer (roadmap Track B).
//
// Instead of each source blending straight into an RGBA buffer back-to-front (the old
// priority-band loop), every source (back screen, NBG0-3, RBG0, VDP1 sprites) emits a
// per-pixel descriptor into a column, and the column resolves to one RGBA pixel at the
// end. VDP2 colour calculation only ever blends the top-priority pixel with the layers
// immediately below it, so a column keeps just the top four contributions by priority --
// no full sort. Four is what the extended (up to four-screen) colour calculation with a
// line colour screen inserted can read. This is what makes per-pixel sprite priority,
// cross-layer colour calc, shadows, colour offset and line colour natural rather than
// special cases.
#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

#include "Vdp1Color.h"   // Rgba

namespace se
{

// Per-contribution flags (PixDesc::flags).
enum : uint16_t
{
    kShadowEnable = 1,    // a scroll/back layer: VDP2 SDCTL lets a sprite shadow darken it
    kShadowMarker = 2,    // a sprite pixel that only shadows what lies under it (normal / transparent
                          // shadow): it draws nothing itself and the layer below shows, darkened
    kShadowSelf   = 4,    // a sprite pixel that is shadowed itself (MSB shadow, sprite shadow)
    kLineColorEn  = 8,    // LNCLEN: insert the line colour screen when this is the top image
    kOffsetEnable = 16,   // CLOFEN: apply colour offset when this is the (resulting) top image
    kOffsetSelect = 32,   // CLOFSL: use offset set B instead of A
    kLayerCc      = 64,   // the screen's own CCCTL enable, which a colour-calculation window does not
                          // clear -- extended colour calculation consults it on the second/third image
    kIsRgb        = 128,  // the pixel's colour came from RGB data rather than a palette
    kGradation    = 256   // the screen is the one CCCTL's gradation calculation is set up for
};

// How the hardware ranks screens that share a priority number, lowest first: the back screen, then
// NBG3, NBG2, NBG1, NBG0 (or RBG1), RBG0, and the sprites on top. With per-pixel priorities (the
// special priority function) the order sources are emitted in no longer settles ties, so the rank
// travels with each contribution.
enum : uint8_t
{
    kRankBack = 0, kRankNbg3 = 1, kRankNbg2 = 2, kRankNbg1 = 3, kRankNbg0 = 4, kRankRbg0 = 5,
    kRankSprite = 6
};

// One source's contribution at a pixel. prio 0 = the back screen / no contribution;
// NBG/RBG/sprite layers use priority 1..7.
struct PixDesc
{
    uint8_t r = 0, g = 0, b = 0;
    uint8_t prio = 0;
    bool    ccEn = false, ccAdd = false;        // this layer's colour-calc parameters
    uint8_t ccRatio = 0;
    uint8_t rank = 0;                           // kRank*: breaks ties between equal priorities
    uint16_t flags = 0;                         // k* flags above
    bool    live = false;                       // a source emitted this (the slot is not empty)
};

// The four highest-priority contributions at a pixel. A column whose top is not 'live' was never
// touched by any source (back screen, an NBG/RBG layer, or a sprite) and still shows the fallback
// backdrop. A sprite shadow marker on top hides itself, which promotes the second to top and
// shifts the rest up.
struct PixColumn
{
    PixDesc top, second, third, fourth;
};

// The VDP2 state the mixer reads when it resolves a column, as opposed to what each source
// emitted. Built once per frame by Vdp2Compositor::ReadMixState.
struct MixState
{
    bool colorCalc = true;            // the "show colour calculation" option
    bool secondRatio = false;         // CCCTL CCRTMD: the ratio comes from the second image
    bool extended = false;            // CCCTL EXCCEN, in a mode that allows it (normal resolution)
    bool cram0 = true;                // colour RAM mode 0 (otherwise modes 1/2)
    bool hiresCram12 = false;         // hi-res with colour RAM mode 1/2: palette seconds don't blend
    int  width = 0;                   // columns per row, to find a pixel's row
    uint8_t lineRatio = 0;            // CCRLB: the line colour screen's own ratio
    bool lineCc = false;              // CCCTL LCCCEN: line colour screen colour-calc enable
    std::vector<Rgba> lineColors;     // one line colour per display row (empty: none defined)
    int16_t offset[2][3] = { { 0, 0, 0 }, { 0, 0, 0 } };   // colour offset A / B, R G B

    // Line colour per pixel: the table entry (a CRAM address) of each display row, and an optional
    // per-pixel override of its low seven bits that RBG0's coefficient table can supply
    // (0xFF = none). CRAM is needed to turn the composed address into a colour.
    //
    // 'lineOverride' and 'cram' point into the frame's own buffers: a MixState is built, used by
    // ResolveColumns and dropped within one RenderFrame, and must not outlive it.
    std::vector<uint16_t> lineEntry;
    const std::vector<uint8_t>* lineOverride = nullptr;
    const std::vector<uint8_t>* cram = nullptr;
    se_cram_mode cramMode = SE_CRAM_RGB555_1024;

    // Gradation calculation (CCCTL BOKEN): when the top or second image is the designated screen, the
    // second image takes this per-pixel blur of that screen instead. Empty when not in use.
    bool gradation = false;
    std::vector<Rgba> blur;
};

// Blend a source colour over dst (RGB) using VDP2 colour-calculation rules — additive
// saturates per channel; ratio mode weights the source by (31 - ratio) and dst by
// (ratio + 1) out of 32. Matches the old BlendColorCalc exactly.
inline void BlendCC(uint8_t* dst, uint8_t r, uint8_t g, uint8_t b, uint32_t ratio, bool add)
{
    if (add)
    {
        dst[0] = static_cast<uint8_t>(std::min(255, r + dst[0]));
        dst[1] = static_cast<uint8_t>(std::min(255, g + dst[1]));
        dst[2] = static_cast<uint8_t>(std::min(255, b + dst[2]));
    }
    else
    {
        const uint32_t fore = 31u - (ratio & 0x1F);
        const uint32_t sec = 32u - fore;
        dst[0] = static_cast<uint8_t>((r * fore + dst[0] * sec) >> 5);
        dst[1] = static_cast<uint8_t>((g * fore + dst[1] * sec) >> 5);
        dst[2] = static_cast<uint8_t>((b * fore + dst[2] * sec) >> 5);
    }
}

// Insert a contribution, keeping the four highest-priority entries. Equal priorities are ranked by
// kRank*, and on an equal rank the later insert wins.
// True if a contribution at (prio, rank) sits above 'd': a higher priority, or on a tie a higher
// rank (and an empty slot is always beaten).
inline bool Beats(uint8_t prio, uint8_t rank, const PixDesc& d)
{
    return !d.live || prio > d.prio || (prio == d.prio && rank >= d.rank);
}

inline void EmitPix(PixColumn& col, uint8_t r, uint8_t g, uint8_t b, uint8_t prio,
                    bool ccEn, uint8_t ccRatio, bool ccAdd, uint16_t flags, uint8_t rank)
{
    PixDesc d;
    d.r = r; d.g = g; d.b = b; d.prio = prio;
    d.ccEn = ccEn; d.ccRatio = ccRatio; d.ccAdd = ccAdd;
    d.flags = flags;
    d.rank = rank;
    d.live = true;
    PixDesc* slot[4] = { &col.top, &col.second, &col.third, &col.fourth };
    for (int i = 0; i < 4; ++i)
    {
        if (Beats(prio, rank, *slot[i]))
        {
            for (int j = 3; j > i; --j)
            {
                *slot[j] = *slot[j - 1];
            }
            *slot[i] = d;
            return;
        }
    }
}

// A sprite shadow marker (normal or transparent shadow). It only matters when it is the top
// contribution: then it darkens the layer under it. Beneath another layer it is invisible, and it
// must not become a colour-calculation partner either (VDP2 never blends with a shadow pixel), so
// it is dropped instead of being inserted.
inline void EmitShadowMarker(PixColumn& col, uint8_t prio)
{
    if (Beats(prio, kRankSprite, col.top))
    {
        EmitPix(col, 0, 0, 0, prio, false, 0, false, kShadowMarker, kRankSprite);
    }
}

namespace mixer_detail
{
// Replace a pixel's colour with the per-channel truncated mean of it and (r, g, b), which is how
// VDP2 averages for the extended colour calculation.
inline void Average(PixDesc& a, uint8_t r, uint8_t g, uint8_t b)
{
    a.r = static_cast<uint8_t>((a.r + r) >> 1);
    a.g = static_cast<uint8_t>((a.g + g) >> 1);
    a.b = static_cast<uint8_t>((a.b + b) >> 1);
}
}  // namespace mixer_detail

// Resolve one column (the pixel at flat index 'index') to an opaque RGBA pixel, in the order VDP2
// does it: pick the top image (a shadow marker is replaced by the layer under it), colour-calculate
// it against what lies below -- optionally through the line colour screen and the extended
// three/four-screen average -- then colour offset by the top image's registers, then shadow.
// Returns false (leaving 'out' alone) for a shadow marker with nothing beneath it, which draws
// nothing.
inline bool ResolveColumnAt(const PixColumn& col, const MixState& mix, size_t index, Rgba& out)
{
    using mixer_detail::Average;
    // The candidate images, top first; a marker is skipped.
    PixDesc empty;
    const PixDesc* img[5] = { &col.top, &col.second, &col.third, &col.fourth, &empty };
    int first = 0;
    bool shadow = false;
    if (col.top.flags & kShadowMarker)
    {
        if (!col.second.live)
        {
            return false;
        }
        first = 1;
        shadow = (col.second.flags & kShadowEnable) != 0;
    }
    const PixDesc& top = *img[first];
    shadow = shadow || (top.flags & kShadowSelf) != 0;

    uint8_t rgb[3] = { top.r, top.g, top.b };
    if (mix.colorCalc && top.ccEn)
    {
        PixDesc second = *img[first + 1];
        PixDesc third = *img[first + 2];
        PixDesc fourth = *img[first + 3];
        if (mix.gradation)
        {
            // Gradation: the second image becomes the blurred designated screen whenever that screen is
            // the top or the second image. (The line colour screen and extended calculation are off.)
            if ((top.flags | second.flags) & kGradation)
            {
                const Rgba& b = mix.blur[index];
                second.r = b.r; second.g = b.g; second.b = b.b;
            }
        }
        else if (top.flags & kLineColorEn)
        {
            // The line colour screen has no priority: it is forced in as the second image, and
            // everything that was below moves down one.
            fourth = third;
            third = second;
            second = PixDesc{};
            second.live = true;
            second.ccRatio = mix.lineRatio;
            second.flags = mix.lineCc ? kLayerCc : uint8_t(0);
            const size_t row = index / static_cast<size_t>(std::max(mix.width, 1));
            if (row < mix.lineColors.size())
            {
                Rgba lc = mix.lineColors[row];
                if (mix.lineOverride && mix.cram && (*mix.lineOverride)[index] != 0xFF)
                {
                    // The coefficient table's seven bits replace the low seven bits of the line's
                    // CRAM address; the top four stay from the line colour table.
                    lc = CramColor(*mix.cram, mix.cramMode,
                                   (mix.lineEntry[row] & ~0x7Fu) | (*mix.lineOverride)[index]);
                }
                second.r = lc.r; second.g = lc.g; second.b = lc.b;
            }
            if (mix.extended && mix.lineCc)
            {
                // Only with the line colour screen's own colour-calculation enable (LCCCEN).
                if (mix.cram0)
                {
                    // Colour RAM mode 0: the second image is the line colour averaged with the third,
                    // which is itself halved first when it colour-calculates.
                    const int shift = (third.flags & kLayerCc) ? 1 : 0;
                    Average(second, static_cast<uint8_t>(third.r >> shift),
                            static_cast<uint8_t>(third.g >> shift), static_cast<uint8_t>(third.b >> shift));
                }
                else if (third.flags & kIsRgb)
                {
                    // Modes 1/2 need an RGB third image (a palette one cannot take part).
                    if ((third.flags & kLayerCc) && (fourth.flags & kIsRgb))
                    {
                        Average(third, fourth.r, fourth.g, fourth.b);
                    }
                    Average(second, third.r, third.g, third.b);
                }
            }
        }
        else if (mix.extended && (second.flags & kLayerCc) &&
                 (mix.cram0 || (third.flags & kIsRgb)))
        {
            // Extended colour calculation: the second image is itself the average of the second
            // and third before the top blends with it.
            Average(second, third.r, third.g, third.b);
        }

        uint8_t dst[3] = { second.r, second.g, second.b };
        if (mix.hiresCram12 && !(second.flags & kIsRgb))
        {
            // Hi-res with colour RAM mode 1/2: a palette-format second image cannot be blended
            // with, so the pixel stays as it is.
            dst[0] = top.r; dst[1] = top.g; dst[2] = top.b;
        }
        const uint32_t ratio = mix.secondRatio ? second.ccRatio : top.ccRatio;
        BlendCC(dst, top.r, top.g, top.b, ratio, top.ccAdd);
        rgb[0] = dst[0]; rgb[1] = dst[1]; rgb[2] = dst[2];
    }

    if (top.flags & kOffsetEnable)
    {
        const int16_t* o = mix.offset[(top.flags & kOffsetSelect) ? 1 : 0];
        for (int i = 0; i < 3; ++i)
        {
            rgb[i] = static_cast<uint8_t>(std::max(0, std::min(255, rgb[i] + o[i])));
        }
    }
    if (shadow)
    {
        for (int i = 0; i < 3; ++i) rgb[i] = static_cast<uint8_t>(rgb[i] >> 1);
    }
    out = Rgba{ rgb[0], rgb[1], rgb[2], 255 };
    return true;
}

// Resolve a whole column buffer to an opaque RGBA image (4 bytes/pixel, sized count*4).
// The counterpart to EmitPix: all column read-out policy lives here rather than in the
// caller. Columns no source touched are left transparent (alpha 0) so the caller's
// fallback backdrop shows through there; every touched column becomes an opaque pixel.
inline void ResolveColumns(const std::vector<PixColumn>& cols, const MixState& mix,
                           std::vector<uint8_t>& outRgba)
{
    outRgba.assign(cols.size() * 4, 0);
    for (size_t i = 0; i < cols.size(); ++i)
    {
        if (!cols[i].top.live)
        {
            continue;
        }
        Rgba c{};
        if (!ResolveColumnAt(cols[i], mix, i, c))
        {
            continue;
        }
        uint8_t* p = &outRgba[i * 4];
        p[0] = c.r; p[1] = c.g; p[2] = c.b; p[3] = 255;
    }
}

}  // namespace se
