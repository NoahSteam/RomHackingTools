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
    bool    msbOn = false;   // CMDPMOD bit 15: only set the destination MSB (it overrides 'effect')
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

// All the per-sprite render attributes the rasterizer needs beyond the ABI sprite struct,
// kept internal (parallel to 'sprites'/'sprites3d') — one record per drawable command.
struct SpriteRender
{
    GouraudQuad gouraud;
    DrawFx      fx;
    ClipRect    clip;
    // The system clip in force when this command ran: the inclusive lower-right corner of the drawing
    // area, in VDP1 coordinates (unbounded before the list sets one). It bounds the framebuffer
    // writes, and is not the framebuffer's size -- the two part company in the exclusive monitor
    // modes, where the framebuffer is 240 rows whatever the clip says. A later clip command never
    // reaches back to pixels already drawn, so each primitive keeps its own.
    int32_t     sysClipX1 = 0x7FFFFFFF, sysClipY1 = 0x7FFFFFFF;
    bool        solid = false;      // untextured polygon/line: fill/edges with 'color'
    uint16_t    color = 0;          // RGB555 fill color, valid when 'solid'
    uint8_t     primKind = 0;       // 0 = filled quad, 1 = polyline (4 edges), 2 = line A-B
    bool        spdHidden = false;   // untextured with SPD clear: the hardware reads it as transparent
    bool        endCodeEnabled = false;   // textured: an end-code texel is transparent, and a
                                          // second one in a row ends the texture line
};

struct Vdp1Scene
{
    std::vector<se_sprite_2d> sprites;
    std::vector<se_sprite_3d> sprites3d;   // same sprites, exploded along Z (§7)
    std::vector<SpriteRender> render;      // parallel to 'sprites' / 'sprites3d'
    int screenWidth  = 320;   // display width (may be raised to the VDP2 TVMD dot count)
    int screenHeight = 224;
    bool hasSystemClip = false;   // a VDP1 system-clip command set the dimensions above
    // The width of the VDP1 coordinate space (the system-clip width). In hi-res modes the
    // display (screenWidth) is 2x this — VDP1 draws at half width and is doubled at
    // scan-out — so the rasterizer scales sprite X by screenWidth / vdp1Width.
    int vdp1Width = 320;
    // The same for the vertical axis, which only differs in the exclusive monitor modes: VDP1 draws
    // 240 lines and each is shown twice on the 480-line screen. 0 = the same as screenHeight.
    int vdp1Height = 0;
};

class GeometryBuilder
{
public:
    static void Build(const std::vector<uint8_t>& vdp1Vram, Vdp1Scene& out);
};

}  // namespace se
