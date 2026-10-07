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

// Sprite-shadow flags a contribution can carry (PixDesc::shad).
enum : uint8_t
{
    kShadowEnable = 1,   // a scroll/back layer: VDP2 SDCTL lets a sprite shadow darken it
    kShadowMarker = 2,   // a sprite pixel that only shadows what lies under it (normal / transparent
                         // shadow): it draws nothing itself and the layer below shows, darkened
    kShadowSelf   = 4    // a sprite pixel that is shadowed itself (MSB shadow, sprite shadow)
};

// One source's contribution at a pixel. prio 0 = the back screen / no contribution;
// NBG/RBG/sprite layers use priority 1..7.
struct PixDesc
{
    uint8_t r = 0, g = 0, b = 0;
    uint8_t prio = 0;
    uint8_t ccEn = 0, ccRatio = 0, ccAdd = 0;   // this layer's colour-calc parameters
    uint8_t shad = 0;                           // kShadow* flags
    bool    live = false;                       // a source emitted this (the slot is not empty)
};

// The three highest-priority contributions at a pixel. 'valid' is set once any source
// (back screen, an NBG/RBG layer, or a sprite) has emitted here -- it distinguishes a
// pixel the mixer actually touched from one still showing the fallback backdrop. Colour
// calculation only ever blends the top contribution with the one under it, but a sprite
// shadow marker on top hides itself, which promotes the second to top and makes the third the
// one it blends with.
struct PixColumn
{
    PixDesc top, second, third;
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

// Insert a contribution, keeping the three highest-priority entries. `prio >=` on ties so a
// later insert wins -- reproducing the old back-to-front overwrite order when sources are
// emitted in draw order (back screen, then NBGs sorted low-priority/high-index first,
// then sprites).
inline void EmitPix(PixColumn& col, uint8_t r, uint8_t g, uint8_t b, uint8_t prio,
                    bool ccEn, uint8_t ccRatio, bool ccAdd, uint8_t shad = 0)
{
    PixDesc d;
    d.r = r; d.g = g; d.b = b; d.prio = prio;
    d.ccEn = static_cast<uint8_t>(ccEn); d.ccRatio = ccRatio; d.ccAdd = static_cast<uint8_t>(ccAdd);
    d.shad = shad;
    d.live = true;
    col.valid = true;
    if (prio >= col.top.prio || !col.top.live)
    {
        col.third = col.second;
        col.second = col.top;
        col.top = d;
    }
    else if (prio >= col.second.prio || !col.second.live)
    {
        col.third = col.second;
        col.second = d;
    }
    else if (prio >= col.third.prio || !col.third.live)
    {
        col.third = d;
    }
}

// A sprite shadow marker (normal or transparent shadow). It only matters when it is the top
// contribution: then it darkens the layer under it. Beneath another layer it is invisible, and it
// must not become a colour-calculation partner either (VDP2 never blends with a shadow pixel), so
// it is dropped instead of being inserted.
inline void EmitShadowMarker(PixColumn& col, uint8_t prio)
{
    if (prio >= col.top.prio || !col.top.live)
    {
        EmitPix(col, 0, 0, 0, prio, false, 0, false, kShadowMarker);
    }
}

// Resolve a column to an opaque RGBA pixel: the top contribution, blended with the layer
// immediately below when colour calculation is on and the top layer enables it, then shadowed
// when a sprite shadow applies. Returns false (leaving 'out' alone) for a shadow marker with
// nothing beneath it, which draws nothing.
//
// A shadow marker on top is replaced by the layer under it, and that layer is darkened if its
// SDCTL enable is set (a marker is "normal"/"transparent" shadow, which VDP2 applies per layer). A
// sprite pixel that is itself a sprite shadow is darkened regardless. Shadow runs after colour
// calculation, as on VDP2.
inline bool ResolveColumnTo(const PixColumn& col, bool colorCalc, Rgba& out)
{
    const PixDesc* d = &col.top;
    const PixDesc* below = &col.second;
    bool shadow = false;
    if (col.top.shad & kShadowMarker)
    {
        if (!col.second.live)
        {
            return false;
        }
        d = &col.second;
        below = &col.third;
        shadow = (d->shad & kShadowEnable) != 0;
    }
    shadow = shadow || (d->shad & kShadowSelf) != 0;

    out = Rgba{ d->r, d->g, d->b, 255 };
    if (colorCalc && d->ccEn)
    {
        uint8_t dst[3] = { below->r, below->g, below->b };
        BlendCC(dst, d->r, d->g, d->b, d->ccRatio, d->ccAdd);
        out.r = dst[0];
        out.g = dst[1];
        out.b = dst[2];
    }
    if (shadow)
    {
        out.r >>= 1; out.g >>= 1; out.b >>= 1;
    }
    return true;
}

inline Rgba ResolveColumn(const PixColumn& col, bool colorCalc)
{
    Rgba out{ 0, 0, 0, 255 };
    ResolveColumnTo(col, colorCalc, out);
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
        Rgba c{};
        if (!ResolveColumnTo(cols[i], colorCalc, c))
        {
            continue;
        }
        uint8_t* p = &outRgba[i * 4];
        p[0] = c.r; p[1] = c.g; p[2] = c.b; p[3] = 255;
    }
}

}  // namespace se
