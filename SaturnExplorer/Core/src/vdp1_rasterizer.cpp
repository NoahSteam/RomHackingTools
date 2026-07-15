#include "vdp1_rasterizer.h"

#include <algorithm>
#include <cmath>

#include "vdp1_color.h"

namespace se
{

namespace
{

float Edge(const se_vec2& a, const se_vec2& b, float px, float py)
{
    return (b.x - a.x) * (py - a.y) - (b.y - a.y) * (px - a.x);
}

int ClampInt(int v, int lo, int hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

// Rasterize one UV-mapped triangle of a sprite into the RGBA buffer.
void RasterTriangle(const se_vec2& p0, const se_vec2& p1, const se_vec2& p2,
                    const se_vec2& t0, const se_vec2& t1, const se_vec2& t2,
                    const se_sprite_2d& sprite, const std::vector<uint8_t>& vram,
                    const std::vector<uint8_t>& cram, se_cram_mode cramMode,
                    int width, int height, std::vector<uint8_t>& out)
{
    float area = Edge(p0, p1, p2.x, p2.y);
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

    const bool spd = (sprite.transparency == SE_TRANSP_NONE);
    const uint16_t texW = sprite.texture.width;
    const uint16_t texH = sprite.texture.height;

    for (int y = minY; y <= maxY; ++y)
    {
        for (int x = minX; x <= maxX; ++x)
        {
            const float px = x + 0.5f;
            const float py = y + 0.5f;
            float w0 = Edge(p1, p2, px, py) * invArea;
            float w1 = Edge(p2, p0, px, py) * invArea;
            float w2 = Edge(p0, p1, px, py) * invArea;
            if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f)
            {
                continue;  // outside this triangle
            }

            const float u = w0 * t0.x + w1 * t1.x + w2 * t2.x;
            const float v = w0 * t0.y + w1 * t1.y + w2 * t2.y;
            const int tx = ClampInt(static_cast<int>(u), 0, texW - 1);
            const int ty = ClampInt(static_cast<int>(v), 0, texH - 1);

            const Rgba c = DecodeTexel(vram, cram, cramMode, sprite.texture.color_mode,
                                       sprite.texture.vram_address, texW, tx, ty,
                                       sprite.texture.palette_bank,
                                       sprite.texture.clut_address, spd);
            if (c.a == 0)
            {
                continue;  // transparent texel
            }

            const size_t o = (static_cast<size_t>(y) * width + x) * 4;
            out[o + 0] = c.r;
            out[o + 1] = c.g;
            out[o + 2] = c.b;
            out[o + 3] = 255;
        }
    }
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
        // A,B,C then A,C,D
        RasterTriangle(s.corners[0], s.corners[1], s.corners[2],
                       s.uv[0], s.uv[1], s.uv[2],
                       s, vram, cram, cramMode, width, height, outRgba);
        RasterTriangle(s.corners[0], s.corners[2], s.corners[3],
                       s.uv[0], s.uv[2], s.uv[3],
                       s, vram, cram, cramMode, width, height, outRgba);
    }
}

namespace
{

struct Projected
{
    float x, y, depth;
};

// Orbit-camera projection: rotate world by yaw (Y) then pitch (X), push back by
// distance, perspective divide. Matches the validated prototype.
Projected Project(const se_vec3& w, const se_camera3d& cam,
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
    Projected p;
    p.x = cam.viewport_width * 0.5f + cam.fov * x1 / z2;
    p.y = cam.viewport_height * 0.5f - cam.fov * y2 / z2;
    p.depth = z2;
    return p;
}

// Depth-tested UV-mapped triangle for the 3D view.
void RasterTriangle3D(const Projected& p0, const Projected& p1, const Projected& p2,
                      const se_vec2& t0, const se_vec2& t1, const se_vec2& t2,
                      const se_sprite_2d& material, const std::vector<uint8_t>& vram,
                      const std::vector<uint8_t>& cram, se_cram_mode cramMode,
                      int width, int height, std::vector<uint8_t>& out,
                      std::vector<float>& depth)
{
    const se_vec2 a { p0.x, p0.y };
    const se_vec2 b { p1.x, p1.y };
    const se_vec2 c { p2.x, p2.y };
    float area = Edge(a, b, c.x, c.y);
    if (std::fabs(area) < 1e-6f)
    {
        return;
    }
    const float invArea = 1.0f / area;

    int minX = static_cast<int>(std::floor(std::min({ a.x, b.x, c.x })));
    int maxX = static_cast<int>(std::ceil (std::max({ a.x, b.x, c.x })));
    int minY = static_cast<int>(std::floor(std::min({ a.y, b.y, c.y })));
    int maxY = static_cast<int>(std::ceil (std::max({ a.y, b.y, c.y })));
    minX = ClampInt(minX, 0, width - 1);
    maxX = ClampInt(maxX, 0, width - 1);
    minY = ClampInt(minY, 0, height - 1);
    maxY = ClampInt(maxY, 0, height - 1);

    const bool spd = (material.transparency == SE_TRANSP_NONE);
    const uint16_t texW = material.texture.width;
    const uint16_t texH = material.texture.height;

    for (int y = minY; y <= maxY; ++y)
    {
        for (int x = minX; x <= maxX; ++x)
        {
            const float px = x + 0.5f;
            const float py = y + 0.5f;
            float w0 = Edge(b, c, px, py) * invArea;
            float w1 = Edge(c, a, px, py) * invArea;
            float w2 = Edge(a, b, px, py) * invArea;
            if (w0 < 0.0f || w1 < 0.0f || w2 < 0.0f)
            {
                continue;
            }

            const float d = w0 * p0.depth + w1 * p1.depth + w2 * p2.depth;
            const size_t idx = static_cast<size_t>(y) * width + x;
            if (d >= depth[idx])
            {
                continue;   // behind something already drawn
            }

            const float u = w0 * t0.x + w1 * t1.x + w2 * t2.x;
            const float v = w0 * t0.y + w1 * t1.y + w2 * t2.y;
            const int tx = ClampInt(static_cast<int>(u), 0, texW - 1);
            const int ty = ClampInt(static_cast<int>(v), 0, texH - 1);
            const Rgba col = DecodeTexel(vram, cram, cramMode, material.texture.color_mode,
                                         material.texture.vram_address, texW, tx, ty,
                                         material.texture.palette_bank,
                                         material.texture.clut_address, spd);
            if (col.a == 0)
            {
                continue;
            }

            depth[idx] = d;
            const size_t o = idx * 4;
            out[o + 0] = col.r;
            out[o + 1] = col.g;
            out[o + 2] = col.b;
            out[o + 3] = 255;
        }
    }
}

}  // namespace

void Vdp1Rasterizer::Render3D(const Vdp1Scene& scene, const std::vector<uint8_t>& vram,
                              const std::vector<uint8_t>& cram, se_cram_mode cramMode,
                              const se_camera3d& camera, const se_render_opts& opts,
                              std::vector<uint8_t>& outRgba)
{
    const int width = static_cast<int>(camera.viewport_width);
    const int height = static_cast<int>(camera.viewport_height);
    outRgba.assign(static_cast<size_t>(width) * height * 4, 0);
    if (width <= 0 || height <= 0 || !opts.show_vdp1_sprites)
    {
        return;
    }

    std::vector<float> depth(static_cast<size_t>(width) * height, 1e30f);
    const float cosYaw = std::cos(camera.yaw);
    const float sinYaw = std::sin(camera.yaw);
    const float cosPitch = std::cos(camera.pitch);
    const float sinPitch = std::sin(camera.pitch);

    // The 3D sprites carry geometry; reuse the matching 2D sprite for material.
    const size_t count = scene.sprites3d.size();
    for (size_t i = 0; i < count; ++i)
    {
        const se_sprite_3d& g = scene.sprites3d[i];
        const se_sprite_2d& m = scene.sprites[i];  // 1:1 with sprites3d
        Projected p[4];
        for (int k = 0; k < 4; ++k)
        {
            p[k] = Project(g.corners[k], camera, cosYaw, sinYaw, cosPitch, sinPitch);
        }
        RasterTriangle3D(p[0], p[1], p[2], g.uv[0], g.uv[1], g.uv[2],
                         m, vram, cram, cramMode, width, height, outRgba, depth);
        RasterTriangle3D(p[0], p[2], p[3], g.uv[0], g.uv[2], g.uv[3],
                         m, vram, cram, cramMode, width, height, outRgba, depth);
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
        const float e0 = Edge(p0, p1, px, py);
        const float e1 = Edge(p1, p2, px, py);
        const float e2 = Edge(p2, p0, px, py);
        const bool hasNeg = (e0 < 0) || (e1 < 0) || (e2 < 0);
        const bool hasPos = (e0 > 0) || (e1 > 0) || (e2 > 0);
        return !(hasNeg && hasPos);   // all same sign (or on edge)
    };

    return inTriangle(a, b, c) || inTriangle(a, c, d);
}

}  // namespace se
