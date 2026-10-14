#include "Sh2Address.h"

#include <cstdio>

#include "SaturnRegions.h"   // kWramSize

namespace sfe
{
namespace
{

const uint32_t kHwramBase = 0x06000000u;
const uint32_t kHwramWindowEnd = 0x08000000u;   // HWRAM's 1 MiB repeats up to here

std::string Hex8(uint32_t v)
{
    char b[16];
    std::snprintf(b, sizeof b, "0x%08X", v);
    return b;
}

}  // namespace

Sh2Partition ClassifyPartition(uint32_t address)
{
    switch (address >> 29)
    {
    case 0: return Sh2Partition::Cached;
    case 1: return Sh2Partition::CacheThrough;
    // 100 and 101 are not in the SH7604's table. Mednafen, which SE runs, decodes them as the cache
    // data array and the associative purge (sh7095.inc); SaturnMemory.txt calls 101 cache-through
    // instead. Either way neither is a bus access the decompiler reads code or data from.
    case 2:
    case 5: return Sh2Partition::AssociativePurge;
    case 3: return Sh2Partition::CacheAddressArray;
    case 4:
    case 6: return Sh2Partition::CacheDataArray;
    default: return Sh2Partition::OnChip;
    }
}

const char* PartitionName(Sh2Partition partition)
{
    switch (partition)
    {
    case Sh2Partition::Cached: return "cached bus access";
    case Sh2Partition::CacheThrough: return "cache-through bus access";
    case Sh2Partition::AssociativePurge: return "associative purge";
    case Sh2Partition::CacheAddressArray: return "cache address array";
    case Sh2Partition::CacheDataArray: return "cache data array";
    case Sh2Partition::OnChip: return "on-chip peripherals";
    }
    return "?";
}

bool CanonicalAddress(uint32_t address, uint32_t& canonical, std::string* why)
{
    const Sh2Partition p = ClassifyPartition(address);
    if (p != Sh2Partition::Cached && p != Sh2Partition::CacheThrough)
    {
        if (why) *why = Hex8(address) + " is in the " + PartitionName(p) + ", not memory";
        return false;
    }
    uint32_t a = address & 0x1FFFFFFFu;
    // The one documented repeat on the bus that code runs from (Docs/Saturn/MemoryLayout.txt).
    if (a >= kHwramBase && a < kHwramWindowEnd)
        a = kHwramBase | (a & (kWramSize - 1));
    canonical = a;
    return true;
}

}  // namespace sfe
