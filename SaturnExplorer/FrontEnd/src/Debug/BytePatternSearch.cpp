#include "Debug/BytePatternSearch.h"

#include <algorithm>

namespace sfe
{

BytePatternSearchResult FindBytePattern(IMemoryBackend& backend,
                                        const std::vector<SearchRegion>& regions,
                                        const std::vector<uint8_t>& pattern,
                                        std::size_t maxHits)
{
    BytePatternSearchResult out;
    if (pattern.empty()) return out;   // matching "nothing" everywhere is never what was meant

    std::vector<uint8_t> buf;
    for (const SearchRegion& r : regions)
    {
        if (r.size < pattern.size()) continue;   // cannot contain it; not an unread failure
        if (!ReadRegionBytes(backend, r.base, r.size, buf))
        {
            out.unread.push_back(r);
            continue;
        }
        // Whole region in one buffer, so a match straddling a read chunk is still found.
        std::vector<uint8_t>::const_iterator from = buf.cbegin();
        for (;;)
        {
            const std::vector<uint8_t>::const_iterator hit =
                std::search(from, buf.cend(), pattern.cbegin(), pattern.cend());
            if (hit == buf.cend()) break;
            const std::size_t off = static_cast<std::size_t>(hit - buf.cbegin());
            out.addresses.push_back(r.base + static_cast<uint32_t>(off));
            if (maxHits != 0 && out.addresses.size() >= maxHits)
            {
                out.truncated = true;
                return out;
            }
            from = hit + 1;   // +1, not +pattern.size(): overlapping occurrences count
        }
    }
    return out;
}

}  // namespace sfe
