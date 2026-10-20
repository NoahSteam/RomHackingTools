#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace sfe
{
using Vdp1CommandBytes = std::array<uint8_t, 0x20>;

inline uint16_t Vdp1MoveWord(const Vdp1CommandBytes& bytes, int offset)
{
    return static_cast<uint16_t>((bytes[offset] << 8) | bytes[offset + 1]);
}

inline int Vdp1MoveCoordinate(const Vdp1CommandBytes& bytes, int offset)
{
    const int v = Vdp1MoveWord(bytes, offset) & 0x1FFF;
    return (v & 0x1000) ? v - 0x2000 : v;
}

// Coordinate words that translate this command. Scaled-sprite B holds dimensions,
// while each axis of C is a position only when that zoom axis uses two points.
inline uint8_t Vdp1MoveAxes(const Vdp1CommandBytes& bytes)
{
    const uint16_t ctrl = Vdp1MoveWord(bytes, 0);
    if ((ctrl & 0x8000) || ((ctrl >> 12) & 7) >= 4) return 0;
    switch (ctrl & 15)
    {
    case 0: return 0x03; // A
    case 1: return static_cast<uint8_t>(0x03 | ((ctrl & 0x0300) ? 0 : 0x10) |
                                            ((ctrl & 0x0C00) ? 0 : 0x20));
    case 2: case 3: case 4: case 5: return 0xFF; // A, B, C, D
    case 6: return 0x0F; // A, B
    default: return 0;
    }
}

// Translate from the original bytes on every update, avoiding rounding drift.
// Clamp the shared delta, not individual corners, so the shape stays intact.
inline Vdp1CommandBytes MoveVdp1Command(const Vdp1CommandBytes& original, int dx, int dy)
{
    const uint8_t axes = Vdp1MoveAxes(original);
    int lo[2] = { -8192, -8192 }, hi[2] = { 8192, 8192 };
    for (int i = 0; i < 8; ++i)
        if (axes & (1u << i))
        {
            const int coordinate = Vdp1MoveCoordinate(original, 0x0C + i * 2);
            lo[i & 1] = std::max(lo[i & 1], -4096 - coordinate);
            hi[i & 1] = std::min(hi[i & 1], 4095 - coordinate);
        }
    const int delta[2] = { std::max(lo[0], std::min(dx, hi[0])),
                           std::max(lo[1], std::min(dy, hi[1])) };
    Vdp1CommandBytes result = original;
    for (int i = 0; i < 8; ++i)
        if (axes & (1u << i))
        {
            const int offset = 0x0C + i * 2;
            const uint16_t word = static_cast<uint16_t>((Vdp1MoveWord(original, offset) & 0xE000) |
                ((Vdp1MoveCoordinate(original, offset) + delta[i & 1]) & 0x1FFF));
            result[offset] = static_cast<uint8_t>(word >> 8);
            result[offset + 1] = static_cast<uint8_t>(word);
        }
    return result;
}

// Shift + a fresh left-button press on a selected sprite starts movement.
// Releasing either input or losing the target stops it until the next click.
struct Vdp1MoveGesture
{
    bool active = false, leftWasDown = false;
    float startX = 0, startY = 0;

    bool Update(bool shift, bool leftDown, bool eligible, float x, float y)
    {
        if (!shift || !leftDown || !eligible) active = false;
        const bool started = shift && leftDown && !leftWasDown && eligible;
        if (started) { active = true; startX = x; startY = y; }
        leftWasDown = leftDown;
        return started;
    }

    static int Delta(float mouse, float start, float unitsPerPixel)
    {
        const float d = (mouse - start) * unitsPerPixel;
        return static_cast<int>(std::round(std::max(-8192.0f, std::min(d, 8192.0f))));
    }
};
} // namespace sfe
