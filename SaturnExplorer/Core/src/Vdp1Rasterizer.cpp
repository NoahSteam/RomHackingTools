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

float Edge(float ax, float ay, float bx, float by, float px, float py)
{
    return (bx - ax) * (py - ay) - (by - ay) * (px - ax);
}

int ClampInt(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
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

// Rasterize one UV-mapped triangle. When 'depth' is non-null, depth-test and
// write per pixel (3D view); when null, overwrite in call order (2D painter's).
void RasterTriangle(const RVert& p0, const RVert& p1, const RVert& p2,
                    const se_vec2& t0, const se_vec2& t1, const se_vec2& t2,
                    const se_texture_ref& tex, bool spd,
                    const std::vector<uint8_t>& vram, const std::vector<uint8_t>& cram,
                    se_cram_mode cramMode, int width, int height,
                    std::vector<uint8_t>& out, std::vector<float>* depth,
                    bool gourOn, uint16_t g0, uint16_t g1, uint16_t g2,
                    DrawFx fx, const Rgba* solid)
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
            if (solid)
            {
                c = *solid;   // untextured polygon: solid fill, always opaque
            }
            else
            {
                const float u = w0 * t0.x + w1 * t1.x + w2 * t2.x;
                const float v = w0 * t0.y + w1 * t1.y + w2 * t2.y;
                const int tx = ClampInt(static_cast<int>(u), 0, texW - 1);
                const int ty = ClampInt(static_cast<int>(v), 0, texH - 1);
                c = DecodeTexel(vram, cram, cramMode, tex.color_mode,
                                tex.vram_address, texW, tx, ty,
                                tex.palette_bank, tex.clut_address, spd);
                if (c.a == 0)
                {
                    continue;  // transparent texel
                }
            }

            // Mesh: checkerboard stipple — drop every other screen pixel. Skips the
            // depth write too, so a meshed sprite doesn't occlude what shows through it.
            if (fx.mesh && ((x + y) & 1))
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
                cr = ApplyGouraud(c.r, gr);
                cg = ApplyGouraud(c.g, gg);
                cb = ApplyGouraud(c.b, gb);
            }
            const size_t o = idx * 4;
            // Draw-mode color calculation. Shadow/half-transparency read the pixel
            // already below; over nothing (alpha 0) they degrade to a plain write.
            // This blends against whatever the priority-band loop drew below (VDP2 +
            // earlier sprites) — an approximation of the two-stage VDP1/VDP2 blend that
            // the descriptor mixer will make exact.
            switch (fx.effect)
            {
            case 1:  // shadow: halve the pixel below, keep the sprite's own color hidden
                if (out[o + 3] != 0)
                {
                    out[o + 0] >>= 1; out[o + 1] >>= 1; out[o + 2] >>= 1;
                }
                break;
            case 2:  // half-luminance
                out[o + 0] = cr >> 1; out[o + 1] = cg >> 1; out[o + 2] = cb >> 1;
                out[o + 3] = 255;
                break;
            case 3:  // half-transparency: average with the pixel below
                if (out[o + 3] != 0)
                {
                    out[o + 0] = static_cast<uint8_t>((cr + out[o + 0]) >> 1);
                    out[o + 1] = static_cast<uint8_t>((cg + out[o + 1]) >> 1);
                    out[o + 2] = static_cast<uint8_t>((cb + out[o + 2]) >> 1);
                }
                else
                {
                    out[o + 0] = cr; out[o + 1] = cg; out[o + 2] = cb; out[o + 3] = 255;
                }
                break;
            default:
                out[o + 0] = cr; out[o + 1] = cg; out[o + 2] = cb; out[o + 3] = 255;
                break;
            }
        }
    }
}

// Draw a sprite quad (two triangles A,B,C and A,C,D) into 'out'.
void RasterQuad(const RVert v[4], const se_vec2 uv[4], const se_texture_ref& tex,
                bool spd, const std::vector<uint8_t>& vram, const std::vector<uint8_t>& cram,
                se_cram_mode cramMode, int width, int height,
                std::vector<uint8_t>& out, std::vector<float>* depth,
                const GouraudQuad& g, DrawFx fx, const Rgba* solid)
{
    // Split matches the corner order: triangle 1 = A,B,C; triangle 2 = A,C,D.
    RasterTriangle(v[0], v[1], v[2], uv[0], uv[1], uv[2], tex, spd,
                   vram, cram, cramMode, width, height, out, depth,
                   g.on, g.corner[0], g.corner[1], g.corner[2], fx, solid);
    RasterTriangle(v[0], v[2], v[3], uv[0], uv[2], uv[3], tex, spd,
                   vram, cram, cramMode, width, height, out, depth,
                   g.on, g.corner[0], g.corner[2], g.corner[3], fx, solid);
}

