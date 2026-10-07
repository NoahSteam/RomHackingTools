#include "Vdp1Rasterizer.h"

#include <algorithm>
#include <cmath>

#include "Vdp1Color.h"

namespace se
{

namespace
{

// A screen-space vertex. 'depth' is used only when a depth buffer is supplied
// (the 3D view); the 2D path leaves it 0 and relies on painter's order.
struct RVert
{
    float x, y, depth;
};

// How near the cursor has to be to a line primitive's edge to pick it. A quad is picked by
// containment, but a line is one pixel wide, so exact containment would make it unclickable.
const float kPickTolerancePx = 3.0f;

float Edge(float ax, float ay, float bx, float by, float px, float py)
{
    return (bx - ax) * (py - ay) - (by - ay) * (px - ax);
}

int ClampInt(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

// Smallest quad area a click may land in. Deliberately far looser than the 1e-6f
// RasterTriangle rejects at: that one only has to keep 1/area finite, this one answers
// "too thin for the user to have aimed at".
const float kMinPickArea = 1e-3f;

// True if (px,py) falls inside triangle p0,p1,p2 — all three edge functions the same sign
// (or zero, so a point on an edge counts).
//
// A degenerate (zero-area) triangle is rejected outright. Its three edge functions are all
// 0 for every point, so the sign test would report EVERY point as "inside" — letting a
// sprite whose screen quad has collapsed to a line or point (off-screen, scaled to nothing
// or projected edge-on, but still in the list) swallow every click and shadow the real
// sprite underneath. An invisible sprite must not be clickable.
//
// Templated on the vertex so both hit tests share one copy: the 2D path passes the
// sprite's se_vec2 corners, the 3D path the RVerts it projected. That sharing is the
// point — the guard above existed only in the 2D copy until the 3D view was found to be
// picking primitives it never drew.
template <typename V>
bool PointInTri(const V& p0, const V& p1, const V& p2, float px, float py)
{
    const float area = Edge(p0.x, p0.y, p1.x, p1.y, p2.x, p2.y);
    if (area > -kMinPickArea && area < kMinPickArea)
    {
        return false;
    }
    const float e0 = Edge(p0.x, p0.y, p1.x, p1.y, px, py);
    const float e1 = Edge(p1.x, p1.y, p2.x, p2.y, px, py);
    const float e2 = Edge(p2.x, p2.y, p0.x, p0.y, px, py);
    const bool hasNeg = (e0 < 0) || (e1 < 0) || (e2 < 0);
    const bool hasPos = (e0 > 0) || (e1 > 0) || (e2 > 0);
    return !(hasNeg && hasPos);
}

// True if (px,py) falls inside the quad A,B,C,D. Split A,B,C + A,C,D, the same split
// RasterQuad rasterizes, so the picked area matches the drawn one.
template <typename V>
bool PointInQuad(const V corners[4], float px, float py)
{
    return PointInTri(corners[0], corners[1], corners[2], px, py) ||
           PointInTri(corners[0], corners[2], corners[3], px, py);
}

// The per-primitive draw state threaded into the rasterizer: draw-mode effects, an
// optional solid fill color (null = textured), and an optional user-clip rect.
struct EndCodeRows;

struct DrawAttribs
{
    DrawFx fx;
    const Rgba* solid = nullptr;
    const ClipRect* clip = nullptr;
    EndCodeRows* endCode = nullptr;   // textured with the end code enabled; else null
    // For an untextured primitive there is no texel to read a priority out of, so the command's
    // own colour word (CMDCOLR) stands in -- which is what the hardware writes to the framebuffer
    // for those pixels.
    uint16_t solidWord = 0;
};

// End-code handling for one textured primitive (CMDPMOD ECD clear).
//
// An end-code texel is never drawn, and the hardware stops reading a texture line at the SECOND end
// code it meets -- a running count over the line, not two in a row -- so every texel after it in
// that line is transparent too. The rasterizer samples texels at random, so the stop position of
// each line is found by scanning it once, in the order the hardware reads it -- right to left when
// the sprite is flipped horizontally -- and cached for the primitive's other pixels.
struct EndCodeRows
{
    const std::vector<uint8_t>* vram = nullptr;
    se_color_mode mode = SE_COLOR_BANK_16;
    uint32_t addr = 0;
    uint16_t width = 0;
    bool flipX = false;
    std::vector<int32_t> stop;   // per row: read-order index of the terminating end code, -1 unscanned

    bool Blocks(int x, int y)
    {
        uint16_t raw = 0;
        if (!RawTexel(*vram, mode, addr, width, x, y, raw))
        {
            return false;
        }
        if (IsEndCode(mode, raw))
        {
            return true;
        }
        int32_t& s = stop[static_cast<size_t>(y)];
        if (s < 0)
        {
            s = width;   // no terminator
            int seen = 0;
            for (int k = 0; k < width; ++k)
            {
                uint16_t v = 0;
                RawTexel(*vram, mode, addr, width, flipX ? width - 1 - k : k, y, v);
                if (IsEndCode(mode, v) && ++seen == 2)
                {
                    s = k;
                    break;
                }
            }
        }
        return (flipX ? width - 1 - x : x) >= s;
    }
};

// True if user clipping rejects pixel (x,y): mode 0 draws only inside the rect, mode 1
// only outside it. No-op when there is no clip or it is disabled.
inline bool ClipRejects(const ClipRect* clip, int x, int y)
{
    if (!clip || !clip->enable) return false;
    const bool inside = (x >= clip->x0 && x <= clip->x1 && y >= clip->y0 && y <= clip->y1);
    return clip->mode ? inside : !inside;
}

// VDP1 gouraud shading works in 5-bit-per-channel space: the interpolated
// gouraud value (0..31, neutral = 16) is added to the texel channel and clamped.
// 'g5' is the interpolated gouraud channel (float); 't8' the decoded 8-bit texel
// channel (always sourced from a 5-bit color, so the recovery is exact).
uint8_t ApplyGouraud(uint8_t t8, float g5)
{
    const int t5 = (static_cast<int>(t8) * 31 + 127) / 255;      // 8-bit -> 5-bit
    int o5 = t5 + static_cast<int>(g5 + 0.5f) - 16;              // add, neutral 16
    o5 = o5 < 0 ? 0 : (o5 > 31 ? 31 : o5);
    return static_cast<uint8_t>(o5 * 255 / 31);
}

// ApplyGouraud on a packed RGB555 word: add the interpolated 5-bit ramp (neutral 16) per channel.
uint16_t ShadeWord(uint16_t word, int g5r, int g5g, int g5b)
{
    auto ch = [](int t5, int g5) { const int o = t5 + g5 - 16; return o < 0 ? 0 : (o > 31 ? 31 : o); };
    return static_cast<uint16_t>((word & 0x8000) | ch(word & 0x1F, g5r) |
                                 (ch((word >> 5) & 0x1F, g5g) << 5) |
                                 (ch((word >> 10) & 0x1F, g5b) << 10));
}

// Shade a covered pixel with the interpolated 5-bit gouraud channels: the displayed colour always,
// and the framebuffer word only when it is a colour (MSB set) -- a palette code is an index, which
// adding a colour ramp would corrupt.
void ApplyGouraudPixel(uint8_t& cr, uint8_t& cg, uint8_t& cb, uint16_t& word, float gr, float gg, float gb)
{
    cr = ApplyGouraud(cr, gr);
    cg = ApplyGouraud(cg, gg);
    cb = ApplyGouraud(cb, gb);
    if (word & 0x8000)
    {
        word = ShadeWord(word, static_cast<int>(gr + 0.5f), static_cast<int>(gg + 0.5f),
                         static_cast<int>(gb + 0.5f));
    }
}

// Rasterize one UV-mapped triangle. When 'depth' is non-null, depth-test and write per
// pixel (3D view). For each covered pixel the final texel colour (after gouraud) is handed
// to 'sink(idx, r, g, b, fx, word)', which decides how it lands: the 2D path writes the packed
// 'word' into the VDP1 framebuffer (applying draw-mode effects against the pixel already there);
// the 3D path writes RGBA and ignores the word. Keeping the sink out of here lets
// both paths share the coverage/UV/gouraud walk without either owning the other's compositing
// rules.
//
// 'word' is the framebuffer word the texel came from, which is where the sprite's priority bits
// live -- gouraud shading changes the colour handed over but not the word, because the hardware
// reads the priority out of the pixel data, not out of the shaded result.
//
// 'dropP0P1Edge' excludes pixels lying exactly on the p0->p1 edge. A quad is drawn as two
// triangles sharing a diagonal, and a pixel centre landing exactly on that diagonal passes
// both triangles' >= 0 coverage test, so the sink runs twice for it. Writing an opaque texel
// twice is harmless, but shadow and half-transparency read what is already in the column, so a
// second visit blends the sprite against itself and leaves a seam along the diagonal. The
// second triangle drops the shared edge; the first one keeps it, so coverage is unchanged.
template <typename Sink>
void RasterTriangle(const RVert& p0, const RVert& p1, const RVert& p2,
                    const se_vec2& t0, const se_vec2& t1, const se_vec2& t2,
                    const se_texture_ref& tex, bool spd,
                    const std::vector<uint8_t>& vram, const std::vector<uint8_t>& cram,
                    se_cram_mode cramMode, int width, int height,
                    std::vector<float>* depth,
                    bool gourOn, uint16_t g0, uint16_t g1, uint16_t g2,
                    const DrawAttribs& da, bool dropP0P1Edge, Sink&& sink)
{
    const float area = Edge(p0.x, p0.y, p1.x, p1.y, p2.x, p2.y);
    if (std::fabs(area) < 1e-6f)
    {
        return;  // degenerate
    }
    const float invArea = 1.0f / area;

    int minX = static_cast<int>(std::floor(std::min({ p0.x, p1.x, p2.x })));
    int maxX = static_cast<int>(std::ceil (std::max({ p0.x, p1.x, p2.x })));
    int minY = static_cast<int>(std::floor(std::min({ p0.y, p1.y, p2.y })));
    int maxY = static_cast<int>(std::ceil (std::max({ p0.y, p1.y, p2.y })));
    minX = ClampInt(minX, 0, width - 1);
    maxX = ClampInt(maxX, 0, width - 1);
    minY = ClampInt(minY, 0, height - 1);
    maxY = ClampInt(maxY, 0, height - 1);

    const uint16_t texW = tex.width;
    const uint16_t texH = tex.height;

    for (int y = minY; y <= maxY; ++y)
    {
        for (int x = minX; x <= maxX; ++x)
        {
            const float px = x + 0.5f;
            const float py = y + 0.5f;
            const float w0 = Edge(p1.x, p1.y, p2.x, p2.y, px, py) * invArea;
            const float w1 = Edge(p2.x, p2.y, p0.x, p0.y, px, py) * invArea;
            const float w2 = Edge(p0.x, p0.y, p1.x, p1.y, px, py) * invArea;
            if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f)
            {
                continue;  // outside this triangle
            }
            if (dropP0P1Edge && w2 <= 0.0f)
            {
                continue;  // on the shared diagonal; the sibling triangle owns it
            }

            const size_t idx = static_cast<size_t>(y) * width + x;
            float d = 0.0f;
            if (depth)
            {
                d = w0 * p0.depth + w1 * p1.depth + w2 * p2.depth;
                if (d >= (*depth)[idx])
                {
                    continue;  // behind something already drawn
                }
            }

            Rgba c;
            uint16_t word = da.solidWord;
            if (da.solid)
            {
                c = *da.solid;   // untextured polygon: solid fill, always opaque
            }
            else
            {
                const float u = w0 * t0.x + w1 * t1.x + w2 * t2.x;
                const float v = w0 * t0.y + w1 * t1.y + w2 * t2.y;
                const int tx = ClampInt(static_cast<int>(u), 0, texW - 1);
                const int ty = ClampInt(static_cast<int>(v), 0, texH - 1);
                const Texel t = DecodeTexelWord(vram, cram, cramMode, tex.color_mode,
                                                tex.vram_address, texW, tx, ty,
                                                tex.palette_bank, tex.clut_address, spd);
                c = t.color;
                word = t.word;
                if (c.a == 0)
                {
                    continue;  // transparent texel
                }
                if (da.endCode && da.endCode->Blocks(tx, ty))
                {
                    continue;  // end code, or past the one that ended this texture line
                }
            }

            // Mesh: checkerboard stipple — drop every other screen pixel. Skips the
            // depth write too, so a meshed sprite doesn't occlude what shows through it.
            if (da.fx.mesh && ((x + y) & 1))
            {
                continue;
            }
            if (ClipRejects(da.clip, x, y))
            {
                continue;
            }

            if (depth)
            {
                (*depth)[idx] = d;
            }
            uint8_t cr = c.r, cg = c.g, cb = c.b;
            if (gourOn)
            {
                // Interpolate each 5-bit gouraud channel over the triangle and
                // add it to the texel (hardware does this per pixel).
                const float gr = w0 * (g0 & 0x1F)        + w1 * (g1 & 0x1F)        + w2 * (g2 & 0x1F);
                const float gg = w0 * ((g0 >> 5) & 0x1F) + w1 * ((g1 >> 5) & 0x1F) + w2 * ((g2 >> 5) & 0x1F);
                const float gb = w0 * ((g0 >> 10) & 0x1F)+ w1 * ((g1 >> 10) & 0x1F)+ w2 * ((g2 >> 10) & 0x1F);
                ApplyGouraudPixel(cr, cg, cb, word, gr, gg, gb);
            }
            // Hand the covered pixel to the sink with the sprite's draw-mode; the sink
            // owns how shadow / half-luminance / half-transparency and the final write or
            // descriptor emission are applied.
            sink(idx, cr, cg, cb, da.fx, word);
        }
    }
}

// Draw a sprite quad (two triangles A,B,C and A,C,D), routing covered pixels to 'sink'.
template <typename Sink>
void RasterQuad(const RVert v[4], const se_vec2 uv[4], const se_texture_ref& tex,
                bool spd, const std::vector<uint8_t>& vram, const std::vector<uint8_t>& cram,
                se_cram_mode cramMode, int width, int height,
                std::vector<float>* depth,
                const GouraudQuad& g, const DrawAttribs& da, Sink&& sink)
{
    // Split matches the corner order: triangle 1 = A,B,C; triangle 2 = A,C,D.
    RasterTriangle(v[0], v[1], v[2], uv[0], uv[1], uv[2], tex, spd,
                   vram, cram, cramMode, width, height, depth,
                   g.on, g.corner[0], g.corner[1], g.corner[2], da, false, sink);
    RasterTriangle(v[0], v[2], v[3], uv[0], uv[2], uv[3], tex, spd,
                   vram, cram, cramMode, width, height, depth,
                   g.on, g.corner[0], g.corner[2], g.corner[3], da, true, sink);
}

// Plot a solid-color segment between two vertices (DDA), clipped to the frame. Used for
// untextured polyline/line primitives; each pixel goes to 'sink' with the command's draw mode,
// the same way a textured pixel would -- the hardware applies half-luminance, half-transparency,
// shadow, MSB-on and mesh to a line exactly as it does to a polygon.
//
// 'g0'/'g1' are the gouraud colours at the two ends (null = no shading); the hardware interpolates
// them along the run.
//
// When 'depth' is supplied (the 3D view), the segment is depth-tested and written like a
// triangle is, interpolating each vertex's projected depth along the run -- otherwise a line
// behind a quad would draw over it, which reads as the line being in front.
template <typename Sink>
void DrawLine(int width, int height, const RVert& a, const RVert& b, Rgba c, uint16_t word,
              const DrawFx& fx, const uint16_t* g0, const uint16_t* g1,
              const ClipRect* clip, std::vector<float>* depth, Sink&& sink)
{
    const int x0 = static_cast<int>(std::lround(a.x)), y0 = static_cast<int>(std::lround(a.y));
    const int x1 = static_cast<int>(std::lround(b.x)), y1 = static_cast<int>(std::lround(b.y));
    const int steps = std::max(std::abs(x1 - x0), std::abs(y1 - y0));
    const float sx = steps ? float(x1 - x0) / steps : 0.0f;
    const float sy = steps ? float(y1 - y0) / steps : 0.0f;
    const float sd = steps ? (b.depth - a.depth) / steps : 0.0f;
    float fxp = x0 + 0.5f, fyp = y0 + 0.5f, fd = a.depth;
    for (int i = 0; i <= steps; ++i, fxp += sx, fyp += sy, fd += sd)
    {
        const int x = static_cast<int>(fxp), y = static_cast<int>(fyp);
        if (x < 0 || x >= width || y < 0 || y >= height) continue;
        if (fx.mesh && ((x + y) & 1)) continue;
        if (ClipRejects(clip, x, y)) continue;
        const size_t idx = static_cast<size_t>(y) * width + x;
        if (depth)
        {
            if (fd >= (*depth)[idx]) continue;
            (*depth)[idx] = fd;
        }
        uint8_t cr = c.r, cg = c.g, cb = c.b;
        uint16_t w = word;
        if (g0 && g1)
        {
            const float t = steps ? float(i) / steps : 0.0f;
            auto lerp = [t](int s0, int s1) { return s0 + (s1 - s0) * t; };
            ApplyGouraudPixel(cr, cg, cb, w, lerp(*g0 & 0x1F, *g1 & 0x1F),
                              lerp((*g0 >> 5) & 0x1F, (*g1 >> 5) & 0x1F),
                              lerp((*g0 >> 10) & 0x1F, (*g1 >> 10) & 0x1F));
        }
        sink(idx, cr, cg, cb, fx, w);
    }
}

// Draw a line primitive's edges: A-B for a line (kind 2), the full A-B-C-D-A outline for
// a polyline (kind 1). Each edge shades between the gouraud colours of its own two corners.
template <typename Sink>
void DrawEdges(int width, int height, const RVert v[4], uint8_t primKind, Rgba c, uint16_t word,
               const DrawFx& fx, const GouraudQuad& g, const ClipRect* clip,
               std::vector<float>* depth, Sink&& sink)
{
    const int edges = (primKind == 1) ? 4 : 1;
    for (int k = 0; k < edges; ++k)
    {
        const int n = (k + 1) & 3;
        DrawLine(width, height, v[k], v[n], c, word, fx,
                 g.on ? &g.corner[k] : nullptr, g.on ? &g.corner[n] : nullptr, clip, depth, sink);
    }
}

// Distance in pixels from (px,py) to the segment a-b, for picking a primitive that is one
// pixel wide: a point-in-quad test cannot pick a line, since a line has no interior.
float DistanceToSegment(const RVert& a, const RVert& b, float px, float py)
{
    const float dx = b.x - a.x, dy = b.y - a.y;
    const float lenSq = dx * dx + dy * dy;
    float t = 0.0f;
    if (lenSq > 1e-6f)
    {
        t = ((px - a.x) * dx + (py - a.y) * dy) / lenSq;
        t = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);
    }
    const float qx = a.x + t * dx, qy = a.y + t * dy;
    return std::sqrt((px - qx) * (px - qx) + (py - qy) * (py - qy));
}

// Distance to the nearest edge a line primitive actually draws, matching DrawEdges.
float DistanceToEdges(const RVert v[4], uint8_t primKind, float px, float py)
{
    float best = DistanceToSegment(v[0], v[1], px, py);
    if (primKind == 1)
    {
        best = std::min(best, DistanceToSegment(v[1], v[2], px, py));
        best = std::min(best, DistanceToSegment(v[2], v[3], px, py));
        best = std::min(best, DistanceToSegment(v[3], v[0], px, py));
    }
    return best;
}

// VDP1 sprite corners are *inclusive* pixel indices: a primitive spanning columns xa..xc covers
// xc-xa+1 pixels, and the game builds a mech from many small strips laid edge-to-edge (strip N
// ends at row R, strip N+1 starts at R+1). The rasterizer samples at pixel centres, so a corner
// index is the pixel's centre, (i + 0.5) in continuous coordinates, and the quad it describes
// reaches half a pixel beyond it: shift each corner onto its pixel's centre, then nudge it outward
// along the quad's own two edges by half a pixel. The edges then fall on pixel boundaries -- columns
// 10..17 span [10,18) and cover exactly eight centres -- and strips laid edge-to-edge neither
// overlap nor leave a seam. UVs are unchanged, so the texture still maps one texel per pixel.
void ExpandQuadInclusive(RVert v[4])
{
    // Half-pixel outward vector along an edge, or (0,0) for a degenerate edge. Computed from the
    // ORIGINAL corners so the four corner nudges below are independent of each other -- otherwise
    // a rotated/distorted quad would skew, since a later edge would read a corner an earlier one
    // already moved.
    struct Half { float x, y; bool zero() const { return x == 0.0f && y == 0.0f; } };
    auto unitHalf = [](const RVert& from, const RVert& to) -> Half
    {
        const float dx = to.x - from.x;
        const float dy = to.y - from.y;
        const float len = std::sqrt(dx * dx + dy * dy);
        if (len < 1e-3f) return { 0.0f, 0.0f };
        return { dx / len * 0.5f, dy / len * 0.5f };
    };
    Half ab = unitHalf(v[0], v[1]);   // A->B (top)
    Half dc = unitHalf(v[3], v[2]);   // D->C (bottom)
    Half ad = unitHalf(v[0], v[3]);   // A->D (left)
    Half bc = unitHalf(v[1], v[2]);   // B->C (right)

    // A one-pixel-thin or -narrow primitive has a pair of coincident corners, and so no direction
    // to widen along. Borrow the opposite edge's, or else the perpendicular of the other axis, so
    // an 8x1 sprite still grows to a full pixel of height instead of collapsing to nothing.
    if (ab.zero() && dc.zero())
    {
        const Half src = ad.zero() ? bc : ad;
        ab = dc = src.zero() ? Half{ 0.5f, 0.0f } : Half{ src.y, -src.x };
    }
    else if (ab.zero()) ab = dc;
    else if (dc.zero()) dc = ab;
    if (ad.zero() && bc.zero())
    {
        ad = bc = Half{ -ab.y, ab.x };
    }
    else if (ad.zero()) ad = bc;
    else if (bc.zero()) bc = ad;

    for (int k = 0; k < 4; ++k)
    {
        v[k].x += 0.5f;   // index -> pixel centre
        v[k].y += 0.5f;
    }
    v[0].x += -ab.x - ad.x; v[0].y += -ab.y - ad.y;   // A: back along AB and AD
    v[1].x += ab.x - bc.x;  v[1].y += ab.y - bc.y;    // B: forward AB, back BC
    v[2].x += dc.x + bc.x;  v[2].y += dc.y + bc.y;    // C: forward DC and BC
    v[3].x += ad.x - dc.x;  v[3].y += ad.y - dc.y;    // D: forward AD, back DC
}

// Orbit-camera projection: rotate world by yaw (Y) then pitch (X), push back by
// distance, perspective divide.
//
// The world is right-handed and the camera looks back down +Z (see se_sprite_3d and
// se_camera3d in SeTypes.h), so view depth counts *down* from the camera: distance minus
// the rotated Z, never plus.
//
// Subtracting here rather than negating w.z on the way in is the part worth guarding: both
// spellings order the layers correctly, but negating the input is algebraically the same as
// flipping the sign of yaw and pitch, so it would leave the 3D View orbiting backwards
// under the mouse while everything else looked right.
RVert Project(const se_vec3& w, const se_camera3d& cam,
              float cosYaw, float sinYaw, float cosPitch, float sinPitch)
{
    const float x1 = cosYaw * w.x + sinYaw * w.z;
    const float z1 = -sinYaw * w.x + cosYaw * w.z;
    const float y2 = cosPitch * w.y - sinPitch * z1;
    float z2 = cam.distance - (sinPitch * w.y + cosPitch * z1);
    if (z2 < 1.0f)
    {
        z2 = 1.0f;
    }
    RVert p;
    p.x = cam.viewport_width * 0.5f + cam.fov * x1 / z2;
    p.y = cam.viewport_height * 0.5f - cam.fov * y2 / z2;
    p.depth = z2;
    return p;
}

}  // namespace

