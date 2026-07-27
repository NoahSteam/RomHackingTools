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
// bitmap mode (NBG0/1 + RBG0), NBG0/1 fractional scroll, zoom, and per-line scroll/zoom,
// per-screen colour offset (CLOFEN/COxR/G/B), and horizontal mosaic. Vertical cell
// scroll, vertical mosaic, shadow, special priority/colour-calc, RBG1, and RPMD
// per-dot/window parameter selection are not modeled yet.
//
// Rather than blending straight into an RGBA buffer, every VDP2 source emits a per-pixel
// descriptor into a PixColumn (see PixelMixer.h): the back screen at priority 0, then each
// enabled NBG/RBG0 layer at its own priority. The VDP1 sprites emit into the same columns
// (Vdp1Rasterizer::EmitSprites), and Context resolves each column to one RGBA pixel. This
// two-deep column is what makes per-pixel sprite priority, cross-layer colour calculation,
// and the line-colour screen natural rather than special cases — colour calculation on
// VDP2 only ever blends the top-priority pixel with the one immediately below it.
//
// Modeled today: NBG0-3 (cell + bitmap), RBG0 rotation, fractional/line scroll + zoom,
// windows, per-screen colour calculation and colour offset, horizontal mosaic, and the
// real back screen. Sprite windows, the line-colour screen, vertical cell scroll, vertical
// mosaic, and RBG1 are not modeled yet.
#pragma once

#include <cstdint>
#include <vector>

#include "saturnexplorer/SeTypes.h"
#include "HardwareSnapshot.h"
#include "PixelMixer.h"

namespace se
{

class Vdp2Compositor
{
public:
    // Emit every enabled NBG/RBG0 layer into 'cols' (one PixColumn per pixel, sized
    // width*height) at its VDP2 priority. Layers are emitted back-to-front (ascending
    // priority, higher-numbered NBG first on ties) so a same-priority sprite emitted
    // afterwards wins the tie, exactly as VDP1 sprites sit in front of same-priority NBGs
    // on hardware. Honors opts.show_layer[] and the BGON enable bits; priority-0 layers
    // (not displayed) are skipped. A no-op when the snapshot lacks VDP2 VRAM or registers.
    static void EmitLayers(const HardwareSnapshot& snapshot, const se_render_opts& opts,
                           int width, int height, std::vector<PixColumn>& cols);

    // Seed the VDP2 back screen (the always-present backdrop below every screen) into
    // every column at priority 0, reading its colour from the BKTA table in VDP2 VRAM —
    // a single colour, or one colour per display line when BKTA's per-line bit is set.
    // Call this before EmitLayers so translucent layers have a real surface to blend
    // against (colour calculation composites against whatever is below). A no-op when the
    // snapshot lacks VDP2 VRAM/registers; the caller keeps its own fallback backdrop then.
    static void SeedBackScreen(const HardwareSnapshot& snapshot, int width, int height,
                               std::vector<PixColumn>& cols);
};

}  // namespace se
