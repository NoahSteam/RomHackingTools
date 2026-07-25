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

struct Vdp1Scene
{
    std::vector<se_sprite_2d> sprites;
    std::vector<se_sprite_3d> sprites3d;   // same sprites, exploded along Z (§7)
    std::vector<GouraudQuad>  gouraud;     // parallel to 'sprites' / 'sprites3d'
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
