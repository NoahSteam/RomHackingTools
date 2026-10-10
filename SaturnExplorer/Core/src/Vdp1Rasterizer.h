// Vdp1Rasterizer — draws the VDP1 command list into a VDP1 framebuffer, the way the
// hardware draws distorted sprites (each quad is two UV-mapped triangles sampled through
// Vdp1Color, in command order), reads that framebuffer out as VDP2 sees it, and renders the
// exploded 3D view. See ARCHITECTURE.md §7.
#pragma once

#include <cstdint>
#include <vector>

#include "saturnexplorer/SeTypes.h"
#include "GeometryBuilder.h"
#include "SpriteLayer.h"

namespace se
{

// One VDP2 scroll screen as a flat textured plane in the exploded 3D view: its isolated render
// (RGBA, alpha 0 where the screen draws nothing) hung at world depth 'z', spanning
// worldWidth x worldHeight centred on the origin, the same space the sprites' quads sit in.
struct LayerPlane
{
    const uint8_t* rgba = nullptr;
    int   width = 0;        // image size in pixels
    int   height = 0;
    float worldWidth = 0;   // extent in world units (the VDP1 coordinate space)
    float worldHeight = 0;
    float z = 0;
};

class Vdp1Rasterizer
{
public:
    // The VDP1 framebuffer column (or row) a display column (or row) reads: VDP1 draws at its own
    // size, which in a hi-res mode is half the display's width and in an exclusive monitor mode half
    // its height, and every column or row is doubled at scan-out. The hit test maps clicks back with
    // the same step.
    static int FramebufferColumn(int x, int fbWidth, int displayWidth)
    {
        return static_cast<int>(static_cast<int64_t>(x) * fbWidth / displayWidth);
    }

    // Draw the scene's VDP1 commands into a VDP1 framebuffer in command order (later pixels
    // replace earlier ones; MSB-on, shadow, half-luminance and half-transparency act on the
    // framebuffer word, never on VDP2 layers), then read it out as VDP2 does into 'layer' (one
    // SpritePixel per display pixel, sized width*height): each word resolved through the SPCTL
    // sprite type into colour, priority, colour-calculation, shadow and sprite-window state
    // (SpritePriorityTable::Resolve). Returns whether any pixel was drawn. The caller decides
    // whether the layer is shown (show_vdp1_sprites) -- the sprite-window bit is needed by the
    // VDP2 layers before the sprites themselves are emitted.
    static bool BuildSpriteLayer(const Vdp1Scene& scene, const std::vector<uint8_t>& vram,
                                 const std::vector<uint8_t>& cram, se_cram_mode cramMode,
                                 const SpritePriorityTable& prios, std::vector<SpritePixel>& layer);

    // Render the exploded 3D view (scene.sprites3d) from 'camera' into 'outRgba'
    // (resized to viewport). 'planes' are the VDP2 screens, drawn first and depth-tested like the
    // sprites, so the caller hangs them behind the sprites' Z range. 'depth' is a caller-owned
    // scratch depth buffer, reused across frames so the camera can orbit without per-frame
    // allocation. 'prios' turns a solid polygon's or line's framebuffer word into the colour VDP2
    // would show for it, which is none at all for a word VDP2 reads as transparent: the full-screen
    // colour-0 polygon many games open their list with draws nothing in the composite, and must not
    // be a black wall in front of the VDP2 planes here.
    static void Render3D(const Vdp1Scene& scene, const std::vector<uint8_t>& vram,
                         const std::vector<uint8_t>& cram, se_cram_mode cramMode,
                         const se_camera3d& camera, const se_render_opts& opts,
                         const std::vector<LayerPlane>& planes, const SpritePriorityTable& prios,
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
