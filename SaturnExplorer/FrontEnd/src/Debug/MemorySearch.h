// MemorySearch — a Cheat-Engine-style value scanner over the debugger's memory view.
// You run a First() scan (e.g. "find every u16 equal to 100"), then repeatedly Next()
// to narrow the surviving addresses by how their value changed between scans
// (increased / decreased / unchanged / changed), or by a new absolute value. It reads
// through IMemoryBackend, so it is emulator-agnostic and unit-testable against a mock.
//
// Values are typed with WatchType (the codebase's one scalar-memory-type enum, shared with
// the Watch list and breakpoint sizing) — the numeric widths U8/S8/U16/S16/U32/S32; RGB555
// and Pointer decode as raw u16/u32. Decoding is big-endian (Saturn byte order) and compared
// as signed 64-bit, so signed and unsigned widths both compare correctly. First() scans each
// region aligned to the value width (the standard "fast scan").
#pragma once

#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

#include "Debug/MemoryBackend.h"
#include "Debug/WatchList.h"   // WatchType + WatchTypeSize (the shared scalar-type table)

namespace sfe
{

enum class SearchCompare
{
    Equal,      // value == operand
    NotEqual,   // value != operand
    Greater,    // value >  operand
    Less,       // value <  operand
    Increased,  // value >  previous scan
    Decreased,  // value <  previous scan
    Unchanged,  // value == previous scan
    Changed,    // value != previous scan
    Unknown     // keep everything (baseline for a first scan)
};

struct SearchRegion { uint32_t base = 0; uint32_t size = 0; };

// The bytes a scan runs over, copied out of the backend up front. A scan then needs no backend,
// so it can run on a worker thread while the UI keeps drawing: the snapshot is private to the
// scan and nothing else writes to it. Memory is kept per region and per 256-byte page, because
// a narrowing scan over a few hundred candidates should read the pages they sit in, not the
// whole 1 MiB bank again.
struct SearchSnapshot
{
    enum : uint32_t { kPage = 256 };   // an enumerator, so C++14 needs no out-of-line definition
    struct Region
    {
        SearchRegion         r;
        std::vector<uint8_t> data;     // r.size bytes; only pages flagged in pageOk are valid
        std::vector<uint8_t> pageOk;   // one flag per kPage bytes
        bool                 failed = false;   // some page the scan needed could not be read
    };
    std::vector<Region> regions;
};

struct SearchHit
{
    uint32_t addr = 0;
    int64_t  value = 0;   // last-seen decode
    // False when the most recent scan could not read this address, so it survived the filter
    // without being tested and 'value' is from an earlier scan. It matters most for exactly the
    // compare a user reaches for: "unchanged" over a region that failed to read reports every
    // hit in it as unchanged, when the truth is that nothing looked.
    bool     verified = true;
};

// Where a stepped scan stopped, so the next call resumes there (see ScanFirstStep).
struct SearchScanCursor
{
    std::size_t region = 0;     // First: the region being walked
    uint32_t    off = 0;        //        and the byte offset in it
    std::size_t index = 0;      // Next: the hit being tested
    std::size_t kept = 0;       //        and how many survivors are compacted so far
    bool        entered = false;
};

struct SearchScan   // what a scan produced; Complete() installs it
{
    std::vector<SearchHit>    hits;
    std::vector<SearchRegion> unread;
};

class MemorySearch
{
public:
    static int64_t DecodeBigEndian(const uint8_t* p, WatchType t);
    static bool    IsSigned(WatchType t);   // for display formatting

    // Start a fresh scan across 'regions'. Absolute compares (Equal/NotEqual/Greater/Less)
    // filter by 'operand'; relative compares and Unknown keep everything as a baseline.
    // Returns the surviving hit count.
    std::size_t First(IMemoryBackend& backend, const std::vector<SearchRegion>& regions,
                      WatchType type, SearchCompare cmp, int64_t operand);

    // Narrow the current hits against memory now. Returns the surviving hit count.
    // A no-op (hits unchanged) when there is no active scan.
    std::size_t Next(IMemoryBackend& backend, SearchCompare cmp, int64_t operand);

    // The same scan, split so the slow half can run off the UI thread:
    //   Begin*/Capture*  -- on the calling thread: touch the backend, copy the bytes
    //   Scan*            -- pure: a snapshot and the previous hits in, the new hits out;
    //                       touches no MemorySearch and no backend, so any thread may run it
    //   Complete         -- on the calling thread: install the result
    // First()/Next() above are exactly these three in a row.
    void BeginFirst(const std::vector<SearchRegion>& regions, WatchType type);
    static SearchSnapshot CaptureFirst(IMemoryBackend& backend,
                                       const std::vector<SearchRegion>& regions);
    // Reads only the pages that hold a current hit (all of them when the hits are dense).
    SearchSnapshot CaptureNext(IMemoryBackend& backend) const;
    // Hands the hit list to a scan, leaving this one empty so nothing reads it meanwhile and
    // nothing has to copy a couple of million entries.
    std::vector<SearchHit> TakeHits() { return std::move(mHits); }
    static SearchScan ScanFirst(const SearchSnapshot& snap, WatchType type, SearchCompare cmp,
                                int64_t operand);
    static SearchScan ScanNext(const SearchSnapshot& snap, WatchType type,
                               std::vector<SearchHit> previous, SearchCompare cmp,
                               int64_t operand);
    // The same two scans in slices, for a build with no threads to run them on: each call does
    // at most 'maxItems' candidates and returns false until the scan is finished, then true with
    // 'out' filled in (for ScanNextStep, 'previous' is consumed). The caller keeps the cursor and
    // the output between calls, and paces the slices to leave the UI its frame.
    static bool ScanFirstStep(const SearchSnapshot& snap, WatchType type, SearchCompare cmp,
                              int64_t operand, SearchScanCursor& cursor, SearchScan& out,
                              std::size_t maxItems);
    static bool ScanNextStep(const SearchSnapshot& snap, WatchType type, SearchCompare cmp,
                             int64_t operand, SearchScanCursor& cursor,
                             std::vector<SearchHit>& previous, SearchScan& out,
                             std::size_t maxItems);
    void Complete(SearchScan&& scan);

    void Reset();
    bool                          Active() const { return mActive; }
    WatchType                     Type()   const { return mType; }
    std::size_t                   Count()  const { return mHits.size(); }
    const std::vector<SearchHit>& Hits()   const { return mHits; }

    // Regions the most recent First()/Next() could not read. Non-empty means the scan covered
    // less memory than the region list says: on a First() those addresses were never candidates,
    // and on a Next() the hits inside them were carried over untested (see SearchHit::verified).
    // A caller that reports a hit count without this is reporting a number it cannot stand
    // behind -- a live emulator that drops a read, or a savestate missing a region, both land
    // here.
    const std::vector<SearchRegion>& UnreadRegions()    const { return mUnread; }
    bool                             LastScanPartial()  const { return !mUnread.empty(); }
    std::size_t                      UnverifiedCount()  const;

    // True on a first scan (relative compares establish a baseline rather than compare).
    static bool IsRelative(SearchCompare cmp);

private:
    static bool Match(SearchCompare cmp, int64_t cur, int64_t prev, int64_t operand);

    bool                      mActive = false;
    WatchType                 mType = WatchType::U32;
    std::vector<SearchRegion> mRegions;
    std::vector<SearchHit>    mHits;
    std::vector<SearchRegion> mUnread;   // regions the last scan failed to read
};

}  // namespace sfe
