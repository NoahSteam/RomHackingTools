// Vdp2Compositor — software-composites the VDP2 NBG scroll screens (the tiled
// backgrounds behind/around the VDP1 sprites) into an RGBA layer, the way the
// hardware builds each screen from its plane -> page -> pattern-name -> cell
// hierarchy. Ported from the validated Python prototype and cross-checked
// against Yabause's vidsoft.c. See ARCHITECTURE.md §7.
//
// Scope today: the four normal backgrounds (NBG0-3), cell (non-bitmap) mode,
// 1- and 2-word pattern names, and every cell color format (16/256/2048-color
// palette, RGB555, RGB888), normal and line-window clipping for NBG0-3, the real
// back-screen color (BKTA, single-colour or per-line), and per-screen color
// calculation (CCCTL/CCRN, ratio + additive blending against the layers below), the RBG0
// rotation screen (rotation parameter set A/B, coefficient tables, screen-over repeat),
// bitmap mode (NBG0/1 + RBG0), and NBG0/1 fractional scroll, zoom, and per-line
// scroll/zoom. Vertical cell scroll, mosaic, sprite-window clipping, RBG1, and RPMD
// per-dot/window parameter selection are not modeled yet.
#pragma once

#include <cstdint>
#include <vector>

#include "saturnexplorer/SeTypes.h"
#include "HardwareSnapshot.h"

namespace se
{

class Vdp2Compositor
{
public:
    // Composite the enabled NBG layers into 'outRgba' (resized to width*height*4),
    // back-to-front by VDP2 priority. Only layers whose priority is in
    // [minPriority, maxPriority] are drawn — this lets the caller split the VDP2
    // screens into those behind the VDP1 sprites and those in front of them
    // (see Context::RenderFrame). When 'clear' is true 'outRgba' is (re)sized and
    // cleared to transparent first; when false the layers composite over whatever
    // is already in 'outRgba' (a finished frame), so transparent texels leave it
    // untouched. Honors opts.show_layer[] and the BGON enable bits; a no-op when
    // the snapshot lacks VDP2 VRAM or registers.
    static void Render(const HardwareSnapshot& snapshot, const se_render_opts& opts,
                       int width, int height, std::vector<uint8_t>& outRgba,
                       int minPriority = 1, int maxPriority = 7, bool clear = true);

    // Paint the VDP2 back screen (the always-present backdrop below every screen)
    // opaquely over the whole frame, reading its colour from the BKTA table in VDP2
    // VRAM — a single colour, or one colour per display line when BKTA's per-line bit
    // is set. Call this before Render so translucent layers have a real surface to
    // blend against (color calculation composites against whatever is below). A no-op
    // when the snapshot lacks VDP2 VRAM/registers; the caller keeps its own fallback
    // backdrop for that case.
    static void RenderBackScreen(const HardwareSnapshot& snapshot, int width, int height,
                                 std::vector<uint8_t>& outRgba);

    // The sprite (VDP1) priority the core uses when interleaving VDP1 with the
    // VDP2 screens: resolved from SPCTL's sprite type + the PRISA..PRISD sprite
    // priority registers. A single per-frame value (the common case); games that
    // vary sprite priority per pixel aren't fully modeled. 0 when unavailable.
    static int SpritePriority(const HardwareSnapshot& snapshot);
};

}  // namespace se
