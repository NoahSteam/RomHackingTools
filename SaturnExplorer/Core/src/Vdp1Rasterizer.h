// Vdp1Rasterizer — software-composites the VDP1 sprite quads into the finished
// frame (RGBA), the way the hardware draws distorted sprites. Each sprite is two
// UV-mapped triangles sampled through Vdp1Color; sprites are drawn in command
// order (painter's order). See ARCHITECTURE.md §7.
#pragma once

#include <cstdint>
#include <vector>

#include "saturnexplorer/SeTypes.h"
#include "GeometryBuilder.h"
#include "PixelMixer.h"

namespace se
{

// How a VDP1 sprite pixel's priority is decided.
//
// The VDP2 sprite-priority registers (PRISA..PRISD) hold eight priority slots. A sprite pixel
// selects one of them with a "priority number" encoded in the framebuffer word itself, and which
// bits carry it depends on the SPCTL sprite type. So priority is a property of the *pixel*, not
// of the command: a single sprite whose CLUT or colour bank spans several numbers has pixels at
// several priorities, and hardware interleaves each of them with the VDP2 layers separately.
//
// Resolving one priority per sprite -- the front-most its pixels could reach -- was accurate for
// the common case where every pixel lands on the same number, and put the rest in front of VDP2
// layers that should have covered them (review findings VDP1-02 and VDP2-01).
struct SpritePriorityTable
{
    uint8_t slot[8] = { 0, 0, 0, 0, 0, 0, 0, 0 };   // PRISA..PRISD, by priority number
    int     type = 0;                               // SPCTL sprite type (0..F)
    bool    spclmd = false;                         // SPCLMD: mixed RGB/palette data
    bool    valid = false;                          // false with no VDP2 registers captured

    // Where the priority number sits in a framebuffer word, per SPCTL sprite type (VDP1 manual;
    // mirrors Yabause's Vdp1GetSpritePixelInfo). A zero mask means the type has no priority bits.
    //
    // Expressed as a field rather than folded into the extraction, because a caller that wants to
    // know which numbers a sprite *can* select -- not just the one a given pixel does -- needs to
    // know which bits are free. Types B and F carry no number and always use slot 0.
    struct Field
    {
        int      shift = 0;
        uint16_t mask = 0;
    };

    static Field FieldFor(int type)
    {
        switch (type)
        {
        case 0x0: return { 14, 0x3 };
        case 0x1: return { 13, 0x7 };
        case 0x2: return { 14, 0x1 };
        case 0x3: return { 13, 0x3 };
        case 0x4: return { 13, 0x3 };
        case 0x5: case 0x6: case 0x7: return { 12, 0x7 };
        case 0x8: case 0x9: return { 7, 0x1 };
        case 0xA:           return { 6, 0x3 };
        case 0xC: case 0xD: return { 7, 0x1 };
        case 0xE:           return { 6, 0x3 };
        default:            return { 0, 0x0 };
        }
    }

    // The priority NUMBER a framebuffer word encodes. A direct-RGB pixel (MSB set, mixed-colour
    // mode) carries no number and uses slot 0.
    int NumberOf(uint16_t word) const
    {
        if (spclmd && (word & 0x8000))
        {
            return 0;
        }
        const Field f = FieldFor(type);
        return (word >> f.shift) & f.mask;
    }

    // The VDP2 priority (0..7) for a framebuffer word.
    uint8_t Of(uint16_t word) const
    {
        return valid ? slot[NumberOf(word) & 0x7] : 0;
    }
};

class Vdp1Rasterizer
{
public:
    // Emit the scene's VDP1 sprites into 'cols' (one PixColumn per pixel, sized
    // width*height), each pixel at the priority its own framebuffer word selects
    // (SpritePriorityTable). Sprites are processed in command
    // order; each pixel emits a descriptor, so a sprite sits in front of a same-priority
    // NBG (which was emitted earlier). Draw-mode effects (shadow / half-luminance /
    // half-transparency) blend against the column already below, resolving it the same
    // way the final frame does (opts.show_color_calculation). Honors show_vdp1_sprites.
    static void EmitSprites(const Vdp1Scene& scene, const std::vector<uint8_t>& vram,
                            const std::vector<uint8_t>& cram, se_cram_mode cramMode,
                            const SpritePriorityTable& prios,
                            const se_render_opts& opts, std::vector<PixColumn>& cols);

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
