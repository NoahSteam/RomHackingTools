// The screen corners of a VDP1 scaled sprite (CMDCTRL comm 0x1). Shared by the geometry
// builder, which draws them, and the command parser, which reports the scale they imply, so
// the scale shown in the Command List is the one the renderer actually uses.
#pragma once

#include <cstdint>

namespace se
{

// CMDCTRL bits 8-11 are the "zoom point": (ax,ay) is an anchor, (dw,dh) -- vertex B -- the
// display width/height, and (cx,cy) -- vertex C -- an alternate corner used only when a zoom
// axis is in two-point mode. The vertical field (zp>>2) and horizontal field (zp&3) each
// select: 0 = two-point (use the alt corner), 1 = anchor at the near edge (grow by the display
// size), 2 = anchor centered, 3 = anchor at the far edge. zp==0 reduces to the plain
// (xa,ya)-(xc,yc) opposite-corner case. Corners are inclusive pixel coordinates, in the order
// A=TL, B=TR, C=BR, D=BL.
inline void ScaledSpriteCorners(uint16_t cmdctrl, int32_t ax, int32_t ay, int32_t cx, int32_t cy,
                                int32_t dw, int32_t dh, int32_t X[4], int32_t Y[4])
{
    const unsigned zp = (cmdctrl >> 8) & 0xF;
    for (int i = 0; i < 4; ++i) { X[i] = ax; Y[i] = ay; }
    switch (zp >> 2)   // vertical
    {
    case 0: Y[2] = cy; Y[3] = cy; break;
    case 1: Y[2] += dh; Y[3] += dh; break;
    case 2: Y[0] -= dh >> 1; Y[1] -= dh >> 1;
            Y[2] += (dh + 1) >> 1; Y[3] += (dh + 1) >> 1; break;
    default: Y[0] -= dh; Y[1] -= dh; break;
    }
    switch (zp & 0x3)   // horizontal
    {
    case 0: X[1] = cx; X[2] = cx; break;
    case 1: X[1] += dw; X[2] += dw; break;
    case 2: X[0] -= dw >> 1; X[3] -= dw >> 1;
            X[1] += (dw + 1) >> 1; X[2] += (dw + 1) >> 1; break;
    default: X[0] -= dw; X[3] -= dw; break;
    }
}

// Signed on-screen extent of one axis from its two inclusive edge coordinates: the pixel count,
// negative when the far edge lies before the near one (the sprite is drawn mirrored).
inline int32_t ScaledSpan(int32_t nearEdge, int32_t farEdge)
{
    return farEdge >= nearEdge ? farEdge - nearEdge + 1 : farEdge - nearEdge - 1;
}

}  // namespace se
