// CaptureLoadImage — the Ghidra engine's view of a MemorySnapshot.
//
// The engine pulls bytes through LoadImage::loadFill as it follows flow. Every address goes
// through the snapshot's A2 classification and folding; one that is not memory, or not
// captured, raises an error, which is how the engine learns to stop following flow there. The
// first such instruction address and the reason are kept, so the result can say where a
// function ran off the captured memory ("uncaptured memory at 0x00000600 (BIOS)").
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
    explicit CaptureLoadImage(std::shared_ptr<const MemorySnapshot> snapshot);

    void loadFill(ghidra::uint1* ptr, ghidra::int4 size, const ghidra::Address& addr) override;
    std::string getArchType() const override { return "saturn-capture"; }
    void adjustVma(long) override {}

    // How many bytes the SLEIGH decoder fetches per instruction (Sleigh::resolve).
    static const int kInstructionFetch = 16;

    // The first instruction the engine tried to decode from memory it cannot have, and why.
    bool HadUnavailable() const { return mUnavailable; }
    uint32_t UnavailableAddress() const { return mUnavailableAddress; }
    const std::string& UnavailableWhy() const { return mUnavailableWhy; }

private:
    std::shared_ptr<const MemorySnapshot> mSnapshot;
    bool mUnavailable = false;
    uint32_t mUnavailableAddress = 0;
    std::string mUnavailableWhy;
};

}  // namespace decomp
}  // namespace sfe
