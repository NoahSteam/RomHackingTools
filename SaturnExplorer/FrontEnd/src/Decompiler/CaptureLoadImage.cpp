#include "Decompiler/CaptureLoadImage.h"

#include "translate.hh"   // BadDataError

#include <cstring>
#include <sstream>

namespace sfe
{
namespace decomp
{

CaptureLoadImage::CaptureLoadImage(std::shared_ptr<const MemorySnapshot> snapshot, Use use)
    : ghidra::LoadImage("saturn-capture"), mSnapshot(std::move(snapshot)), mUse(use)
{
}

void CaptureLoadImage::loadFill(ghidra::uint1* ptr, ghidra::int4 size, const ghidra::Address& addr)
{
    const uint32_t a = static_cast<uint32_t>(addr.getOffset());
    std::string why;
    // Read() leaves 'ptr' untouched when it fails, so the partial fill below starts clean.
    if (mSnapshot->Read(a, ptr, static_cast<size_t>(size), &why)) return;

    std::ostringstream e;
    e << "Unable to load " << size << " bytes at 0x" << std::hex << a << ": " << why;
    if (mUse == Use::Data) throw ghidra::DataUnavailError(e.str());

    // The SLEIGH decoder fetches 16 bytes for every instruction, so the last instructions of a
    // region would read past its end and fail to decode. When the instruction itself (its first
    // two bytes) is there, serve what exists and zero the rest: the decoder only looks at the
    // bytes the instruction occupies. Fetches only -- for data, zeroes would be a wrong value.
    int prefix = 0;
    while (prefix < size && mSnapshot->Read(a + prefix, ptr + prefix, 1)) ++prefix;
    if (prefix >= 2)
    {
        std::memset(ptr + prefix, 0, static_cast<size_t>(size - prefix));
        return;
    }
    if (!mUnavailable)
    {
        mUnavailable = true;
        mUnavailableAddress = a;
        mUnavailableWhy = why;
    }
    throw ghidra::BadDataError(e.str());
}

}  // namespace decomp
}  // namespace sfe
