// Bounded observations of immutable captured memory; never evaluates an SSA occurrence.
#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>
#include "Decompiler/InspectionTarget.h"

namespace sfe { namespace decomp {
class MemorySnapshot;
constexpr size_t kCapturedPreviewLimit = 16;
struct CapturedPreview
{
    std::vector<uint8_t> bytes;
    std::string ascii, reason;
    bool truncated = false;
    bool loadedBios = false;
};
struct CapturedValue
{
    bool available = false, loadedBios = false;
    std::vector<uint8_t> bytes;
    uint32_t bits = 0;
    std::string scalarText, reason;
    bool hasPointer = false;
    uint32_t pointer = 0;
    CapturedPreview preview;
};
CapturedValue InspectCapturedValue(const MemorySnapshot& snapshot, const InspectionTarget& target);
// Pointer/register candidates at zero are null. Explicit object storage at zero is permitted.
CapturedPreview PreviewCapturedMemory(const MemorySnapshot& snapshot, uint32_t address,
                                     bool nullCandidate = true);
// The existing Memory/Watch backends can represent only contiguous canonical WRAM accesses.
// A repeated-HWRAM boundary may be previewed but cannot be navigated as a contiguous scalar.
bool CanonicalCapturedWram(const MemorySnapshot& snapshot, uint32_t address, uint32_t width,
                          uint32_t& canonical, std::string* reason = nullptr);
// The same for a whole object of any size (Memory navigation only; never a scalar read): the
// range must lie in one captured WRAM region without crossing a repeated-HWRAM boundary.
bool CanonicalCapturedWramRange(const MemorySnapshot& snapshot, uint32_t address, uint32_t size,
                                uint32_t& canonical, std::string* reason = nullptr);
} }
