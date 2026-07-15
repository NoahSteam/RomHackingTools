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
