// Vdp2Compositor — software-composites the VDP2 NBG scroll screens (the tiled
// backgrounds behind/around the VDP1 sprites) into an RGBA layer, the way the
// hardware builds each screen from its plane -> page -> pattern-name -> cell
// hierarchy. Ported from the validated Python prototype and cross-checked
// against Yabause's vidsoft.c. See ARCHITECTURE.md §7.
//
// Scope today: the four normal backgrounds (NBG0-3), cell (non-bitmap) mode,
// 1- and 2-word pattern names, and every cell color format (16/256/2048-color
// palette, RGB555, RGB888). Rotation screens (RBG0/1), bitmap mode, line
// scroll, mosaic, window clipping and color calculation are not modeled yet.
#pragma once

#include <cstdint>
#include <vector>

#include "saturnexplorer/se_types.h"
#include "hardware_snapshot.h"

namespace se
{

class Vdp2Compositor
{
public:
    // Composite the enabled NBG layers into 'outRgba' (resized to width*height*4),
    // back-to-front by VDP2 priority. Pixels with no opaque layer are left
    // transparent (alpha 0). Honors opts.show_layer[] and the BGON enable bits;
    // a no-op (transparent fill) when the snapshot lacks VDP2 VRAM or registers.
    static void Render(const HardwareSnapshot& snapshot, const se_render_opts& opts,
                       int width, int height, std::vector<uint8_t>& outRgba);
};

}  // namespace se
