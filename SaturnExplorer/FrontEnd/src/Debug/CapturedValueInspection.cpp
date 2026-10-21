#include "CapturedValueInspection.h"
#include "Decompiler/MemorySnapshot.h"
#include <algorithm>
#include <cstdio>
#include <limits>

namespace sfe { namespace decomp {
namespace {
std::string Hex(uint32_t value)
{
    char text[16];
    std::snprintf(text, sizeof(text), "0x%08X", value);
    return text;
}
bool Wram(uint32_t a)
{
    return (a >= MemorySnapshot::kLwramBase && a - MemorySnapshot::kLwramBase < MemorySnapshot::kWorkRamSize) ||
           (a >= MemorySnapshot::kHwramBase && a - MemorySnapshot::kHwramBase < MemorySnapshot::kWorkRamSize);
}
// Check before MemorySnapshot::Read: that reader serves any captured region and its general
// size_t API does not reject 32-bit wrapping. Inspection permits only these bus byte locations.
bool ReadByte(const MemorySnapshot& snapshot, uint64_t address, uint8_t& value,
              bool& bios, std::string& why)
{
    if (address > std::numeric_limits<uint32_t>::max())
    {
        why = "address overflow";
        return false;
    }
    const uint32_t a = static_cast<uint32_t>(address);
    uint32_t c = 0;
    if (!CanonicalAddress(a, c, &why)) return false;
    const bool isBios = c < 0x00080000u;
    if (!Wram(c) && !isBios)
    {
        why = std::string(BusAreaName(c)) + ": device value not available from this capture";
        return false;
    }
    if (!snapshot.Read(a, &value, 1, &why)) return false;
    bios = bios || isBios;
    return true;
}
CapturedPreview Preview(const MemorySnapshot& snapshot, uint32_t address, size_t wanted,
                        bool nullCandidate)
{
    CapturedPreview result;
    if (nullCandidate && address == 0)
    {
        result.reason = "null pointer";
        return result;
    }
    const size_t count = std::min(wanted, kCapturedPreviewLimit);
    result.bytes.reserve(count);
    for (size_t i = 0; i < count; ++i)
    {
        uint8_t byte = 0;
        if (!ReadByte(snapshot, uint64_t(address) + i, byte, result.loadedBios, result.reason))
        {
            result.truncated = !result.bytes.empty();
            break;
        }
        result.bytes.push_back(byte);
        if (byte == '"' || byte == '\\') result.ascii += '\\';
        result.ascii += byte >= 32 && byte <= 126 ? char(byte) : '.';
    }
    if (wanted > count) result.truncated = true;
    return result;
}
}

CapturedPreview PreviewCapturedMemory(const MemorySnapshot& snapshot, uint32_t address,
                                     bool nullCandidate)
{
    return Preview(snapshot, address, kCapturedPreviewLimit, nullCandidate);
}

bool CanonicalCapturedWram(const MemorySnapshot& snapshot, uint32_t address, uint32_t width,
                          uint32_t& canonical, std::string* reason)
{
    std::string why;
    if (width != 1 && width != 2 && width != 4)
        why = "unsupported scalar width";
    else
    {
        uint32_t start = 0;
        if (CanonicalAddress(address, start, &why) && Wram(start))
        {
            for (uint32_t i = 0; i < width; ++i)
            {
                uint8_t byte = 0;
                bool bios = false;
                uint32_t c = 0;
                const uint64_t a = uint64_t(address) + i;
                if (!ReadByte(snapshot, a, byte, bios, why)) break;
                if (!CanonicalAddress(static_cast<uint32_t>(a), c, &why)) break;
                if (c != uint64_t(start) + i || !Wram(c))
                {
                    why = "access crosses a canonical memory boundary";
                    break;
                }
                if (i + 1 == width)
                {
                    canonical = start;
                    if (reason) reason->clear();
                    return true;
                }
            }
        }
        else if (why.empty()) why = "location is not captured work RAM";
    }
    if (reason) *reason = why;
    return false;
}

CapturedValue InspectCapturedValue(const MemorySnapshot& snapshot, const InspectionTarget& target)
{
    CapturedValue result;
    if (target.kind == InspectionTarget::Kind::ConstantAddress)
    {
        result.available = true;  // The proven constant itself does not require readable memory.
        result.hasPointer = true;
        result.pointer = target.address;
        result.bits = target.address;
        result.scalarText = Hex(target.address);
        result.preview = PreviewCapturedMemory(snapshot, target.address);
        return result;
    }
    if (target.byteSize == 0)
    {
        result.reason = "object size is unavailable";
        return result;
    }
    const bool widthSupported = target.byteSize == 1 || target.byteSize == 2 || target.byteSize == 4;
    const bool scalar = target.display != InspectionTarget::Display::RawBytes && widthSupported &&
                        (target.display != InspectionTarget::Display::Pointer || target.byteSize == 4);
    const size_t count = scalar ? target.byteSize : std::min<size_t>(target.byteSize, kCapturedPreviewLimit);
    CapturedPreview storage = Preview(snapshot, target.address, target.byteSize, false);
    if (scalar && storage.bytes.size() != count)
    {
        result.reason = storage.reason.empty() ? "incomplete captured scalar" : storage.reason;
        return result;
    }
    if (storage.bytes.empty())
    {
        result.reason = storage.reason;
        return result;
    }
    result.available = true;
    result.loadedBios = storage.loadedBios;
    result.bytes = storage.bytes;
    if (!scalar)
    {
        result.preview = std::move(storage);
        result.scalarText = "raw captured bytes";
        return result;
    }
    for (uint8_t byte : result.bytes) result.bits = (result.bits << 8) | byte;
    char text[80];
    switch (target.display)
    {
    case InspectionTarget::Display::SignedInteger:
    {
        const unsigned bitCount = target.byteSize * 8;
        const int64_t signedValue = (result.bits & (uint32_t(1) << (bitCount - 1)))
            ? int64_t(result.bits) - (int64_t(1) << bitCount) : int64_t(result.bits);
        std::snprintf(text, sizeof(text), "%s   Signed: %lld", Hex(result.bits).c_str(),
                      static_cast<long long>(signedValue));
        result.scalarText = text;
        break;
    }
    case InspectionTarget::Display::Bool:
        result.scalarText = Hex(result.bits) + (result.bits ? "   true" : "   false");
        break;
    case InspectionTarget::Display::Pointer:
        result.scalarText = Hex(result.bits);
        result.hasPointer = true;
        result.pointer = result.bits;
        result.preview = PreviewCapturedMemory(snapshot, result.pointer);
        break;
    default:
        std::snprintf(text, sizeof(text), "%s   Unsigned: %u", Hex(result.bits).c_str(), result.bits);
        result.scalarText = text;
        break;
    }
    return result;
}
} }
