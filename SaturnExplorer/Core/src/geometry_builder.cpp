#include "geometry_builder.h"

#include "byteorder.h"
#include "vdp1_parser.h"

namespace se
{

namespace
{
// Depth between successive sprites in the exploded 3D view. Z is driven by draw
// order here (priority comes from the write-only VDP2 SPCTL, absent from a RAM
// dump); see ARCHITECTURE.md §7. Purely a display tunable.
constexpr float kZSpacing = 3.0f;
}

void GeometryBuilder::Build(const std::vector<uint8_t>& vram, Vdp1Scene& out)
{
    out.sprites.clear();
    out.sprites3d.clear();
    out.screenWidth = 320;
    out.screenHeight = 224;

    const std::vector<uint32_t> addresses = Vdp1Walk(vram);

    int32_t originX = 0;
    int32_t originY = 0;
    uint32_t objectNumber = 0;

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

        const uint16_t jp   = (ctrl >> 12) & 0x7;
        const uint16_t comm = ctrl & 0xF;
        const bool skip = (jp >= 4);

        if (comm == 0xA)  // local coordinate set
        {
            originX = xa;
            originY = ya;
            continue;
        }
        if (comm == 0x9)  // system clip: lower-right defines the drawing area
        {
            if (xc > 0 && yc > 0)
            {
                out.screenWidth = xc + 1;
                out.screenHeight = yc + 1;
            }
            continue;
        }

        const bool textured = (comm == 0x0 || comm == 0x1 || comm == 0x2);  // normal/scaled/distorted
        if (skip || !textured)
        {
            continue;
        }

        const uint16_t width  = ((size >> 8) & 0x3F) * 8;
        const uint16_t height = size & 0xFF;
        if (width == 0 || height == 0)
        {
            continue;
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
        else if (comm == 0x1)  // scaled sprite (two-vertex): A and C opposite corners
        {
            A = { float(xa + originX), float(ya + originY) };
            B = { float(xc + originX), float(ya + originY) };
            C = { float(xc + originX), float(yc + originY) };
            D = { float(xa + originX), float(yc + originY) };
        }
        else  // distorted sprite: four explicit corners
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
        s.draw_mode = SE_DRAW_NORMAL;

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

        out.sprites.push_back(s);

        // Same sprite in world space: screen XY centered, Z by draw order.
        const float cx = out.screenWidth * 0.5f;
        const float cy = out.screenHeight * 0.5f;
        const float z = s.object_number * kZSpacing;
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
