#include "vdp1_rasterizer.h"

#include <algorithm>
#include <cmath>

#include "vdp1_color.h"

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

// Rasterize one UV-mapped triangle. When 'depth' is non-null, depth-test and
// write per pixel (3D view); when null, overwrite in call order (2D painter's).
void RasterTriangle(const RVert& p0, const RVert& p1, const RVert& p2,
                    const se_vec2& t0, const se_vec2& t1, const se_vec2& t2,
                    const se_texture_ref& tex, bool spd,
                    const std::vector<uint8_t>& vram, const std::vector<uint8_t>& cram,
                    se_cram_mode cramMode, int width, int height,
                    std::vector<uint8_t>& out, std::vector<float>* depth)
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

            const float u = w0 * t0.x + w1 * t1.x + w2 * t2.x;
            const float v = w0 * t0.y + w1 * t1.y + w2 * t2.y;
            const int tx = ClampInt(static_cast<int>(u), 0, texW - 1);
            const int ty = ClampInt(static_cast<int>(v), 0, texH - 1);
            const Rgba c = DecodeTexel(vram, cram, cramMode, tex.color_mode,
                                       tex.vram_address, texW, tx, ty,
                                       tex.palette_bank, tex.clut_address, spd);
            if (c.a == 0)
            {
                continue;  // transparent texel
            }

            if (depth)
            {
                (*depth)[idx] = d;
            }
            const size_t o = idx * 4;
            out[o + 0] = c.r;
            out[o + 1] = c.g;
            out[o + 2] = c.b;
            out[o + 3] = 255;
        }
    }
}

// Draw a sprite quad (two triangles A,B,C and A,C,D) into 'out'.
void RasterQuad(const RVert v[4], const se_vec2 uv[4], const se_texture_ref& tex,
                bool spd, const std::vector<uint8_t>& vram, const std::vector<uint8_t>& cram,
                se_cram_mode cramMode, int width, int height,
                std::vector<uint8_t>& out, std::vector<float>* depth)
{
    RasterTriangle(v[0], v[1], v[2], uv[0], uv[1], uv[2], tex, spd,
                   vram, cram, cramMode, width, height, out, depth);
    RasterTriangle(v[0], v[2], v[3], uv[0], uv[2], uv[3], tex, spd,
                   vram, cram, cramMode, width, height, out, depth);
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
    auto expandAlong = [](RVert& lo, RVert& hi)
    {
        const float dx = hi.x - lo.x;
        const float dy = hi.y - lo.y;
        const float len = std::sqrt(dx * dx + dy * dy);
        if (len < 1e-3f)
        {
            return;  // degenerate edge (e.g. a 1px-thin sprite); leave it be
        }
        const float nx = dx / len * 0.5f;
        const float ny = dy / len * 0.5f;
        lo.x -= nx; lo.y -= ny;
        hi.x += nx; hi.y += ny;
    };
    expandAlong(v[0], v[1]);   // top edge  A->B
    expandAlong(v[3], v[2]);   // bottom    D->C
    expandAlong(v[0], v[3]);   // left      A->D
    expandAlong(v[1], v[2]);   // right     B->C
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
                            const se_render_opts& opts, std::vector<uint8_t>& outRgba)
{
    const int width = scene.screenWidth;
    const int height = scene.screenHeight;
    outRgba.assign(static_cast<size_t>(width) * height * 4, 0);  // transparent
    if (!opts.show_vdp1_sprites)
    {
        return;
    }

    for (const se_sprite_2d& s : scene.sprites)
    {
        RVert v[4] = { { s.corners[0].x, s.corners[0].y, 0.0f },
                       { s.corners[1].x, s.corners[1].y, 0.0f },
                       { s.corners[2].x, s.corners[2].y, 0.0f },
                       { s.corners[3].x, s.corners[3].y, 0.0f } };
        ExpandQuadInclusive(v);
        RasterQuad(v, s.uv, s.texture, s.transparency == SE_TRANSP_NONE,
                   vram, cram, cramMode, width, height, outRgba, nullptr);
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

    for (const se_sprite_3d& g : scene.sprites3d)
    {
        RVert v[4] = {
            Project(g.corners[0], camera, cosYaw, sinYaw, cosPitch, sinPitch),
            Project(g.corners[1], camera, cosYaw, sinYaw, cosPitch, sinPitch),
            Project(g.corners[2], camera, cosYaw, sinYaw, cosPitch, sinPitch),
            Project(g.corners[3], camera, cosYaw, sinYaw, cosPitch, sinPitch) };
        ExpandQuadInclusive(v);
        RasterQuad(v, g.uv, g.texture, g.transparency == SE_TRANSP_NONE,
                   vram, cram, cramMode, width, height, outRgba, &depth);
    }
}

bool PointInSprite(const se_sprite_2d& sprite, float px, float py)
{
    const se_vec2& a = sprite.corners[0];
    const se_vec2& b = sprite.corners[1];
    const se_vec2& c = sprite.corners[2];
    const se_vec2& d = sprite.corners[3];

    auto inTriangle = [&](const se_vec2& p0, const se_vec2& p1, const se_vec2& p2)
    {
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
