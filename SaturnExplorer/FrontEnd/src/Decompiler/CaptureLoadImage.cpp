#include "Decompiler/CaptureLoadImage.h"

#include "translate.hh"   // BadDataError

#include <cstring>
#include <sstream>

namespace sfe
{
namespace decomp
{

CaptureLoadImage::CaptureLoadImage(std::shared_ptr<const MemorySnapshot> snapshot)
    : ghidra::LoadImage("saturn-capture"), mSnapshot(std::move(snapshot))
{
}

void CaptureLoadImage::loadFill(ghidra::uint1* ptr, ghidra::int4 size, const ghidra::Address& addr)
{
    const uint32_t a = static_cast<uint32_t>(addr.getOffset());
    std::string why;
    if (mSnapshot->Read(a, ptr, static_cast<size_t>(size), &why)) return;

    // The SLEIGH decoder fetches 16 bytes for every instruction, so the last instructions of a
    // region would read past its end and fail to decode. When the instruction itself (its first
    // two bytes) is there, serve what exists and zero the rest: the decoder only looks at the
    // bytes the instruction occupies. Never for data, where zeroes would be a wrong value.
    if (size == kInstructionFetch)
    {
        int prefix = 0;
        while (prefix < size && mSnapshot->Read(a + prefix, ptr + prefix, 1)) ++prefix;
        if (prefix >= 2)
        {
            std::memset(ptr + prefix, 0, static_cast<size_t>(size - prefix));
            return;
        }
    }
    std::ostringstream e;
    e << "Unable to load " << size << " bytes at 0x" << std::hex << a << ": " << why;
    // An instruction fetch -- Sleigh::resolve always asks for exactly kInstructionFetch bytes --
    // raises BadDataError: FlowInfo catches that one, ends the flow there with an artificial halt
    // and marks the function as reaching bad data, so a branch into the BIOS truncates the
    // function instead of aborting it. (DataUnavailError, its documented signal, propagates out
    // of flow following, and Ghidra's alternative, a bounded followFlow, leaves the branch with
    // no target op.) Data reads keep DataUnavailError, which the engine's value readers expect.
    if (size == kInstructionFetch)
    {
        if (!mUnavailable)
        {
            mUnavailable = true;
            mUnavailableAddress = a;
            mUnavailableWhy = why;
        }
        throw ghidra::BadDataError(e.str());
    }
    throw ghidra::DataUnavailError(e.str());
}

}  // namespace decomp
}  // namespace sfe
