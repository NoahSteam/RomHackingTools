#include "Debug/MemorySearch.h"

namespace sfe
{

bool MemorySearch::IsSigned(WatchType t)
{
    return t == WatchType::S8 || t == WatchType::S16 || t == WatchType::S32;
}

int64_t MemorySearch::DecodeBigEndian(const uint8_t* p, WatchType t)
{
    switch (t)
    {
        case WatchType::U8:  return static_cast<int64_t>(p[0]);
        case WatchType::S8:  return static_cast<int64_t>(static_cast<int8_t>(p[0]));
        case WatchType::U16:
        case WatchType::RGB555: return static_cast<int64_t>((p[0] << 8) | p[1]);
        case WatchType::S16: return static_cast<int64_t>(
                                    static_cast<int16_t>((p[0] << 8) | p[1]));
        case WatchType::U32:
        case WatchType::S32:
        case WatchType::Pointer:
        {
            const uint32_t v = (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
                               (uint32_t(p[2]) << 8) | uint32_t(p[3]);
            return t == WatchType::S32 ? static_cast<int64_t>(static_cast<int32_t>(v))
                                       : static_cast<int64_t>(v);
        }
    }
    return 0;
}

bool MemorySearch::IsRelative(SearchCompare cmp)
{
    switch (cmp)
    {
        case SearchCompare::Increased:
        case SearchCompare::Decreased:
        case SearchCompare::Unchanged:
        case SearchCompare::Changed:
        case SearchCompare::Unknown:
            return true;
        default:
            return false;
    }
}

bool MemorySearch::Match(SearchCompare cmp, int64_t cur, int64_t prev, int64_t operand)
{
    switch (cmp)
    {
        case SearchCompare::Equal:     return cur == operand;
        case SearchCompare::NotEqual:  return cur != operand;
        case SearchCompare::Greater:   return cur >  operand;
        case SearchCompare::Less:      return cur <  operand;
        case SearchCompare::Increased: return cur >  prev;
        case SearchCompare::Decreased: return cur <  prev;
        case SearchCompare::Unchanged: return cur == prev;
        case SearchCompare::Changed:   return cur != prev;
        case SearchCompare::Unknown:   return true;
    }
    return false;
}

// Read an entire region into 'out' (big-endian Saturn bytes), coalescing into
// backend-sized chunks. Returns false (and leaves out empty) if any chunk fails.
bool MemorySearch::ReadRegion(IMemoryBackend& backend, const SearchRegion& r,
                              std::vector<uint8_t>& out)
{
    return ReadRegionBytes(backend, r.base, r.size, out);
}

std::size_t MemorySearch::First(IMemoryBackend& backend,
                                const std::vector<SearchRegion>& regions, WatchType type,
                                SearchCompare cmp, int64_t operand)
{
    mActive = true;
    mType = type;
    mRegions = regions;
    mHits.clear();
    mUnread.clear();

    const int width = (int)WatchTypeSize(type);
    if (width <= 0) return 0;
    const bool baseline = IsRelative(cmp);   // no previous scan yet: just record everything
    for (const SearchRegion& r : mRegions)
    {
        std::vector<uint8_t> buf;
        if (!ReadRegion(backend, r, buf))
        {
            // Skipping it silently reports a hit count for a search that never looked at part of
            // the range the user asked for.
            mUnread.push_back(r);
            continue;
        }
        mHits.reserve(mHits.size() + buf.size() / width);   // exact upper bound for the region
        for (uint32_t off = 0; off + width <= buf.size(); off += width)
        {
            const int64_t cur = DecodeBigEndian(buf.data() + off, type);
            if (baseline || Match(cmp, cur, cur, operand))
                mHits.push_back({r.base + off, cur});
        }
    }
    return mHits.size();
}

std::size_t MemorySearch::Next(IMemoryBackend& backend, SearchCompare cmp, int64_t operand)
{
    if (!mActive) return 0;

    const int width = (int)WatchTypeSize(mType);
    std::vector<SearchHit> kept;
    kept.reserve(mHits.size());
    mUnread.clear();
    for (const SearchRegion& r : mRegions)
    {
        std::vector<uint8_t> buf;
        const bool ok = ReadRegion(backend, r, buf);
        if (!ok) mUnread.push_back(r);
        for (const SearchHit& h : mHits)
        {
            if (h.addr < r.base || h.addr + width > r.base + r.size) continue;  // other region
            if (!ok)
            {
                // Carried over rather than dropped -- a region that failed once (a live
                // emulator between frames, say) is usually readable on the next pass, and
                // discarding the hits would throw away a narrowing the user built up. But it
                // was not tested, so it is not a result: mark it, and let the panel say so.
                SearchHit stale = h;
                stale.verified = false;
                kept.push_back(stale);
                continue;
            }
            const int64_t cur = DecodeBigEndian(buf.data() + (h.addr - r.base), mType);
            if (Match(cmp, cur, h.value, operand)) kept.push_back({h.addr, cur, true});
        }
    }
    mHits.swap(kept);
    return mHits.size();
}

std::size_t MemorySearch::UnverifiedCount() const
{
    std::size_t n = 0;
    for (const SearchHit& h : mHits)
        if (!h.verified) ++n;
    return n;
}

void MemorySearch::Reset()
{
    mActive = false;
    mHits.clear();
    mRegions.clear();
    mUnread.clear();
}

}  // namespace sfe
