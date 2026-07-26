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

struct Vdp1Scene
{
    std::vector<se_sprite_2d> sprites;
    std::vector<se_sprite_3d> sprites3d;   // same sprites, exploded along Z (§7)
    std::vector<GouraudQuad>  gouraud;     // parallel to 'sprites' / 'sprites3d'
    std::vector<DrawFx>       drawfx;      // parallel to 'sprites'
    // Parallel to 'sprites': -1 for a textured sprite; otherwise the RGB555 fill color of
    // an untextured polygon (VDP1 command 3), whose quad is filled with that solid color.
    std::vector<int32_t>      solidRgb555;
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
