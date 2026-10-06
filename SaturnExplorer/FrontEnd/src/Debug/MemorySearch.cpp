#include "Debug/MemorySearch.h"

#include <algorithm>

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

namespace
{
constexpr uint32_t kChunk = 0x10000;   // ContextBackend::ReadOne caps one request at 64 KiB

// Mark pages [first, last] of 'reg' readable or not.
void MarkPages(SearchSnapshot::Region& reg, uint32_t first, uint32_t last, bool ok)
{
    for (uint32_t p = first; p <= last && p < reg.pageOk.size(); ++p) reg.pageOk[p] = ok ? 1 : 0;
}

SearchSnapshot::Region NewRegion(const SearchRegion& r)
{
    SearchSnapshot::Region reg;
    reg.r = r;
    reg.data.assign(r.size, 0);
    reg.pageOk.assign((uint64_t(r.size) + SearchSnapshot::kPage - 1) / SearchSnapshot::kPage, 0);
    return reg;
}

// Read the byte runs [from, to) of each region named in 'runs' in one batch, and store them.
struct Run { std::size_t region; uint32_t from; uint32_t to; };   // byte offsets into the region

void ReadRuns(IMemoryBackend& backend, SearchSnapshot& snap, const std::vector<Run>& runs)
{
    // One request per <= 64 KiB piece, all in a single backend call.
    struct Piece { std::size_t run; uint32_t off; uint32_t len; };
    std::vector<MemoryReadRequest> req;
    std::vector<Piece> pieces;
    for (std::size_t i = 0; i < runs.size(); ++i)
    {
        const Run& run = runs[i];
        const SearchSnapshot::Region& reg = snap.regions[run.region];
        for (uint32_t off = run.from; off < run.to; off += kChunk)
        {
            const uint32_t n = (run.to - off < kChunk) ? (run.to - off) : kChunk;
            req.push_back({reg.r.base + off, n});
            pieces.push_back({i, off, n});
        }
    }
    if (req.empty()) return;
    const std::vector<MemoryReadResult> res = backend.ReadMemoryBatch(req);
    std::vector<char> runFailed(runs.size(), 0);
    for (std::size_t k = 0; k < pieces.size(); ++k)
    {
        const Piece& pc = pieces[k];
        const bool ok = k < res.size() && res[k].success && res[k].bytes.size() == pc.len;
        if (!ok) { runFailed[pc.run] = 1; continue; }
        SearchSnapshot::Region& reg = snap.regions[runs[pc.run].region];
        std::copy(res[k].bytes.begin(), res[k].bytes.end(), reg.data.begin() + pc.off);
    }
    // A run is all-or-nothing: one failed piece and its pages are not trusted.
    for (std::size_t i = 0; i < runs.size(); ++i)
    {
        SearchSnapshot::Region& reg = snap.regions[runs[i].region];
        const uint32_t first = runs[i].from / SearchSnapshot::kPage;
        const uint32_t last = (runs[i].to - 1) / SearchSnapshot::kPage;
        MarkPages(reg, first, last, !runFailed[i]);
        if (runFailed[i]) reg.failed = true;
    }
}
}  // namespace

SearchSnapshot MemorySearch::CaptureFirst(IMemoryBackend& backend,
                                          const std::vector<SearchRegion>& regions)
{
    SearchSnapshot snap;
    std::vector<Run> runs;
    for (const SearchRegion& r : regions)
    {
        snap.regions.push_back(NewRegion(r));
        if (r.size == 0) { snap.regions.back().failed = true; continue; }
        runs.push_back({snap.regions.size() - 1, 0, r.size});
    }
    ReadRuns(backend, snap, runs);
    return snap;
}

SearchSnapshot MemorySearch::CaptureNext(IMemoryBackend& backend) const
{
    SearchSnapshot snap;
    const uint32_t width = (uint32_t)WatchTypeSize(mType);
    std::size_t totalPages = 0;
    for (const SearchRegion& r : mRegions)
    {
        snap.regions.push_back(NewRegion(r));
        totalPages += snap.regions.back().pageOk.size();
    }

    // Which pages hold a hit. When the hits outnumber the pages there is no point working it
    // out -- nearly every page is needed -- and walking a couple of million hits here would be
    // the very cost this split exists to keep off the UI thread.
    std::vector<std::vector<char>> need(snap.regions.size());
    for (std::size_t i = 0; i < snap.regions.size(); ++i)
        need[i].assign(snap.regions[i].pageOk.size(), mHits.size() >= totalPages ? 1 : 0);
    if (mHits.size() < totalPages && width > 0)
    {
        for (const SearchHit& h : mHits)
        {
            for (std::size_t i = 0; i < mRegions.size(); ++i)
            {
                const SearchRegion& r = mRegions[i];
                if (h.addr < r.base || h.addr - r.base > r.size || r.size - (h.addr - r.base) < width)
                    continue;
                const uint32_t off = h.addr - r.base;
                need[i][off / SearchSnapshot::kPage] = 1;
                need[i][(off + width - 1) / SearchSnapshot::kPage] = 1;
                break;
            }
        }
    }

    std::vector<Run> runs;
    for (std::size_t i = 0; i < snap.regions.size(); ++i)
    {
        const SearchRegion& r = mRegions[i];
        if (r.size == 0) { snap.regions[i].failed = true; continue; }
        bool any = false;
        const uint32_t pages = (uint32_t)need[i].size();
        for (uint32_t p = 0; p < pages;)
        {
            if (!need[i][p]) { ++p; continue; }
            uint32_t q = p;
            while (q + 1 < pages && need[i][q + 1]) ++q;
            const uint32_t to = (uint32_t)std::min<uint64_t>(r.size,
                                    (uint64_t(q) + 1) * SearchSnapshot::kPage);
            runs.push_back({i, p * SearchSnapshot::kPage, to});
            any = true;
            p = q + 1;
        }
        // A region with no hits needs nothing read, but probe its first page anyway: a region
        // the backend cannot serve at all should still be reported, as it always was.
        if (!any)
            runs.push_back({i, 0, std::min<uint32_t>(r.size, uint32_t(SearchSnapshot::kPage))});
    }
    ReadRuns(backend, snap, runs);
    return snap;
}

