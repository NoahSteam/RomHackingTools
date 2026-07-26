#include "GeometryBuilder.h"

#include <algorithm>

#include "ByteOrder.h"
#include "Vdp1Parser.h"

namespace se
{

namespace
{
// Depth between successive *layers* in the exploded 3D view. A sprite that does
// not overlap any earlier one stays on layer 0 (coplanar); one that overlaps
// earlier sprites is pushed back a layer. This keeps a mech's abutting tile
// strips coplanar (so they don't split into gaps under the orbit camera) while
// still pulling genuinely overlapping/stacked sprites apart. See ARCHITECTURE.md
// §7. Purely a display tunable.
constexpr float kZSpacing = 6.0f;

// Axis-aligned bounds of a sprite in screen space, plus its assigned layer.
struct PlacedSprite
{
    float minX, minY, maxX, maxY;
    uint32_t layer;
};
}

void GeometryBuilder::Build(const std::vector<uint8_t>& vram, Vdp1Scene& out)
{
    out.sprites.clear();
    out.sprites3d.clear();
    out.render.clear();
    out.screenWidth = 320;
    out.screenHeight = 224;
    out.hasSystemClip = false;

    const std::vector<uint32_t> addresses = Vdp1Walk(vram);

    int32_t originX = 0;
    int32_t originY = 0;
    int32_t userClipX0 = 0, userClipY0 = 0, userClipX1 = 0, userClipY1 = 0;
    uint32_t objectNumber = 0;
    std::vector<PlacedSprite> placed;   // bounds+layer of sprites already emitted

    for (uint32_t index = 0; index < addresses.size(); ++index)
    {
        const uint32_t a = addresses[index];
        const uint16_t ctrl = ReadBE16(vram, a + 0x00);
        const uint16_t link = ReadBE16(vram, a + 0x02);  (void)link;
        const uint16_t pmod = ReadBE16(vram, a + 0x04);
        const uint16_t colr = ReadBE16(vram, a + 0x06);
        const uint16_t srca = ReadBE16(vram, a + 0x08);
        const uint16_t size = ReadBE16(vram, a + 0x0A);
        const int32_t  xa = ReadBE16S(vram, a + 0x0C);
        const int32_t  ya = ReadBE16S(vram, a + 0x0E);
        const int32_t  xb = ReadBE16S(vram, a + 0x10);
        const int32_t  yb = ReadBE16S(vram, a + 0x12);
        const int32_t  xc = ReadBE16S(vram, a + 0x14);
        const int32_t  yc = ReadBE16S(vram, a + 0x16);
        const int32_t  xd = ReadBE16S(vram, a + 0x18);
        const int32_t  yd = ReadBE16S(vram, a + 0x1A);
        const uint16_t grda = ReadBE16(vram, a + 0x1C);   // gouraud table (words)

        const uint16_t jp   = (ctrl >> 12) & 0x7;
        const uint16_t comm = ctrl & 0xF;
        const bool skip = (jp >= 4);

        if (comm == 0xA)  // local coordinate set
        {
            originX = xa;
            originY = ya;
            continue;
        }
        if (comm == 0x6)  // user clip: (xa,ya) upper-left, (xc,yc) lower-right
        {
            userClipX0 = xa; userClipY0 = ya;
            userClipX1 = xc; userClipY1 = yc;
            continue;
        }
        if (comm == 0x9)  // system clip: lower-right defines the drawing area
        {
            if (xc > 0 && yc > 0)
            {
                out.screenWidth = xc + 1;
                out.screenHeight = yc + 1;
                out.hasSystemClip = true;
            }
            continue;
        }

        const bool textured = (comm == 0x0 || comm == 0x1 || comm == 0x2);  // normal/scaled/distorted
        const bool polygon = (comm == 0x3);    // untextured, solid-color filled quad
        const bool polyline = (comm == 0x4);   // untextured, 4 edges
        const bool line = (comm == 0x5);       // untextured, single edge A-B
        const bool untextured = polygon || polyline || line;
        if (skip || (!textured && !untextured))
        {
            continue;
        }

        uint16_t width = 0, height = 0;
        if (textured)
        {
            width  = ((size >> 8) & 0x3F) * 8;
            height = size & 0xFF;
            if (width == 0 || height == 0)
            {
                continue;
            }
        }

        // Resolve the four screen-space corners (A=TL, B=TR, C=BR, D=BL).
        se_vec2 A, B, C, D;
        if (comm == 0x0)  // normal sprite: one corner + size
        {
            A = { float(xa + originX),         float(ya + originY) };
            B = { float(xa + width + originX),  float(ya + originY) };
            C = { float(xa + width + originX),  float(ya + height + originY) };
            D = { float(xa + originX),         float(ya + height + originY) };
        }
        else if (comm == 0x1)  // scaled sprite
        {
            // CMDCTRL bits 8-11 are the "zoom point": (xa,ya) is an anchor, (xb,yb) the
            // display width/height, and (xc,yc) an alternate corner used only when a zoom
            // axis is in two-point mode. The vertical field (zp>>2) and horizontal field
            // (zp&3) each select: 0 = two-point (use the alt corner), 1 = anchor at the
            // near edge (grow by the display size), 2 = anchor centered, 3 = anchor at the
            // far edge. zp==0 reduces to the old (xa,ya)-(xc,yc) opposite-corner case.
            const unsigned zp = (ctrl >> 8) & 0xF;
            const int32_t ax = xa + originX, ay = ya + originY;   // anchor
            const int32_t cx = xc + originX, cy = yc + originY;   // alt corner
            const int32_t dw = xb, dh = yb;                       // display size (signed)
            int32_t X[4] = { ax, ax, ax, ax };   // A=TL, B=TR, C=BR, D=BL
            int32_t Y[4] = { ay, ay, ay, ay };
            switch (zp >> 2)   // vertical
            {
            case 0: Y[2] = cy; Y[3] = cy; break;
            case 1: Y[2] += dh; Y[3] += dh; break;
            case 2: Y[0] -= dh >> 1; Y[1] -= dh >> 1;
                    Y[2] += (dh + 1) >> 1; Y[3] += (dh + 1) >> 1; break;
            default: Y[0] -= dh; Y[1] -= dh; break;
            }
            switch (zp & 0x3)   // horizontal
            {
            case 0: X[1] = cx; X[2] = cx; break;
            case 1: X[1] += dw; X[2] += dw; break;
            case 2: X[0] -= dw >> 1; X[3] -= dw >> 1;
                    X[1] += (dw + 1) >> 1; X[2] += (dw + 1) >> 1; break;
            default: X[0] -= dw; X[3] -= dw; break;
            }
            A = { float(X[0]), float(Y[0]) };
            B = { float(X[1]), float(Y[1]) };
            C = { float(X[2]), float(Y[2]) };
            D = { float(X[3]), float(Y[3]) };
        }
        else if (line)  // comm 5: a single segment between endpoints A and B
        {
            A = { float(xa + originX), float(ya + originY) };
            B = { float(xb + originX), float(yb + originY) };
            C = B; D = A;   // keep the bounding box to the segment
        }
        else  // distorted (2) / polygon (3) / polyline (4): four explicit corners
        {
            A = { float(xa + originX), float(ya + originY) };
            B = { float(xb + originX), float(yb + originY) };
            C = { float(xc + originX), float(yc + originY) };
            D = { float(xd + originX), float(yd + originY) };
        }

        const se_color_mode colorMode = static_cast<se_color_mode>((pmod >> 3) & 0x7);
        const uint16_t spd = (pmod >> 6) & 0x1;
        const uint8_t flipX = (ctrl >> 4) & 0x1;
        const uint8_t flipY = (ctrl >> 5) & 0x1;

        // Bake flip into the texture UVs.
        const float u0 = flipX ? float(width)  : 0.0f;
        const float u1 = flipX ? 0.0f : float(width);
        const float v0 = flipY ? float(height) : 0.0f;
        const float v1 = flipY ? 0.0f : float(height);

        se_sprite_2d s = se_sprite_2d {};
        s.command_index = index;
        s.object_number = objectNumber++;
        s.corners[0] = A; s.corners[1] = B; s.corners[2] = C; s.corners[3] = D;
        s.uv[0] = { u0, v0 }; s.uv[1] = { u1, v0 }; s.uv[2] = { u1, v1 }; s.uv[3] = { u0, v1 };
        s.priority = 0;
        s.flip_x = flipX;
        s.flip_y = flipY;
        s.gouraud = (pmod & 0x4) ? 1 : 0;
        s.color_mode = colorMode;
        s.transparency = spd ? SE_TRANSP_NONE : SE_TRANSP_PER_PIXEL;

        // CMDPMOD draw mode: bits 0-2 are the color-calculation field (bit 2 = Gouraud,
        // handled above; bits 0-1 = 1 shadow / 2 half-luminance / 3 half-transparency),
        // bit 8 = mesh (checkerboard stipple), bit 15 = MSB-on shadow.
        const unsigned ccb = pmod & 0x7;
        const bool msbShadow = (pmod & 0x8000) != 0;
        DrawFx fx;
        fx.effect = msbShadow ? 1 : static_cast<uint8_t>(ccb & 0x3);
        fx.mesh = (pmod & 0x0100) ? 1 : 0;
        s.draw_mode = (fx.effect == 3) ? SE_DRAW_HALF_TRANS
                    : (fx.effect == 2) ? SE_DRAW_HALF_LUM
                    : (fx.effect == 1) ? SE_DRAW_SHADOW
                    : (fx.mesh)        ? SE_DRAW_MESH
                                       : SE_DRAW_NORMAL;

        s.texture.vram_address = static_cast<uint32_t>(srca) * 8;
        s.texture.width = width;
        s.texture.height = height;
        s.texture.color_mode = colorMode;
        if (colorMode == SE_COLOR_LUT_16)
        {
            s.texture.clut_address = static_cast<uint32_t>(colr) * 8;
            s.texture.palette_bank = 0;
        }
        else
        {
            s.texture.clut_address = 0;
            s.texture.palette_bank = colr;
        }

        // Per-sprite render attributes (parallel to the sprite): draw-mode effects, user
        // clip, the untextured fill color, and Gouraud corners (CMDGRDA is a word address
        // to four RGB555 colors, one per corner A,B,C,D).
        SpriteRender sr;
        sr.fx = fx;
        sr.solid = untextured;
        sr.color = colr;   // CMDCOLR as a solid RGB555 (only used when 'solid')
        sr.primKind = polyline ? 1 : line ? 2 : 0;
        sr.clip.enable = (pmod >> 10) & 0x1;
        sr.clip.mode = (pmod >> 9) & 0x1;
        sr.clip.x0 = userClipX0; sr.clip.y0 = userClipY0;
        sr.clip.x1 = userClipX1; sr.clip.y1 = userClipY1;
        sr.gouraud.on = (pmod & 0x4) != 0;
        if (sr.gouraud.on)
        {
            const uint32_t table = static_cast<uint32_t>(grda) * 8;
            for (int k = 0; k < 4; ++k)
            {
                sr.gouraud.corner[k] = ReadBE16(vram, table + static_cast<uint32_t>(k) * 2);
            }
        }

        out.sprites.push_back(s);
        out.render.push_back(sr);

        // Assign a depth layer: 0 if this sprite overlaps nothing placed so far,
        // else one past the deepest sprite it overlaps. Abutting tiles (which
        // touch but don't overlap) share a layer and stay coplanar; stacked
        // sprites separate. Uses strict inequality so shared edges don't count.
        PlacedSprite box { s.corners[0].x, s.corners[0].y, s.corners[0].x, s.corners[0].y, 0 };
        for (int k = 1; k < 4; ++k)
        {
            box.minX = std::min(box.minX, s.corners[k].x);
            box.minY = std::min(box.minY, s.corners[k].y);
            box.maxX = std::max(box.maxX, s.corners[k].x);
            box.maxY = std::max(box.maxY, s.corners[k].y);
        }
        for (const PlacedSprite& p : placed)
        {
            const bool overlap = box.minX < p.maxX && p.minX < box.maxX &&
                                 box.minY < p.maxY && p.minY < box.maxY;
            if (overlap && p.layer + 1 > box.layer)
            {
                box.layer = p.layer + 1;
            }
        }
        placed.push_back(box);

        // Same sprite in world space: screen XY centered, Z by overlap layer.
        const float cx = out.screenWidth * 0.5f;
        const float cy = out.screenHeight * 0.5f;
        const float z = box.layer * kZSpacing;
        se_sprite_3d s3 = se_sprite_3d {};
        s3.command_index = index;
        s3.object_number = s.object_number;
        for (int k = 0; k < 4; ++k)
        {
            s3.corners[k] = { s.corners[k].x - cx, -(s.corners[k].y - cy), z };
            s3.uv[k] = s.uv[k];
        }
        s3.texture = s.texture;            // self-contained: the 3D renderer and
        s3.transparency = s.transparency;  // GPU-embedding hosts need no sibling lookup
        out.sprites3d.push_back(s3);
    }
}

}  // namespace se
