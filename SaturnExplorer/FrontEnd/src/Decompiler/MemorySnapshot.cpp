#include "Decompiler/MemorySnapshot.h"

#include "Debug/MemoryBackend.h"
#include "Decompiler/SpecBundle.h"   // Sha256Hex

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace sfe
{
namespace decomp
{
namespace
{

const uint32_t kHwramWindowEnd = 0x08000000u;   // HWRAM's 1 MiB repeats up to here

// The first 64 bits of a SHA-256, as a number. Collisions only matter for keys, and 64 bits of
// a cryptographic hash is far beyond what a session ever compares.
uint64_t Hash64(const void* data, size_t size)
{
    const std::string hex = Sha256Hex(data, size);
    return std::strtoull(hex.substr(0, 16).c_str(), nullptr, 16);
}

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
    case 2: return Sh2Partition::AssociativePurge;
    case 3: return Sh2Partition::CacheAddressArray;
    case 4:
    case 5: return Sh2Partition::Reserved;
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
    case Sh2Partition::Reserved: return "reserved partition";
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
    if (a >= MemorySnapshot::kHwramBase && a < kHwramWindowEnd)
        a = MemorySnapshot::kHwramBase | (a & (MemorySnapshot::kWorkRamSize - 1));
    canonical = a;
    return true;
}

const char* BusAreaName(uint32_t a)
{
    if (a < 0x00100000u) return "BIOS";
    if (a < 0x00180000u) return "SMPC";
    if (a < 0x00200000u) return "backup RAM";
    if (a < 0x00300000u) return "LWRAM";
    if (a < 0x02000000u) return "unmapped";
    if (a < 0x05000000u) return "cartridge";
    if (a < 0x05A00000u) return "A-bus / CD block";
    if (a < 0x05C00000u) return "sound";
    if (a < 0x05E00000u) return "VDP1";
    if (a < 0x05FC0000u) return "VDP2";
    if (a < 0x06000000u) return "SCU";
    if (a < kHwramWindowEnd) return "HWRAM";
    return "unmapped";
}

std::shared_ptr<const BiosImage> BiosImage::Make(std::vector<uint8_t> bytes)
{
    auto image = std::make_shared<BiosImage>();
    image->contentHash = Hash64(bytes.data(), bytes.size());
    image->bytes = std::move(bytes);
    return image;
}

std::shared_ptr<const MemorySnapshot> MemorySnapshot::Capture(IMemoryBackend& backend,
                                                              const se_sh2_regs regs[2], const bool haveRegs[2],
                                                              uint64_t sourceId, uint64_t deriveSerial,
                                                              uint64_t frameNumber,
                                                              std::shared_ptr<const BiosImage> bios)
{
    auto snap = std::make_shared<MemorySnapshot>();
    struct { const char* name; uint32_t base; } work[] = { { "LWRAM", kLwramBase }, { "HWRAM", kHwramBase } };
    for (const auto& w : work)
    {
        MemoryRegion r;
        r.name = w.name;
        r.base = w.base;
        if (ReadRegionBytes(backend, w.base, kWorkRamSize, r.bytes)) snap->regions.push_back(std::move(r));
    }
    if (bios && !bios->bytes.empty())
    {
        MemoryRegion r;
        r.name = "BIOS";
        r.base = kBiosBase;
        r.bytes = bios->bytes;
        snap->regions.push_back(std::move(r));
        snap->biosHash = bios->contentHash;
    }
    for (int cpu = 0; cpu < 2; ++cpu)
    {
        snap->haveRegs[cpu] = haveRegs[cpu];
        if (haveRegs[cpu]) snap->regs[cpu] = regs[cpu];
    }
    snap->key.sourceId = sourceId;
    snap->key.deriveSerial = deriveSerial;
    snap->key.frameNumber = frameNumber;
    snap->key.regionSetHash = snap->ComputeRegionSetHash();
    return snap;
}

uint64_t MemorySnapshot::ComputeRegionSetHash() const
{
    std::string desc;
    char b[64];
    for (const MemoryRegion& r : regions)
    {
        std::snprintf(b, sizeof b, "%s@%08X+%zu;", r.name, r.base, r.bytes.size());
        desc += b;
    }
    std::snprintf(b, sizeof b, "bios=%016llX", static_cast<unsigned long long>(biosHash));
    desc += b;
    return Hash64(desc.data(), desc.size());
}

const MemoryRegion* MemorySnapshot::RegionAt(uint32_t canonical) const
{
    for (const MemoryRegion& r : regions)
        if (canonical >= r.base && canonical - r.base < r.bytes.size()) return &r;
    return nullptr;
}

bool MemorySnapshot::Read(uint32_t address, uint8_t* out, size_t size, std::string* why) const
{
    size_t done = 0;
    while (done < size)
    {
        // Folded per run, not once: a read may straddle the end of one HWRAM repeat into the
        // next, which on the bus is the start of the same 1 MiB again.
        const uint32_t a = address + static_cast<uint32_t>(done);
        uint32_t c = 0;
        if (!CanonicalAddress(a, c, why)) return false;
        const MemoryRegion* r = RegionAt(c);
        if (!r)
        {
            if (why) *why = "uncaptured memory at " + Hex8(c) + " (" + BusAreaName(c) + ")";
            return false;
        }
        const size_t off = c - r->base;
        size_t n = r->bytes.size() - off;
        if (n > size - done) n = size - done;
        std::memcpy(out + done, r->bytes.data() + off, n);
        done += n;
    }
    return true;
}

bool MemorySnapshot::ReadU16(uint32_t address, uint16_t& value) const
{
    uint8_t b[2];
    if (!Read(address, b, 2)) return false;
    value = static_cast<uint16_t>((b[0] << 8) | b[1]);
    return true;
}

bool MemorySnapshot::ReadU32(uint32_t address, uint32_t& value) const
{
    uint8_t b[4];
    if (!Read(address, b, 4)) return false;
    value = (uint32_t(b[0]) << 24) | (uint32_t(b[1]) << 16) | (uint32_t(b[2]) << 8) | b[3];
    return true;
}

}  // namespace decomp
}  // namespace sfe