namespace
{

// One pixel of the VDP1 framebuffer: the packed 16-bit word the hardware would hold. Everything
// downstream -- shadow and half-transparency (which test its MSB: they act on a pixel that holds an
// RGB colour, and leave an MSB-clear one alone), the priority VDP2 reads out of its bits, and the
// colour it decodes -- is a function of this word, so a draw-mode effect has to change the word, not
// a colour kept beside it.
struct FbPixel
{
    uint16_t word = 0;
    bool     written = false;
};

// VDP1 colour arithmetic runs on the packed RGB555 word: halving shifts every 5-bit channel down
// (truncating) and keeps the MSB, averaging is the per-channel truncated mean.
uint16_t HalveWord(uint16_t w)
{
    return static_cast<uint16_t>(((w & 0x7BDE) >> 1) | (w & 0x8000));
}

uint16_t AverageWord(uint16_t a, uint16_t b)
{
    return static_cast<uint16_t>(((static_cast<uint32_t>(a) + b) -
                                  ((static_cast<uint32_t>(a) ^ b) & 0x8421)) >> 1);
}

}  // namespace

SpritePriorityTable::Pixel SpritePriorityTable::Resolve(uint16_t word,
                                                        const std::vector<uint8_t>& cram,
                                                        se_cram_mode cramMode) const
{
    Pixel out;
    out.visible = true;
    if (!valid)
    {
        // No VDP2 registers, so no sprite type to go by: read the MSB as the RGB flag and anything
        // else as a CRAM entry.
        out.color = (word & 0x8000) ? Rgb555ToRgba(word) : CramColor(cram, cramMode, word);
        return out;
    }

    // Colour calculation: the pixel's ratio comes from its colour-calculation bits, and it is enabled
    // by the SPCCCS condition on its priority (or by the data MSB for condition 3).
    auto finish = [&](unsigned pr, unsigned cc, bool msbCc, bool transparent)
    {
        out.prio = slot[pr & 0x7];
        out.visible = !transparent && out.prio != 0;
        out.ccRatio = ccRatio[cc & 0x7];
        bool en = false;
        if (ccEnable)
        {
            switch (ccCond)
            {
            case 0: en = out.prio <= ccNum; break;
            case 1: en = out.prio == ccNum; break;
            case 2: en = out.prio >= ccNum; break;
            default: en = msbCc; break;
            }
        }
        out.ccEn = en;
    };

    if (spclmd && (word & 0x8000))
    {
        out.color = Rgb555ToRgba(word);
        out.isRgb = true;
        bool tp = false;
        if (type & 0x8)
        {
            tp = (word & 0xFF) == 0;
        }
        else if (spriteWindow && type >= 0x2 && type <= 0x7)
        {
            tp = (word & 0x7FFF) == 0;
        }
        finish(0, 0, true, tp);
        return out;
    }

    unsigned src = word;
    if (type & 0x8)
    {
        src &= 0xFF;
    }
    unsigned cc = 0, dc = 0, dcMask = 0;
    bool sd = false;
    switch (type)
    {
    case 0x0: cc = (src >> 11) & 0x7; dcMask = 0x7FF; break;
    case 0x1: cc = (src >> 11) & 0x3; dcMask = 0x7FF; break;
    case 0x2: sd = (src >> 15) & 1; cc = (src >> 11) & 0x7; dcMask = 0x7FF; break;
    case 0x3: sd = (src >> 15) & 1; cc = (src >> 11) & 0x3; dcMask = 0x7FF; break;
    case 0x4: sd = (src >> 15) & 1; cc = (src >> 10) & 0x7; dcMask = 0x3FF; break;
    case 0x5: sd = (src >> 15) & 1; cc = (src >> 11) & 0x1; dcMask = 0x7FF; break;
    case 0x6: sd = (src >> 15) & 1; cc = (src >> 10) & 0x3; dcMask = 0x3FF; break;
    case 0x7: sd = (src >> 15) & 1; cc = (src >> 9) & 0x7; dcMask = 0x1FF; break;
    case 0x8: dcMask = 0x7F; break;
    case 0x9: cc = (src >> 6) & 0x1; dcMask = 0x3F; break;
    case 0xA: dcMask = 0x3F; break;
    case 0xB: cc = (src >> 6) & 0x3; dcMask = 0x3F; break;
    case 0xC: dcMask = 0xFF; break;
    case 0xD: cc = (src >> 6) & 0x1; dcMask = 0xFF; break;
    case 0xE: dcMask = 0xFF; break;
    default:  cc = (src >> 6) & 0x3; dcMask = 0xFF; break;
    }
    dc = src & dcMask;

    // A zero word is transparent. A normal shadow is a pixel whose dot-colour bits are all ones
    // but the lowest; it draws no colour and darkens the layer under it.
    bool tp = (src == 0);
    const bool normalShadow = (dc == (dcMask & ~1u));
    out.color = CramColor(cram, cramMode, cramOffset + dc);
    const bool msbCc = CramMsb(cram, cramMode, cramOffset + dc);
    out.swBit = spriteWindow && sd;   // under SPWINEN the shadow bit is the window bit instead
    if (normalShadow)
    {
        out.shadowMarker = true;
    }
    else if (spriteWindow)
    {
        if (type >= 0x2 && type <= 0x7)
        {
            tp = (src & 0x7FFF) == 0;
        }
    }
    else if (sd)
    {
        // MSB shadow (types 2-7). With colour data it is a sprite shadow: this pixel is shadowed.
        // Without, it is a transparent shadow, which darkens what is under it if TPSDSL allows and
        // is otherwise transparent.
        if (src & 0x7FFF)
        {
            out.shadowSelf = true;
        }
        else if (transparentShadow)
        {
            out.shadowMarker = true;
        }
        else
        {
            tp = true;
        }
    }
    finish(static_cast<unsigned>(NumberOf(word)), cc, msbCc, tp);
    return out;
}

