#include "Debug/BytePatternSearch.h"

namespace sfe
{

namespace
{
// Knuth-Morris-Pratt failure table: fail[i] is the length of the longest proper prefix of
// pattern[0..i] that is also a suffix of it. Built once per scan, then used to resume the
// match without ever re-examining a buffer byte.
std::vector<std::size_t> BuildFailureTable(const std::vector<uint8_t>& pattern)
{
    std::vector<std::size_t> fail(pattern.size(), 0);
    std::size_t k = 0;
    for (std::size_t i = 1; i < pattern.size(); ++i)
    {
        while (k > 0 && pattern[i] != pattern[k]) k = fail[k - 1];
        if (pattern[i] == pattern[k]) ++k;
        fail[i] = k;
    }
    return fail;
}
}  // namespace

BytePatternSearchResult FindBytePattern(IMemoryBackend& backend,
                                        const std::vector<SearchRegion>& regions,
                                        const std::vector<uint8_t>& pattern,
                                        std::size_t maxHits)
{
    BytePatternSearchResult out;
    if (pattern.empty()) return out;   // matching "nothing" everywhere is never what was meant

    // KMP, not a naive scan. This runs on the UI thread while the Memory panel draws, and the
    // selection may be up to 4 KiB: a pattern with a long repeated prefix (4095 x AA then BB,
    // over RAM full of AA) makes a naive matcher re-compare almost the whole pattern at almost
    // every address, which measured 1.83 s for a single MiB. KMP never re-examines a buffer
    // byte, so the scan is linear in the bytes searched regardless of what the pattern looks
    // like. The hit cap cannot help here -- it bounds the output, not the work.
    const std::vector<std::size_t> fail = BuildFailureTable(pattern);

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
        std::size_t k = 0;   // bytes of the pattern matched so far
        for (std::size_t i = 0; i < buf.size(); ++i)
        {
            while (k > 0 && buf[i] != pattern[k]) k = fail[k - 1];
            if (buf[i] == pattern[k]) ++k;
            if (k == pattern.size())
            {
                out.addresses.push_back(r.base + static_cast<uint32_t>(i + 1 - k));
                if (maxHits != 0 && out.addresses.size() >= maxHits)
                {
                    out.truncated = true;
                    return out;
                }
                // Resume as if the longest prefix-suffix were already matched, which is what
                // lets an overlapping occurrence start inside the one just reported.
                k = fail[k - 1];
            }
        }
    }
    return out;
}

}  // namespace sfe
