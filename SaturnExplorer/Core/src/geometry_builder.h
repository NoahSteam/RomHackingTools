// GeometryBuilder — turns the VDP1 command table into per-sprite 2D screen-space
// quads, resolving the local-coordinate origin as it walks. Each drawable
// textured command becomes an se_sprite_2d (four corners + texture UVs) that the
// rasterizer draws and the host hit-tests. See ARCHITECTURE.md §7.
#pragma once

#include <cstdint>
#include <vector>

#include "saturnexplorer/se_types.h"

namespace se
{

struct Vdp1Scene
{
    std::vector<se_sprite_2d> sprites;
    std::vector<se_sprite_3d> sprites3d;   // same sprites, exploded along Z (§7)
    int screenWidth  = 320;   // from the system clip command, else NTSC default
    int screenHeight = 224;
};

class GeometryBuilder
{
public:
    static void Build(const std::vector<uint8_t>& vdp1Vram, Vdp1Scene& out);
};

}  // namespace se
