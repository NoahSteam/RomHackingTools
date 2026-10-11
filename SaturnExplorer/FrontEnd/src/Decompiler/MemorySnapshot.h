// MemorySnapshot — the immutable copy of SH-2 memory the decompiler reads (PLAN.md A1, A2).
//
// The engine runs on its own thread for milliseconds to seconds, while the UI thread keeps
// capturing frames into the live context. So it never reads the context: the UI thread copies
// the regions SH-2 code runs from (low and high work RAM, the two Sh2Dump regions) plus both
// register files into one of these, and hands a shared_ptr to the worker. Nothing in it changes
// after Capture, so both threads may read it at once.
//
// Every result is valid only under the SnapshotKey it was produced from (A1). The key moves on
// anything that can change the bytes: another source, another frame, an in-place edit (which
// bumps the derive serial without touching SourceId), or a different region set -- including
// replacing the optional BIOS image with another one of the same size at the same path, since
// the image's contents are part of the hash.
//
// Addresses are classified by SH-2 partition before anything is folded (A2). Only the cached
// and cache-through partitions reach the bus; the cache purge / address-array / data-array
// partitions and on-chip space are not memory, and are never treated as RAM mirrors. On the
// bus only documented aliases fold: high work RAM repeats every 1 MiB across
// 0x06000000-0x07FFFFFF. Low work RAM is not assumed to repeat.
//
// No ImGui, no Ghidra: pure functions over byte vectors, testable on their own.
#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "Sh2Address.h"
#include "saturnexplorer/SaturnExplorer.h"   // se_sh2_regs

namespace sfe
{
class IMemoryBackend;

namespace decomp
{

struct SnapshotKey
{
    uint64_t sourceId = 0;        // IMemoryBackend::SourceId()
    uint64_t deriveSerial = 0;    // se_derive_serial: moves on every capture and in-place edit
    uint64_t frameNumber = 0;     // se_frame_number
    uint64_t regionSetHash = 0;   // which regions are present, and the BIOS image's contents

    bool operator==(const SnapshotKey& o) const
    {
        return sourceId == o.sourceId && deriveSerial == o.deriveSerial &&
               frameNumber == o.frameNumber && regionSetHash == o.regionSetHash;
    }
    bool operator!=(const SnapshotKey& o) const { return !(*this == o); }
};

// The partition classification and the bus fold are shared with the AI investigation's
// CanonicalTarget, so both sides apply one rule (Sh2Address.h). The names stay available here.
using sfe::Sh2Partition;
using sfe::ClassifyPartition;
using sfe::PartitionName;
using sfe::CanonicalAddress;

// What lives at a canonical bus address, for messages about memory the snapshot does not hold
// ("BIOS", "VDP2", ...).
const char* BusAreaName(uint32_t canonical);

struct MemoryRegion
{
    const char* name = "";        // "LWRAM", "HWRAM", "BIOS"
    uint32_t base = 0;            // canonical bus address
    std::vector<uint8_t> bytes;
};

// A BIOS image the user supplied (DESIGN.md 6.1). Its content hash is computed once, here,
// when the image is loaded -- never per frame -- and goes into every key built with it.
struct BiosImage
{
    std::vector<uint8_t> bytes;
    uint64_t contentHash = 0;
    static std::shared_ptr<const BiosImage> Make(std::vector<uint8_t> bytes);
};

class MemorySnapshot
{
public:
    static const uint32_t kLwramBase = 0x00200000u;
    static const uint32_t kHwramBase = 0x06000000u;
    static const uint32_t kWorkRamSize = 0x00100000u;
    static const uint32_t kBiosBase = 0x00000000u;

    SnapshotKey key;
    std::vector<MemoryRegion> regions;
    bool haveRegs[2] = { false, false };   // master, slave
    se_sh2_regs regs[2] = {};

    // Copy LWRAM and HWRAM through 'backend' (on the UI thread), plus 'bios' when given. A region
    // the backend cannot provide in full is left out -- never half-read -- and the key's region
    // hash says which were present. 'regs' / 'haveRegs' are both CPUs' register files.
    static std::shared_ptr<const MemorySnapshot> Capture(IMemoryBackend& backend,
                                                         const se_sh2_regs regs[2], const bool haveRegs[2],
                                                         uint64_t sourceId, uint64_t deriveSerial,
                                                         uint64_t frameNumber,
                                                         std::shared_ptr<const BiosImage> bios = nullptr);

    // The region hash for the regions held now (and the BIOS content hash, when present). Capture
    // fills key.regionSetHash with it; a snapshot assembled by hand calls it itself.
    uint64_t ComputeRegionSetHash() const;

    // Copy 'size' bytes starting at 'address', folding each byte's address. All or nothing: false,
    // with 'why' saying which address failed and why ("cache address array", "uncaptured memory at
    // 0x00000600 (BIOS)"), when any byte is not in a captured region.
    bool Read(uint32_t address, uint8_t* out, size_t size, std::string* why = nullptr) const;

    // The 16-bit big-endian word at 'address', or false.
    bool ReadU16(uint32_t address, uint16_t& value) const;
    bool ReadU32(uint32_t address, uint32_t& value) const;

    // The region holding canonical address 'canonical', or null.
    const MemoryRegion* RegionAt(uint32_t canonical) const;

    // The BIOS image's content hash when a BIOS region is held, 0 otherwise.
    uint64_t biosHash = 0;
};

}  // namespace decomp
}  // namespace sfe