SearchScan MemorySearch::ScanFirst(const SearchSnapshot& snap, WatchType type, SearchCompare cmp,
                                   int64_t operand)
{
    SearchScan out;
    const int width = (int)WatchTypeSize(type);
    if (width <= 0) return out;
    const bool baseline = IsRelative(cmp);   // no previous scan yet: just record everything
    for (const SearchSnapshot::Region& reg : snap.regions)
    {
        if (reg.failed)
        {
            // Skipping it silently reports a hit count for a search that never looked at part of
            // the range the user asked for.
            out.unread.push_back(reg.r);
            continue;
        }
        out.hits.reserve(out.hits.size() + reg.data.size() / width);   // exact upper bound
        for (uint32_t off = 0; off + width <= reg.data.size(); off += width)
        {
            const int64_t cur = DecodeBigEndian(reg.data.data() + off, type);
            if (baseline || Match(cmp, cur, cur, operand))
                out.hits.push_back({reg.r.base + off, cur});
        }
    }
    return out;
}

SearchScan MemorySearch::ScanNext(const SearchSnapshot& snap, WatchType type,
                                  std::vector<SearchHit> previous, SearchCompare cmp,
                                  int64_t operand)
{
    SearchScan out;
    const uint32_t width = (uint32_t)WatchTypeSize(type);
    for (const SearchSnapshot::Region& reg : snap.regions)
        if (reg.failed) out.unread.push_back(reg.r);

    // Filter in place: the survivors are compacted into the front of the vector the caller gave
    // us, so a narrowing pass over a couple of million candidates allocates nothing.
    std::size_t kept = 0;
    for (std::size_t i = 0; i < previous.size(); ++i)
    {
        const SearchHit& h = previous[i];
        const SearchSnapshot::Region* reg = nullptr;
        for (const SearchSnapshot::Region& cand : snap.regions)
        {
            if (h.addr < cand.r.base || h.addr - cand.r.base > cand.r.size ||
                cand.r.size - (h.addr - cand.r.base) < width)
                continue;
            reg = &cand;
            break;
        }
        if (!reg) continue;   // outside every region: not this scan's to keep
        const uint32_t off = h.addr - reg->r.base;
        const bool readable = !reg->failed ||
            (reg->pageOk[off / SearchSnapshot::kPage] &&
             reg->pageOk[(off + width - 1) / SearchSnapshot::kPage]);
        if (!readable)
        {
            // Carried over rather than dropped -- a region that failed once (a live emulator
            // between frames, say) is usually readable on the next pass, and discarding the
            // hits would throw away a narrowing the user built up. But it was not tested, so it
            // is not a result: mark it, and let the panel say so.
            SearchHit stale = h;
            stale.verified = false;
            previous[kept++] = stale;
            continue;
        }
        const int64_t cur = DecodeBigEndian(reg->data.data() + off, type);
        if (Match(cmp, cur, h.value, operand)) previous[kept++] = {h.addr, cur, true};
    }
    previous.resize(kept);
    out.hits = std::move(previous);
    return out;
}

void MemorySearch::BeginFirst(const std::vector<SearchRegion>& regions, WatchType type)
{
    mActive = true;
    mType = type;
    mRegions = regions;
    mHits.clear();
    mUnread.clear();
}

void MemorySearch::Complete(SearchScan&& scan)
{
    mHits = std::move(scan.hits);
    mUnread = std::move(scan.unread);
}

std::size_t MemorySearch::First(IMemoryBackend& backend,
                                const std::vector<SearchRegion>& regions, WatchType type,
                                SearchCompare cmp, int64_t operand)
{
    BeginFirst(regions, type);
    Complete(ScanFirst(CaptureFirst(backend, regions), type, cmp, operand));
    return mHits.size();
}

std::size_t MemorySearch::Next(IMemoryBackend& backend, SearchCompare cmp, int64_t operand)
{
    if (!mActive) return 0;
    const SearchSnapshot snap = CaptureNext(backend);
    Complete(ScanNext(snap, mType, TakeHits(), cmp, operand));
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