bool Vdp1Rasterizer::BuildSpriteLayer(const Vdp1Scene& scene, const std::vector<uint8_t>& vram,
                                      const std::vector<uint8_t>& cram, se_cram_mode cramMode,
                                      const SpritePriorityTable& prios, std::vector<SpritePixel>& layer)
{
    const int width = scene.screenWidth;
    const int height = scene.screenHeight;
    // A pixel nothing drew is invisible, but VDP2 still reads the zero word there and the gradation
    // screen blurs the colour it decodes to (the CRAM entry at the sprite colour offset).
    SpritePixel blank;
    if (prios.valid)
    {
        blank.color = prios.Resolve(0, cram, cramMode).color;
    }
    layer.assign(static_cast<size_t>(std::max(width, 0)) * std::max(height, 0), blank);
    if (width <= 0 || height <= 0)
    {
        return false;
    }
    // VDP1 draws into its own framebuffer, at its own width. In hi-res modes that is half the
    // display width and every column is doubled at scan-out, so sprite and clip coordinates stay in
    // VDP1 space here and only the read-out below maps to the display. (Scaling the coordinates
    // instead put the right edge of an inclusive clip, or of a sprite, one doubled column short.)
    const int fbWidth = (scene.vdp1Width > 0) ? scene.vdp1Width : width;
    const int fbHeight = (scene.vdp1Height > 0) ? scene.vdp1Height : height;   // 2x in exclusive monitor modes
    std::vector<FbPixel> fb(static_cast<size_t>(fbWidth) * fbHeight);

    // Commands draw into the framebuffer strictly in list order, and a later pixel replaces an
    // earlier one however the two rank in VDP2 priority: that is decided afterwards, on the
    // pixel that survived. Draw-mode effects read the framebuffer pixel under them, never the
    // VDP2 layers -- VDP2 only sees the finished framebuffer -- and work on the packed word:
    //
    //  - MSB-on only sets the destination's MSB;
    //  - shadow halves the destination if its MSB is set, and otherwise does nothing;
    //  - half-luminance halves the sprite's own word;
    //  - half-transparency averages with the destination if its MSB is set, and otherwise
    //    just replaces it.
    auto sink = [&fb](size_t idx, uint8_t, uint8_t, uint8_t, const DrawFx& fx, uint16_t word)
    {
        FbPixel& d = fb[idx];
        if (fx.msbOn)
        {
            // MSB-on keeps the pixel under it and only sets its MSB (and writes a bare MSB
            // where nothing was drawn). It is what marks a sprite for VDP2's MSB shadow; it
            // overrides the colour-calculation mode and draws no colour of its own.
            d.word = static_cast<uint16_t>(d.word | 0x8000);
            d.written = true;
            return;
        }
        if (fx.effect == 1)   // shadow
        {
            if (d.written && (d.word & 0x8000))
            {
                d.word = HalveWord(d.word);
            }
            return;
        }
        if (fx.effect == 2)   // half-luminance
        {
            word = HalveWord(word);
        }
        else if (fx.effect == 3 && d.written && (d.word & 0x8000))   // half-transparency
        {
            word = AverageWord(word, d.word);
        }
        d.word = word;
        d.written = true;
    };

    for (size_t i = 0; i < scene.sprites.size(); ++i)
    {
        const se_sprite_2d& s = scene.sprites[i];
        const se_vec2* c = s.corners;
        RVert v[4] = { { c[0].x, c[0].y, 0.0f }, { c[1].x, c[1].y, 0.0f },
                       { c[2].x, c[2].y, 0.0f }, { c[3].x, c[3].y, 0.0f } };
        const SpriteRender& r = scene.render[i];
        if (r.spdHidden)
        {
            continue;   // an untextured primitive the hardware reads as transparent
        }
        const ClipRect* clip = r.clip.enable ? &r.clip : nullptr;
        if (r.primKind != 0)   // polyline/line: draw edges in solid color (no quad fill)
        {
            DrawEdges(fbWidth, fbHeight, v, r.primKind, Rgb555ToRgba(r.color), r.color, r.fx,
                      r.gouraud, clip, nullptr, sink);
            continue;
        }
        ExpandQuadInclusive(v);
        const Rgba solidCol = r.solid ? Rgb555ToRgba(r.color) : Rgba{};
        DrawAttribs da{ r.fx, r.solid ? &solidCol : nullptr, clip, nullptr, r.color };
        EndCodeRows endCode;
        if (!r.solid && r.endCodeEnabled)
        {
            endCode.vram = &vram;
            endCode.mode = s.texture.color_mode;
            endCode.addr = s.texture.vram_address;
            endCode.width = s.texture.width;
            endCode.flipX = s.flip_x != 0;
            endCode.stop.assign(s.texture.height, -1);
            da.endCode = &endCode;
        }
        RasterQuad(v, s.uv, s.texture, s.transparency == SE_TRANSP_NONE,
                   vram, cram, cramMode, fbWidth, fbHeight, nullptr, r.gouraud, da, sink);
    }

    // Read the finished framebuffer out as VDP2 sees it: each pixel resolved through the sprite type
    // into a colour, a priority, colour-calculation and shadow state, and a sprite-window bit. In a
    // hi-res mode every VDP1 column is doubled here.
    std::vector<int> column(static_cast<size_t>(width));
    for (int x = 0; x < width; ++x)
    {
        column[static_cast<size_t>(x)] = FramebufferColumn(x, fbWidth, width);
    }
    bool any = false;
    uint16_t lastWord = 0;
    SpritePixel lastPixel;
    bool haveLast = false;
    for (int y = 0; y < height; ++y)
    {
        const int row = FramebufferColumn(y, fbHeight, height);
        for (int x = 0; x < width; ++x)
        {
            const FbPixel& p = fb[static_cast<size_t>(row) * fbWidth + column[static_cast<size_t>(x)]];
            if (!p.written)
            {
                continue;
            }
            // A solid sprite repeats one word thousands of times; resolve it once.
            if (!haveLast || p.word != lastWord)
            {
                lastPixel = prios.Resolve(p.word, cram, cramMode);
                lastWord = p.word;
                haveLast = true;
            }
            layer[static_cast<size_t>(y) * width + x] = lastPixel;
            any = true;
        }
    }
    return any;
}

