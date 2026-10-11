// CaptureLoadImage — the Ghidra engine's view of a MemorySnapshot.
//
// The engine pulls bytes through LoadImage::loadFill. Every address goes through the
// snapshot's A2 classification and folding; one that is not memory, or not captured, is refused.
//
// Two instances serve one Architecture, because the engine needs two different refusals and
// the request itself does not say which kind it is:
//   Fetch  the SLEIGH translator's, used only to decode instructions. Refuses with BadDataError,
//          which flow following catches: it halts flow at that address and marks the function as
//          reaching bad data, so a branch into the BIOS truncates the function instead of
//          aborting it. Keeps the first refused address and why ("uncaptured memory at
//          0x00000600 (BIOS)") for the result.
//   Data   the Architecture's own, used for values (read-only literals, jump tables, strings).
//          Refuses with DataUnavailError, which those readers catch.
// (DataUnavailError from a fetch would propagate out of flow following and abort the whole
// function; Ghidra's alternative, a bounded followFlow, leaves the branch with no target op.)
//
// Engine-side only: included by the Decompiler/*.cpp files and their tests, never by UI code.
#pragma once

// <windows.h> defines LoadImage as LoadImageA, which would rename ghidra::LoadImage in every
// header below. Nothing here wants the Win32 function.
#if defined(_WIN32) && defined(LoadImage)
#undef LoadImage
#endif

#include "loadimage.hh"

#include "Decompiler/MemorySnapshot.h"

#include <memory>
#include <string>

namespace sfe
{
namespace decomp
{

class CaptureLoadImage : public ghidra::LoadImage
{
public:
    enum class Use { Fetch, Data };

    CaptureLoadImage(std::shared_ptr<const MemorySnapshot> snapshot, Use use);

    void loadFill(ghidra::uint1* ptr, ghidra::int4 size, const ghidra::Address& addr) override;
    std::string getArchType() const override { return "saturn-capture"; }
    void adjustVma(long) override {}

    // Fetch: the first instruction the engine tried to decode from memory it cannot have, and why.
    bool HadUnavailable() const { return mUnavailable; }
    uint32_t UnavailableAddress() const { return mUnavailableAddress; }
    const std::string& UnavailableWhy() const { return mUnavailableWhy; }

private:
    std::shared_ptr<const MemorySnapshot> mSnapshot;
    Use mUse;
    bool mUnavailable = false;
    uint32_t mUnavailableAddress = 0;
    std::string mUnavailableWhy;
};

}  // namespace decomp
}  // namespace sfe
