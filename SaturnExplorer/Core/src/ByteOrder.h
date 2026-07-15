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
    if (off + 1 >= mem.size())
    {
        return 0;
    }
    return static_cast<uint16_t>((mem[off] << 8) | mem[off + 1]);
}

inline int16_t ReadBE16S(const std::vector<uint8_t>& mem, uint32_t off)
{
    return static_cast<int16_t>(ReadBE16(mem, off));
}

}  // namespace se
