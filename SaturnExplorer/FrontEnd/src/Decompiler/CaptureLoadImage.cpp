#include "Decompiler/CaptureLoadImage.h"

#include "translate.hh"   // BadDataError

#include <algorithm>
#include <cstring>
#include <iterator>
#include <sstream>

namespace sfe
{
namespace decomp
{

CaptureLoadImage::CaptureLoadImage(std::shared_ptr<const MemorySnapshot> snapshot, Use use)
    : ghidra::LoadImage("saturn-capture"), mSnapshot(std::move(snapshot)), mUse(use)
{
}

void CaptureLoadImage::SetVolatile(std::vector<std::pair<uint32_t, uint32_t>> ranges)
{
    std::sort(ranges.begin(), ranges.end());
    mVolatile = std::move(ranges);
}

void CaptureLoadImage::loadFill(ghidra::uint1* ptr, ghidra::int4 size, const ghidra::Address& addr)
{
    const uint32_t a = static_cast<uint32_t>(addr.getOffset());
    if (mUse == Use::Data && size > 0)
    {
        const uint64_t end = static_cast<uint64_t>(a) + static_cast<uint64_t>(size) - 1;
        for (const auto& v : mVolatile)
        {
            if (v.first > end) break;
            if (v.second >= a)
            {
                std::ostringstream e;
                e << "Unable to load " << size << " bytes at 0x" << std::hex << a << ": volatile memory";
                throw ghidra::DataUnavailError(e.str());
            }
        }
    }
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
    if (mRefused.size() < kMaxRefused || a < mRefused.rbegin()->first)
    {
        mRefused.emplace(a, why);
        if (mRefused.size() > kMaxRefused) mRefused.erase(std::prev(mRefused.end()));
    }
    throw ghidra::BadDataError(e.str());
}

}  // namespace decomp
}  // namespace sfe
