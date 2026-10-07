// byteorder — shared big-endian readers for Saturn VRAM/register data (the
// console is big-endian). One bounds-checked implementation used by the parser,
// geometry builder, and texel decoder instead of a private copy in each.
#pragma once

#include <cstdint>
#include <vector>

namespace se
{

inline uint16_t ReadBE16(const std::vector<uint8_t>& mem, uint32_t off)
{
    // Order the check so `off` never has 1 added to it: `off + 1` wraps to 0 at
    // off == UINT32_MAX and would sail past a non-empty vector into an OOB read.
    if (off >= mem.size() || mem.size() - off < 2)
    {
        return 0;
    }
    return static_cast<uint16_t>((mem[off] << 8) | mem[off + 1]);
}

// Sign-extend the low 'bits' of 'value'. VDP1 coordinates are not 16-bit: drawing coordinates are
// 13-bit two's complement and the local-coordinate origin is 11-bit, so 0x1FFF is -1 and a local
// 0x07FF is -1 -- reading them as int16 gives 8191 and 2047 and throws a primitive off-screen.
inline int32_t SignExtend(uint32_t value, int bits)
{
    const uint32_t sign = 1u << (bits - 1);
    value &= (sign << 1) - 1;
    return static_cast<int32_t>((value ^ sign) - sign);
}

inline int32_t ReadBE16Sx(const std::vector<uint8_t>& mem, uint32_t off, int bits)
{
    return SignExtend(ReadBE16(mem, off), bits);
}

}  // namespace se
