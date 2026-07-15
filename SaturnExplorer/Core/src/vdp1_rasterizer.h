// Vdp1Rasterizer — software-composites the VDP1 sprite quads into the finished
// frame (RGBA), the way the hardware draws distorted sprites. Each sprite is two
// UV-mapped triangles sampled through vdp1_color; sprites are drawn in command
// order (painter's order). See ARCHITECTURE.md §7.
#pragma once

#include <cstdint>
#include <vector>

#include "saturnexplorer/se_types.h"
#include "geometry_builder.h"

namespace se
{

class Vdp1Rasterizer
{
public:
    // Render 'scene' into 'outRgba' (resized to width*height*4). Honors
    // opts.show_vdp1_sprites. 'cramMode' selects the CRAM color layout.
    static void Render(const Vdp1Scene& scene, const std::vector<uint8_t>& vram,
                       const std::vector<uint8_t>& cram, se_cram_mode cramMode,
                       const se_render_opts& opts, std::vector<uint8_t>& outRgba);

    // Render the exploded 3D view (scene.sprites3d) from 'camera' into 'outRgba'
    // (resized to viewport). Uses a depth buffer since the camera can orbit.
    static void Render3D(const Vdp1Scene& scene, const std::vector<uint8_t>& vram,
                         const std::vector<uint8_t>& cram, se_cram_mode cramMode,
                         const se_camera3d& camera, const se_render_opts& opts,
                         std::vector<uint8_t>& outRgba);
};

// True if screen point (px,py) falls inside the sprite's quad (either triangle).
bool PointInSprite(const se_sprite_2d& sprite, float px, float py);

}  // namespace se