void Vdp1Rasterizer::Render3D(const Vdp1Scene& scene, const std::vector<uint8_t>& vram,
                              const std::vector<uint8_t>& cram, se_cram_mode cramMode,
                              const se_camera3d& camera, const se_render_opts& opts,
                              std::vector<uint8_t>& outRgba, std::vector<float>& depth)
{
    const int width = static_cast<int>(camera.viewport_width);
    const int height = static_cast<int>(camera.viewport_height);
    outRgba.assign(static_cast<size_t>(width) * height * 4, 0);
    if (width <= 0 || height <= 0 || !opts.show_vdp1_sprites)
    {
        return;
    }
    depth.assign(static_cast<size_t>(width) * height, 1e30f);

    const float cosYaw = std::cos(camera.yaw);
    const float sinYaw = std::sin(camera.yaw);
    const float cosPitch = std::cos(camera.pitch);
    const float sinPitch = std::sin(camera.pitch);

    for (size_t i = 0; i < scene.sprites3d.size(); ++i)
    {
        const SpriteRender& r = scene.render[i];
        const se_sprite_3d& s = scene.sprites3d[i];
        RVert v[4] = {
            Project(s.corners[0], camera, cosYaw, sinYaw, cosPitch, sinPitch),
            Project(s.corners[1], camera, cosYaw, sinYaw, cosPitch, sinPitch),
            Project(s.corners[2], camera, cosYaw, sinYaw, cosPitch, sinPitch),
            Project(s.corners[3], camera, cosYaw, sinYaw, cosPitch, sinPitch) };
        // The exploded view separates sprites along Z by draw order, so a pixel's VDP2 priority
        // plays no part in it and the framebuffer word is dropped here.
        auto lineSink = [&outRgba](size_t idx, uint8_t cr, uint8_t cg, uint8_t cb, const DrawFx&,
                                   uint16_t)
        {
            const size_t o = idx * 4;
            outRgba[o + 0] = cr; outRgba[o + 1] = cg; outRgba[o + 2] = cb; outRgba[o + 3] = 255;
        };
        if (r.primKind != 0)
        {
            // Lines and polylines are part of the VDP1 list, so the exploded view has to show
            // them or it is not a view of the list -- a wireframe overlay or a debug cross drawn
            // with line commands would simply be absent, and the user has no way to tell that
            // from the game not having drawn it. No corner expansion: that exists to close the
            // seams between abutting quad strips, and a line has no interior to widen.
            DrawEdges(width, height, v, r.primKind, Rgb555ToRgba(r.color), r.color, DrawFx{},
                      r.gouraud, nullptr, &depth, lineSink);
            continue;
        }
        ExpandQuadInclusive(v);
        const Rgba solidCol = r.solid ? Rgb555ToRgba(r.color) : Rgba{};
        // The exploded 3D view keeps sprites opaque (no shadow/half-transparency against
        // the depth-sorted stack); only Gouraud and solid polygon fills carry over. The
        // depth test in RasterTriangle has already run by the time the sink sees a pixel.
        const DrawAttribs da{ DrawFx{}, r.solid ? &solidCol : nullptr, nullptr, nullptr, r.color };
        RasterQuad(v, s.uv, s.texture, s.transparency == SE_TRANSP_NONE,
                   vram, cram, cramMode, width, height, &depth, r.gouraud, da, lineSink);
    }
}

