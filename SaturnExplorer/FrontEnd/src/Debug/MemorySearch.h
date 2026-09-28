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

#include <cstdint>
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
    static bool ReadRegion(IMemoryBackend& backend, const SearchRegion& r,
                           std::vector<uint8_t>& out);

    bool                      mActive = false;
    WatchType                 mType = WatchType::U32;
    std::vector<SearchRegion> mRegions;
    std::vector<SearchHit>    mHits;
    std::vector<SearchRegion> mUnread;   // regions the last scan failed to read
};

}  // namespace sfe