// VDP1 sprite corners are *inclusive* pixel coordinates: a sprite spanning
// screen columns xa..xc covers xc-xa+1 pixels, and the game builds a mech from
// many small strips laid edge-to-edge (strip N ends at row R, strip N+1 starts
// at R+1). A center-sampling rasterizer treats each quad's span as half-open and
// draws one pixel fewer per axis, so a 1px seam opens at every strip boundary and
// the backdrop shows through. Nudge each corner outward along the quad's own two
// edges by half a pixel: the far edge's pixel centers then land on the boundary
// (drawn, since coverage includes edges) and neighbouring strips overlap by a
// pixel instead of leaving a gap. UVs are unchanged, so the half-pixel of extra
// coverage just repeats the clamped edge texel.
void ExpandQuadInclusive(RVert v[4])
{
    // Half-pixel outward unit vector along an edge, or (0,0) for a degenerate
    // edge (e.g. a 1px-thin sprite). Computed from the ORIGINAL corners so the
    // four corner nudges below are independent of each other — otherwise a
    // rotated/distorted quad would skew, since a later edge would read a corner
    // an earlier one already moved.
    auto unitHalf = [](const RVert& from, const RVert& to, float& nx, float& ny)
    {
        const float dx = to.x - from.x;
        const float dy = to.y - from.y;
        const float len = std::sqrt(dx * dx + dy * dy);
        if (len < 1e-3f) { nx = 0.0f; ny = 0.0f; return; }
        nx = dx / len * 0.5f;
        ny = dy / len * 0.5f;
    };
    float abx, aby, adx, ady, bcx, bcy, dcx, dcy;
    unitHalf(v[0], v[1], abx, aby);   // A->B (top)
    unitHalf(v[0], v[3], adx, ady);   // A->D (left)
    unitHalf(v[1], v[2], bcx, bcy);   // B->C (right)
    unitHalf(v[3], v[2], dcx, dcy);   // D->C (bottom)

    v[0].x += -abx - adx; v[0].y += -aby - ady;   // A: back along AB and AD
    v[1].x += abx - bcx;  v[1].y += aby - bcy;    // B: forward AB, back BC
    v[2].x += dcx + bcx;  v[2].y += dcy + bcy;    // C: forward DC and BC
    v[3].x += adx - dcx;  v[3].y += ady - dcy;    // D: forward AD, back DC
}

