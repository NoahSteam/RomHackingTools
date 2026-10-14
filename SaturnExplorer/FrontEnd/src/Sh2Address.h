// Sh2Address -- the SH-2 address-partition rule and the bus fold, in one place (Docs/Decompiler/PLAN.md
// A2). The decompiler's MemorySnapshot and the AI investigation's CanonicalTarget both answer "which
// bytes does this address reach?", and the answer is a safety rule (a purge-space address must never
// be read as RAM), so there is one implementation and both call it.
//
// Bits 31-29 select a partition. Only the cached (000) and cache-through (001) partitions reach the
// bus; the purge, address-array, data-array and on-chip partitions are not memory and are never
// treated as RAM mirrors. On the bus, only the documented repeat folds: high work RAM repeats every
// 1 MiB across 0x06000000-0x07FFFFFF (Docs/Saturn/MemoryLayout.txt). Nothing else is assumed to
// repeat -- low work RAM included -- and an address past the captured regions is the reader's to
// refuse, not this function's.
//
// Pure, header-and-one-source, no ImGui and no platform headers.
#pragma once

#include <cstdint>
#include <string>

namespace sfe
{

// SH-2 address partitions, selected by bits 31-29.
enum class Sh2Partition : uint8_t
{
    Cached,              // 000: bus access through the cache
    CacheThrough,        // 001: the same bus, uncached
    AssociativePurge,    // 010, and 101 as Mednafen decodes it
    CacheAddressArray,   // 011
    CacheDataArray,      // 110, and 100 as Mednafen decodes it
    OnChip,              // 111: on-chip peripherals
};

Sh2Partition ClassifyPartition(uint32_t address);
const char* PartitionName(Sh2Partition partition);

// The bus address 'address' reaches, with the documented HWRAM repeat folded. False, with 'why'
// naming the partition, for an address that is not a bus access at all. A bus address that is simply
// not captured still folds; this only answers "which bytes", and the caller's read says whether they
// are held. Bits 28-27 are kept as they are: 0x08000000 and up fold to themselves and are unmapped.
bool CanonicalAddress(uint32_t address, uint32_t& canonical, std::string* why = nullptr);

}  // namespace sfe