bool Vdp1Rasterizer::HitTest3D(const Vdp1Scene& scene, const se_camera3d& camera,
                               int x, int y, uint32_t* outCmd)
{
    const float cosYaw = std::cos(camera.yaw);
    const float sinYaw = std::sin(camera.yaw);
    const float cosPitch = std::cos(camera.pitch);
    const float sinPitch = std::sin(camera.pitch);
    const float px = static_cast<float>(x);
    const float py = static_cast<float>(y);

    bool found = false;
    float bestDepth = 0.0f;
    uint32_t bestCmd = 0;
    for (size_t i = 0; i < scene.sprites3d.size(); ++i)
    {
        const se_sprite_3d& g = scene.sprites3d[i];
        const RVert v[4] = {
            Project(g.corners[0], camera, cosYaw, sinYaw, cosPitch, sinPitch),
            Project(g.corners[1], camera, cosYaw, sinYaw, cosPitch, sinPitch),
            Project(g.corners[2], camera, cosYaw, sinYaw, cosPitch, sinPitch),
            Project(g.corners[3], camera, cosYaw, sinYaw, cosPitch, sinPitch) };
        const uint8_t primKind = scene.render[i].primKind;
        if (primKind != 0)
        {
            // A line has no interior, so a point-in-quad test can never pick one; it needs a
            // proximity test against the edges Render3D actually draws. The tolerance is what
            // makes a one-pixel-wide primitive clickable at all -- picking demands the cursor be
            // on the line exactly, which no one can do.
            if (DistanceToEdges(v, primKind, px, py) > kPickTolerancePx)
            {
                continue;
            }
        }
        else if (!PointInQuad(v, px, py))
        {
            continue;
        }
        // Nearest to the camera wins (smallest projected depth = z2).
        const float depth = (v[0].depth + v[1].depth + v[2].depth + v[3].depth) * 0.25f;
        if (!found || depth < bestDepth)
        {
            found = true;
            bestDepth = depth;
            bestCmd = g.command_index;
        }
    }
    if (found && outCmd)
    {
        *outCmd = bestCmd;
    }
    return found;
}

bool PointInSprite(const se_sprite_2d& sprite, float px, float py)
{
    // Test against the quad the rasterizer actually fills. The corners are inclusive pixel indices,
    // so a one-pixel-thin sprite's own corners are a zero-area quad that nothing can be inside.
    RVert v[4] = { { sprite.corners[0].x, sprite.corners[0].y, 0.0f },
                   { sprite.corners[1].x, sprite.corners[1].y, 0.0f },
                   { sprite.corners[2].x, sprite.corners[2].y, 0.0f },
                   { sprite.corners[3].x, sprite.corners[3].y, 0.0f } };
    ExpandQuadInclusive(v);
    return PointInQuad(v, px, py);
}

}  // namespace se
