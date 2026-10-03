// BytePatternSearch -- find every occurrence of a byte sequence in the debugger's memory view.
//
// Distinct from MemorySearch, which is a Cheat-Engine-style *value* scanner: it decodes a
// typed scalar at its own width and only looks at aligned addresses. This one matches a raw
// byte sequence of any length at any alignment, which is what "I selected these bytes, where
// else do they appear?" asks for. Reads through IMemoryBackend, so it is emulator-agnostic
// and unit-testable against a mock.
//
// Matches may overlap: searching AA AA across AA AA AA reports both offset 0 and offset 1,
// because both really are occurrences and skipping the second would hide a real hit.
#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "Debug/MemoryBackend.h"
#include "Debug/MemorySearch.h"   // SearchRegion -- the one region pair this layer already has

namespace sfe
{

struct BytePatternSearchResult
{
    // Start address of each match, ascending within a region and in region order.
    std::vector<uint32_t>     addresses;
    // Regions that could not be read, so nothing in them was examined. A caller that reports
    // "no matches" without mentioning these is reporting a result it cannot stand behind --
    // the same distinction MemorySearch::UnreadRegions exists to preserve.
    std::vector<SearchRegion> unread;
    // True when the scan stopped at 'maxHits' and later matches were never collected.
    bool                      truncated = false;
};

// Scan each region for 'pattern'. An empty pattern matches nothing (rather than everything).
// A region shorter than the pattern simply yields no matches. 'maxHits' bounds the result so
// a one-byte pattern over megabytes of RAM can't build an unbounded list; 0 means unbounded.
BytePatternSearchResult FindBytePattern(IMemoryBackend& backend,
                                        const std::vector<SearchRegion>& regions,
                                        const std::vector<uint8_t>& pattern,
                                        std::size_t maxHits);

}  // namespace sfe
