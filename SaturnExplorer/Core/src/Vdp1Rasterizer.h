// Vdp1Rasterizer — software-composites the VDP1 sprite quads into the finished
// frame (RGBA), the way the hardware draws distorted sprites. Each sprite is two
// UV-mapped triangles sampled through Vdp1Color; sprites are drawn in command
// order (painter's order). See ARCHITECTURE.md §7.
#pragma once

#include <cstdint>
#include <vector>

#include "saturnexplorer/SeTypes.h"
#include "GeometryBuilder.h"

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
    // (resized to viewport). 'depth' is a caller-owned scratch depth buffer,
    // reused across frames so the camera can orbit without per-frame allocation.
    static void Render3D(const Vdp1Scene& scene, const std::vector<uint8_t>& vram,
                         const std::vector<uint8_t>& cram, se_cram_mode cramMode,
                         const se_camera3d& camera, const se_render_opts& opts,
                         std::vector<uint8_t>& outRgba, std::vector<float>& depth);

    // Pick the topmost 3D sprite under screen point (x,y) for 'camera', using the
    // exact same projection as Render3D. Returns true and writes the winning
    // sprite's command index to *outCmd; false if the point hits no sprite.
    static bool HitTest3D(const Vdp1Scene& scene, const se_camera3d& camera,
                          int x, int y, uint32_t* outCmd);
};

// True if screen point (px,py) falls inside the sprite's quad (either triangle).
bool PointInSprite(const se_sprite_2d& sprite, float px, float py);

}  // namespace se
