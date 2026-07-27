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
