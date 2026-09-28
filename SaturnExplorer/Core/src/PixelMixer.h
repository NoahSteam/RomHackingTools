// PixelMixer — the per-pixel VDP2 mixer (roadmap Track B).
//
// Instead of each source blending straight into an RGBA buffer back-to-front (the old
// priority-band loop), every source (back screen, NBG0-3, RBG0, VDP1 sprites) emits a
// per-pixel descriptor into a column, and the column resolves to one RGBA pixel at the
// end. VDP2 colour calculation only ever blends the top-priority pixel with the one
// immediately below it, so a column needs just the top two contributions by priority —
// no full sort. This is what makes per-pixel sprite priority, cross-layer colour calc,
// shadows, and line colour natural rather than special cases.
#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

#include "Vdp1Color.h"   // Rgba

namespace se
{

// One source's contribution at a pixel. prio 0 = the back screen / no contribution;
// NBG/RBG/sprite layers use priority 1..7.
struct PixDesc
{
    uint8_t r = 0, g = 0, b = 0;
    uint8_t prio = 0;
    uint8_t ccEn = 0, ccRatio = 0, ccAdd = 0;   // this layer's colour-calc parameters
};

// The two highest-priority contributions at a pixel. 'valid' is set once any source
// (back screen, an NBG/RBG layer, or a sprite) has emitted here — it distinguishes a
// pixel the mixer actually touched from one still showing the fallback backdrop, and
// tells a sprite draw-mode whether there is an opaque pixel below to blend against.
struct PixColumn
{
    PixDesc top, second;
    bool valid = false;
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

// Insert a contribution, keeping the two highest-priority entries. `prio >=` on ties so a
// later insert wins — reproducing the old back-to-front overwrite order when sources are
// emitted in draw order (back screen, then NBGs sorted low-priority/high-index first,
// then sprites).
inline void EmitPix(PixColumn& col, uint8_t r, uint8_t g, uint8_t b, uint8_t prio,
                    bool ccEn, uint8_t ccRatio, bool ccAdd)
{
    const PixDesc d{ r, g, b, prio, static_cast<uint8_t>(ccEn), ccRatio,
                     static_cast<uint8_t>(ccAdd) };
    col.valid = true;
    if (prio >= col.top.prio)
    {
        col.second = col.top;
        col.top = d;
    }
    else if (prio >= col.second.prio)
    {
        col.second = d;
    }
}

// Resolve a column to an opaque RGBA pixel: the top contribution, blended with the layer
// immediately below when colour calculation is on and the top layer enables it.
inline Rgba ResolveColumn(const PixColumn& col, bool colorCalc)
{
    Rgba out{ col.top.r, col.top.g, col.top.b, 255 };
    if (colorCalc && col.top.ccEn)
    {
        uint8_t dst[3] = { col.second.r, col.second.g, col.second.b };
        BlendCC(dst, col.top.r, col.top.g, col.top.b, col.top.ccRatio, col.top.ccAdd);
        out.r = dst[0];
        out.g = dst[1];
        out.b = dst[2];
    }
    return out;
}

// Resolve what sits immediately below a contribution about to be inserted at 'prio'.
//
// A sprite draw-mode effect (shadow, half-transparency) blends against what is *under* the sprite,
// and that is not the top of the column. Every VDP2 layer is emitted before any sprite, so the top
// may be a layer with a HIGHER priority than this sprite pixel -- one the sprite goes behind, not
// in front of. Blending against it bakes a higher layer into a lower contribution, and if that
// higher layer then colour-calculates against the sprite as its second contribution, it is blended
// with itself.
//
// This was latent while every pixel of a sprite carried one priority (the front-most its pixels
// could reach, so a VDP2 layer above it was unusual). Per-pixel priority makes a low-priority
// sprite pixel ordinary, so it had to be fixed with it.
//
// EmitPix lets a later insert win a priority tie, so the sprite sits in front of everything with
// prio <= its own. Returns false when nothing the column still holds is below it: a column keeps
// only its top two contributions, so a sprite under both is not representable -- and not visible
// either, which is why dropping the effect there is the right degradation.
//
// A sprite landing between the two blends against the second contribution's own colour, even when
// that layer enables colour calculation and had something under it. That is the model rather than a
// shortfall: standard colour calculation blends the top contribution with the one immediately below
// it, so a layer's cc-enable does nothing while it is third in the stack, which is where the second
// contribution ends up once the sprite goes in above it. ResolveColumn says the same thing from the
// other side -- it blends the top against the *raw* second, never a resolved one. Blending
// second-with-third is extended colour calculation (3-layer, roadmap C6), which the mixer does not
// implement anywhere; retaining a third contribution for the sprite path alone would make it the
// only place that did.
inline bool ResolveBelow(const PixColumn& col, uint8_t prio, bool colorCalc, Rgba& out)
{
    if (!col.valid)
    {
        return false;
    }
    if (prio >= col.top.prio)   // in front of everything the column holds
    {
        out = ResolveColumn(col, colorCalc);
        return true;
    }
    if (prio >= col.second.prio)   // between the two
    {
        out = Rgba{ col.second.r, col.second.g, col.second.b, 255 };
        return true;
    }
    return false;
}

// Resolve a whole column buffer to an opaque RGBA image (4 bytes/pixel, sized count*4).
// The counterpart to EmitPix: all column read-out policy lives here rather than in the
// caller. Columns no source touched are left transparent (alpha 0) so the caller's
// fallback backdrop shows through there; every touched column becomes an opaque pixel.
inline void ResolveColumns(const std::vector<PixColumn>& cols, bool colorCalc,
                           std::vector<uint8_t>& outRgba)
{
    outRgba.assign(cols.size() * 4, 0);
    for (size_t i = 0; i < cols.size(); ++i)
    {
        if (!cols[i].valid)
        {
            continue;
        }
        const Rgba c = ResolveColumn(cols[i], colorCalc);
        uint8_t* p = &outRgba[i * 4];
        p[0] = c.r; p[1] = c.g; p[2] = c.b; p[3] = 255;
    }
}

}  // namespace se