// Orbit-camera projection: rotate world by yaw (Y) then pitch (X), push back by
// distance, perspective divide. Matches the validated prototype.
RVert Project(const se_vec3& w, const se_camera3d& cam,
              float cosYaw, float sinYaw, float cosPitch, float sinPitch)
{
    const float x1 = cosYaw * w.x + sinYaw * w.z;
    const float z1 = -sinYaw * w.x + cosYaw * w.z;
    const float y2 = cosPitch * w.y - sinPitch * z1;
    float z2 = sinPitch * w.y + cosPitch * z1 + cam.distance;
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

void Vdp1Rasterizer::Render(const Vdp1Scene& scene, const std::vector<uint8_t>& vram,
                            const std::vector<uint8_t>& cram, se_cram_mode cramMode,
                            const se_render_opts& opts, std::vector<uint8_t>& outRgba,
                            int minPriority, int maxPriority, bool clear)
{
    const int width = scene.screenWidth;
    const int height = scene.screenHeight;
    if (clear)
    {
        outRgba.assign(static_cast<size_t>(width) * height * 4, 0);  // transparent
    }
    if (!opts.show_vdp1_sprites)
    {
        return;
    }

    for (size_t i = 0; i < scene.sprites.size(); ++i)
    {
        const se_sprite_2d& s = scene.sprites[i];
        if (static_cast<int>(s.priority) < minPriority ||
            static_cast<int>(s.priority) > maxPriority)
        {
            continue;   // outside this priority band
        }
        RVert v[4] = { { s.corners[0].x, s.corners[0].y, 0.0f },
                       { s.corners[1].x, s.corners[1].y, 0.0f },
                       { s.corners[2].x, s.corners[2].y, 0.0f },
                       { s.corners[3].x, s.corners[3].y, 0.0f } };
        ExpandQuadInclusive(v);
        const GouraudQuad& g = i < scene.gouraud.size() ? scene.gouraud[i] : GouraudQuad{};
        const DrawFx fx = i < scene.drawfx.size() ? scene.drawfx[i] : DrawFx{};
        const int32_t sc = i < scene.solidRgb555.size() ? scene.solidRgb555[i] : -1;
        const Rgba solidCol = sc >= 0 ? Rgb555ToRgba(static_cast<uint16_t>(sc)) : Rgba{};
        RasterQuad(v, s.uv, s.texture, s.transparency == SE_TRANSP_NONE,
                   vram, cram, cramMode, width, height, outRgba, nullptr, g, fx,
                   sc >= 0 ? &solidCol : nullptr);
    }
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
        const se_sprite_3d& s = scene.sprites3d[i];
        RVert v[4] = {
            Project(s.corners[0], camera, cosYaw, sinYaw, cosPitch, sinPitch),
            Project(s.corners[1], camera, cosYaw, sinYaw, cosPitch, sinPitch),
            Project(s.corners[2], camera, cosYaw, sinYaw, cosPitch, sinPitch),
            Project(s.corners[3], camera, cosYaw, sinYaw, cosPitch, sinPitch) };
        ExpandQuadInclusive(v);
        const GouraudQuad& g = i < scene.gouraud.size() ? scene.gouraud[i] : GouraudQuad{};
        const int32_t sc = i < scene.solidRgb555.size() ? scene.solidRgb555[i] : -1;
        const Rgba solidCol = sc >= 0 ? Rgb555ToRgba(static_cast<uint16_t>(sc)) : Rgba{};
        // The exploded 3D view keeps sprites opaque (no shadow/half-transparency against
        // the depth-sorted stack); only Gouraud and solid polygon fills carry over.
        RasterQuad(v, s.uv, s.texture, s.transparency == SE_TRANSP_NONE,
                   vram, cram, cramMode, width, height, outRgba, &depth, g, DrawFx{},
                   sc >= 0 ? &solidCol : nullptr);
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

    auto inTri = [&](const RVert& p0, const RVert& p1, const RVert& p2)
    {
        const float e0 = Edge(p0.x, p0.y, p1.x, p1.y, px, py);
        const float e1 = Edge(p1.x, p1.y, p2.x, p2.y, px, py);
        const float e2 = Edge(p2.x, p2.y, p0.x, p0.y, px, py);
        const bool hasNeg = (e0 < 0) || (e1 < 0) || (e2 < 0);
        const bool hasPos = (e0 > 0) || (e1 > 0) || (e2 > 0);
        return !(hasNeg && hasPos);
    };

    bool found = false;
    float bestDepth = 0.0f;
    uint32_t bestCmd = 0;
    for (const se_sprite_3d& g : scene.sprites3d)
    {
        const RVert v[4] = {
            Project(g.corners[0], camera, cosYaw, sinYaw, cosPitch, sinPitch),
            Project(g.corners[1], camera, cosYaw, sinYaw, cosPitch, sinPitch),
            Project(g.corners[2], camera, cosYaw, sinYaw, cosPitch, sinPitch),
            Project(g.corners[3], camera, cosYaw, sinYaw, cosPitch, sinPitch) };
        if (!(inTri(v[0], v[1], v[2]) || inTri(v[0], v[2], v[3])))
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
    const se_vec2& a = sprite.corners[0];
    const se_vec2& b = sprite.corners[1];
    const se_vec2& c = sprite.corners[2];
    const se_vec2& d = sprite.corners[3];

    auto inTriangle = [&](const se_vec2& p0, const se_vec2& p1, const se_vec2& p2)
    {
        // Reject a degenerate (zero-area) triangle. Its three edge functions are
        // all 0 for every point, so the sign test below would report EVERY point
        // as "inside" — letting a sprite whose screen quad has collapsed to a line
        // or point (off-screen/scaled-to-nothing, but still in the list) swallow
        // every click and shadow the real sprite underneath. An invisible sprite
        // must not be clickable.
        const float area = Edge(p0.x, p0.y, p1.x, p1.y, p2.x, p2.y);
        if (area > -1e-3f && area < 1e-3f)
        {
            return false;
        }
        const float e0 = Edge(p0.x, p0.y, p1.x, p1.y, px, py);
        const float e1 = Edge(p1.x, p1.y, p2.x, p2.y, px, py);
        const float e2 = Edge(p2.x, p2.y, p0.x, p0.y, px, py);
        const bool hasNeg = (e0 < 0) || (e1 < 0) || (e2 < 0);
        const bool hasPos = (e0 > 0) || (e1 > 0) || (e2 > 0);
        return !(hasNeg && hasPos);   // all same sign (or on edge)
    };

    return inTriangle(a, b, c) || inTriangle(a, c, d);
}

}  // namespace se
