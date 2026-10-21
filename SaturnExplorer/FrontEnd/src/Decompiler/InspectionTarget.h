// Owned metadata for a proven printed absolute object/address. No engine or runtime values.
#pragma once
#include <cstdint>
#include <string>

namespace sfe { namespace decomp {
struct InspectionTarget
{
    enum class Kind : uint8_t { AbsoluteObject, ConstantAddress };
    enum class Display : uint8_t { RawBytes, SignedInteger, UnsignedInteger, Bool, Pointer };
    Kind kind = Kind::AbsoluteObject;
    uint32_t address = 0;       // Original bus spelling, including valid address zero.
    uint32_t byteSize = 0;
    Display display = Display::RawBytes;
    std::string typeLabel;
};
} }
