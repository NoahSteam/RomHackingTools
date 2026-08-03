#include "Debug/MemorySearch.h"

namespace sfe
{

int MemorySearch::TypeSize(SearchType t)
{
    switch (t)
    {
        case SearchType::U8:  case SearchType::S8:  return 1;
        case SearchType::U16: case SearchType::S16: return 2;
        case SearchType::U32: case SearchType::S32: return 4;
    }
    return 4;
}

int64_t MemorySearch::DecodeBigEndian(const uint8_t* p, SearchType t)
{
    switch (t)
    {
        case SearchType::U8:  return static_cast<int64_t>(p[0]);
        case SearchType::S8:  return static_cast<int64_t>(static_cast<int8_t>(p[0]));
        case SearchType::U16: return static_cast<int64_t>((p[0] << 8) | p[1]);
        case SearchType::S16: return static_cast<int64_t>(
                                     static_cast<int16_t>((p[0] << 8) | p[1]));
        case SearchType::U32:
        {
            const uint32_t v = (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
                               (uint32_t(p[2]) << 8) | uint32_t(p[3]);
            return static_cast<int64_t>(v);
        }
        case SearchType::S32:
        {
            const uint32_t v = (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
                               (uint32_t(p[2]) << 8) | uint32_t(p[3]);
            return static_cast<int64_t>(static_cast<int32_t>(v));
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
    out.clear();
    if (r.size == 0) return false;
    constexpr uint32_t kChunk = 0x10000;   // ReadOne caps a single request at 64 KiB
    out.reserve(r.size);
    for (uint32_t off = 0; off < r.size; off += kChunk)
    {
        const uint32_t n = (r.size - off < kChunk) ? (r.size - off) : kChunk;
        std::vector<MemoryReadRequest> req{{r.base + off, n}};
        auto res = backend.ReadMemoryBatch(req);
        if (res.empty() || !res[0].success || res[0].bytes.size() != n)
        {
            out.clear();
            return false;
        }
        out.insert(out.end(), res[0].bytes.begin(), res[0].bytes.end());
    }
    return true;
}

std::size_t MemorySearch::First(IMemoryBackend& backend,
                                const std::vector<SearchRegion>& regions, SearchType type,
                                SearchCompare cmp, int64_t operand)
{
    mActive = true;
    mType = type;
    mRegions = regions;
    mHits.clear();

    const int width = TypeSize(type);
    const bool baseline = IsRelative(cmp);   // no previous scan yet: just record everything
    for (const SearchRegion& r : mRegions)
    {
        std::vector<uint8_t> buf;
        if (!ReadRegion(backend, r, buf)) continue;
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

    const int width = TypeSize(mType);
    std::vector<SearchHit> kept;
    kept.reserve(mHits.size());
    for (const SearchRegion& r : mRegions)
    {
        std::vector<uint8_t> buf;
        const bool ok = ReadRegion(backend, r, buf);
        for (const SearchHit& h : mHits)
        {
            if (h.addr < r.base || h.addr + width > r.base + r.size) continue;  // other region
            if (!ok) { kept.push_back(h); continue; }   // unreadable this scan: leave untouched
            const int64_t cur = DecodeBigEndian(buf.data() + (h.addr - r.base), mType);
            if (Match(cmp, cur, h.value, operand)) kept.push_back({h.addr, cur});
        }
    }
    mHits.swap(kept);
    return mHits.size();
}

void MemorySearch::Reset()
{
    mActive = false;
    mHits.clear();
    mRegions.clear();
}

}  // namespace sfe
