// CanonicalTarget -- the one way an AI investigation names a location in Saturn memory (PLAN.md §5.3).
//
// A location arrives as a bus address, as {region, offset}, or as both. All of them resolve to a
// CanonicalTarget, and that -- not the spelling the agent chose -- keys every store, every
// duplicate check and every display, so a name applied at a cache-through mirror is the name at the
// canonical address.
//
// Resolution:
//   * A bus address is first put through the decompiler's A2 rule (Sh2Address.h): only the cached
//     and cache-through partitions are memory, and the one documented repeat -- high work RAM every
//     1 MiB across 0x06000000-0x07FFFFFF -- folds to the first copy. Nothing else is assumed to
//     repeat: low work RAM does not, and an address outside every captured region is unmapped.
//   * The folded address is then looked up in the captured regions (SaturnRegions.h, the same table
//     the Memory tab and Memory Compare use). A cache-through mirror therefore folds to the cached
//     form, which is also the form the emulator's data watchpoints expect.
//   * The VDP1 frame buffer is an app-derived image with no bus writer (RegionTraits::space ==
//     DeviceImage). It is reachable only as {region, offset}; a bus address that falls in its window
//     is refused with that explanation rather than resolved, so nothing can mistake it for writable
//     SH-2 memory.
//   * {region, offset} is never folded: an offset outside the region is an error, even where a bus
//     mirror would have wrapped.
//   * When both spellings are given they must resolve to the same target, or the whole location is
//     rejected. The first disagreement is reported; nothing is "corrected".
//
// Pure functions over the region table. No ImGui, no platform headers, no I/O.
#pragma once

#include <cstdint>
#include <string>

#include "AIInvestigation/Json.h"
#include "Debug/MemoryCompare.h"   // RegionId, RegionTraits, RegionCaps

namespace sfe
{
namespace ai
{

// The lower-case wire name of a region: lwram, hwram, soundram, vdp1ram, vdp1fb, vdp1regs,
// vdp2ram, vdp2cram, vdp2regs. Derived from the region table's own name (lower-cased, spaces
// removed), so a region added there gets one automatically; a test pins the nine current ones.
const char* RegionApiName(RegionId id);
bool RegionFromApiName(const std::string& name, RegionId* out);
// Every wire name, in RegionId order, joined by 'sep' -- for messages and the prompt, so none of them
// carries a hand-copied list that goes stale when the region table grows.
std::string RegionApiNameList(const char* sep);

struct CanonicalTarget
{
    RegionId region = RegionId::Lwram;
    uint32_t offset = 0;        // within the region
    bool     hasBus = false;    // false for an app-derived image (the VDP1 frame buffer)
    uint32_t bus = 0;           // canonical bus address, meaningful when hasBus

    bool operator==(const CanonicalTarget& o) const
    {
        return region == o.region && offset == o.offset && hasBus == o.hasBus && (!hasBus || bus == o.bus);
    }
    bool operator!=(const CanonicalTarget& o) const { return !(*this == o); }
};

enum class TargetError : uint8_t
{
    None,
    Missing,          // neither a bus address nor a region/offset pair
    Malformed,        // wrong JSON shape, an unknown field, or a half-given {region, offset}
    BadEncoding,      // not "0x"-prefixed upper-case hex
    NotMemory,        // an SH-2 partition that is not a bus access (purge, address array, on-chip, ...)
    Unmapped,         // a bus address no captured region holds
    NoBusAddress,     // a bus address inside an app-derived image's window
    UnknownRegion,    // not one of the nine wire names
    OffsetOutOfRange, // offset at or past the end of the region
    Mismatch,         // bus_address and {region, offset} name different places
};

struct TargetResult
{
    bool ok = false;
    CanonicalTarget target;
    TargetError error = TargetError::None;
    std::string message;   // one line, for the response preview and error list
};

TargetResult CanonicalizeBusAddress(uint32_t busAddress);
TargetResult CanonicalizeRegionOffset(RegionId region, uint32_t offset);

// A location as an agent wrote it, before resolution.
struct TargetInput
{
    bool     hasBus = false;
    uint32_t bus = 0;
    bool     hasRegion = false;
    RegionId region = RegionId::Lwram;
    bool     hasOffset = false;
    uint32_t offset = 0;
};
TargetResult ResolveTarget(const TargetInput& input);

// Reads {"bus_address": "0x..."} and/or {"region": "...", "offset": "0x..."} from a JSON object
// (any other member is an error: a target carries nothing else) and resolves it.
TargetResult ResolveTargetJson(const JsonValue& target);

// Whether [offset, offset + length) lies inside the target's region. A zero length never fits:
// every range in this feature names at least one byte.
bool RangeFits(const CanonicalTarget& target, uint32_t length);

// Whether the target's region has every capability bit in 'caps' (RegionCaps).
bool HasCaps(const CanonicalTarget& target, uint8_t caps);

// {"region": "hwram", "offset": "0x4000", "bus_address": "0x06004000"}; no bus_address for an image.
JsonValue TargetToJson(const CanonicalTarget& target);
// "hwram+0x4000 (0x06004000)" for messages.
std::string TargetToString(const CanonicalTarget& target);

}  // namespace ai
}  // namespace sfe
