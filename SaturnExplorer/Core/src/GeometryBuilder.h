// GeometryBuilder — turns the VDP1 command table into per-sprite 2D screen-space
// quads, resolving the local-coordinate origin as it walks. Each drawable
// textured command becomes an se_sprite_2d (four corners + texture UVs) that the
// rasterizer draws and the host hit-tests. See ARCHITECTURE.md §7.
#pragma once

#include <cstdint>
#include <vector>

#include "saturnexplorer/SeTypes.h"

namespace se
{

// Per-sprite gouraud shading: the four corner colors (RGB555, A,B,C,D order) the
// hardware interpolates across the quad and adds to each texel. Kept internal
// (parallel to 'sprites'/'sprites3d') rather than on the ABI sprite structs.
struct GouraudQuad
{
    bool     on = false;
    uint16_t corner[4] = { 0, 0, 0, 0 };   // RGB555 per corner A,B,C,D
};

// Per-sprite VDP1 draw-mode effect from CMDPMOD, applied at pixel-write time. Kept
// internal (parallel to 'sprites') like GouraudQuad. 'effect': 0 normal, 1 shadow
// (halve the pixel below), 2 half-luminance (halve this sprite), 3 half-transparency
// (average with the pixel below). 'mesh': checkerboard stipple.
struct DrawFx
{
    uint8_t effect = 0;
    uint8_t mesh = 0;
};

// Per-sprite VDP1 user clipping. 'enable' from CMDPMOD bit 10; 'mode' from bit 9
// (0 = draw only inside [x0,x1]x[y0,y1], 1 = draw only outside it). The rectangle is the
// most recent user-clip command's, in the same screen space as the sprite corners.
struct ClipRect
{
    uint8_t enable = 0;
    uint8_t mode = 0;
    int32_t x0 = 0, y0 = 0, x1 = 0, y1 = 0;
};

struct Vdp1Scene
{
    std::vector<se_sprite_2d> sprites;
    std::vector<se_sprite_3d> sprites3d;   // same sprites, exploded along Z (§7)
    std::vector<GouraudQuad>  gouraud;     // parallel to 'sprites' / 'sprites3d'
    std::vector<DrawFx>       drawfx;      // parallel to 'sprites'
    // Parallel to 'sprites': -1 for a textured sprite; otherwise the RGB555 color of an
    // untextured primitive (polygon fill, or polyline/line edges).
    std::vector<int32_t>      solidRgb555;
    // Parallel to 'sprites': 0 = filled quad (sprite/polygon), 1 = polyline (4 edges),
    // 2 = line (edge A-B only). Lines draw their edges rather than filling.
    std::vector<uint8_t>      primKind;
    // Parallel to 'sprites': per-sprite user clipping (CMDPMOD bit 10 enable, bit 9 mode)
    // against the rectangle set by the most recent user-clip command (VDP1 command 6).
    std::vector<ClipRect>     clip;
    int screenWidth  = 320;   // from the system clip command, else NTSC default
    int screenHeight = 224;
    bool hasSystemClip = false;   // a VDP1 system-clip command set the dimensions above
};

class GeometryBuilder
{
public:
    static void Build(const std::vector<uint8_t>& vdp1Vram, Vdp1Scene& out);
};

}  // namespace se
