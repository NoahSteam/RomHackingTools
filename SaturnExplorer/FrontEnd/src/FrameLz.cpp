#include "FrameLz.h"

#include <cstring>

namespace sfe
{

namespace
{
constexpr int kMinMatch = 3;
constexpr int kMaxMatch = 258;       // 3..258, stored as length-3 in one byte
constexpr uint32_t kWindow = 65535;  // max back-offset (fits in 16 bits)
constexpr int kHashBits = 16;
constexpr uint32_t kHashSize = 1u << kHashBits;
constexpr int kMaxChain = 64;        // match-search effort (ratio vs speed)

inline uint32_t Hash3(const uint8_t* p)
{
    // Mix three bytes into a 16-bit bucket.
    const uint32_t v = static_cast<uint32_t>(p[0]) |
                       (static_cast<uint32_t>(p[1]) << 8) |
                       (static_cast<uint32_t>(p[2]) << 16);
    return (v * 2654435761u) >> (32 - kHashBits);
}
}  // namespace

size_t FrameLzCompress(const uint8_t* src, size_t size, std::vector<uint8_t>& out)
{
    out.clear();
    out.reserve(size / 2 + 16);
    if (size == 0)
    {
        return 0;
    }

    // Hash chains: head[h] = most recent position with hash h; prev[pos] links
    // to the previous position sharing the hash. Positions are absolute indices.
    std::vector<int32_t> head(kHashSize, -1);
    std::vector<int32_t> prev(size, -1);

    size_t pos = 0;
    while (pos < size)
    {
        uint8_t flags = 0;
        const size_t flagsPos = out.size();
        out.push_back(0);   // placeholder for this group's flag byte

        for (int bit = 0; bit < 8 && pos < size; ++bit)
        {
            int bestLen = 0;
            uint32_t bestOff = 0;
            if (pos + kMinMatch <= size)
            {
                const uint32_t h = Hash3(src + pos);
                int32_t cand = head[h];
                int chain = kMaxChain;
                const size_t maxLen = (size - pos) < static_cast<size_t>(kMaxMatch)
                                          ? (size - pos) : static_cast<size_t>(kMaxMatch);
                while (cand >= 0 && chain-- > 0)
                {
                    const uint32_t off = static_cast<uint32_t>(pos) - static_cast<uint32_t>(cand);
                    if (off == 0 || off > kWindow)
                    {
                        break;   // chain is newest-first; older ones are farther
                    }
                    // Extend the match.
                    size_t l = 0;
                    const uint8_t* a = src + cand;
                    const uint8_t* b = src + pos;
                    while (l < maxLen && a[l] == b[l])
                    {
                        ++l;
                    }
                    if (static_cast<int>(l) > bestLen)
                    {
                        bestLen = static_cast<int>(l);
                        bestOff = off;
                        if (l >= maxLen)
                        {
                            break;
                        }
                    }
                    cand = prev[cand];
                }
            }

            if (bestLen >= kMinMatch)
            {
                // Emit a match token (flag bit stays 0).
                out.push_back(static_cast<uint8_t>(bestOff & 0xFF));
                out.push_back(static_cast<uint8_t>((bestOff >> 8) & 0xFF));
                out.push_back(static_cast<uint8_t>(bestLen - kMinMatch));
                // Insert every covered position into the hash chains.
                const size_t end = pos + static_cast<size_t>(bestLen);
                for (; pos < end; ++pos)
                {
                    if (pos + kMinMatch <= size)
                    {
                        const uint32_t hh = Hash3(src + pos);
                        prev[pos] = head[hh];
                        head[hh] = static_cast<int32_t>(pos);
                    }
                }
            }
            else
            {
                flags |= static_cast<uint8_t>(1u << bit);   // literal
                out.push_back(src[pos]);
                if (pos + kMinMatch <= size)
                {
                    const uint32_t hh = Hash3(src + pos);
                    prev[pos] = head[hh];
                    head[hh] = static_cast<int32_t>(pos);
                }
                ++pos;
            }
        }
        out[flagsPos] = flags;
    }
    return out.size();
}

bool FrameLzDecompress(const uint8_t* src, size_t size, uint8_t* out, size_t outSize)
{
    size_t ip = 0;
    size_t op = 0;
    while (op < outSize)
    {
        if (ip >= size)
        {
            return false;
        }
        uint8_t flags = src[ip++];
        for (int bit = 0; bit < 8 && op < outSize; ++bit)
        {
            if (flags & (1u << bit))
            {
                if (ip >= size)
                {
                    return false;
                }
                out[op++] = src[ip++];
            }
            else
            {
                if (ip + 3 > size)
                {
                    return false;
                }
                const uint32_t off = static_cast<uint32_t>(src[ip]) |
                                     (static_cast<uint32_t>(src[ip + 1]) << 8);
                const size_t len = static_cast<size_t>(src[ip + 2]) + kMinMatch;
                ip += 3;
                if (off == 0 || off > op || op + len > outSize)
                {
                    return false;
                }
                const size_t from = op - off;
                for (size_t k = 0; k < len; ++k)   // overlapping copy is intentional
                {
                    out[op + k] = out[from + k];
                }
                op += len;
            }
        }
    }
    return op == outSize;
}

}  // namespace sfe
