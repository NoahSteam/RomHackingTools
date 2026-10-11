// SaturnArchitecture — the Ghidra engine configured for Saturn SH-2 code.
//
// A SleighArchitecture with the SuperH:BE:32:SH-2 language, our CaptureLoadImage as its loader,
// read-only propagation on (so literal-pool words marked read-only fold to constants, as in
// Ghidra's GUI), an instruction cap, and the host's cancel flag (PLAN.md A4).
//
// One instance is built per decompilation, on the engine thread, and destroyed before the next
// is built: the SLEIGH translator is a process-global object that every new Architecture resets,
// so two alive at once would corrupt each other. Function bodies, symbols and read-only ranges
// therefore never leak from one memory image into the next.
//
// Engine-side only, like CaptureLoadImage.
#pragma once

#include "Decompiler/CaptureLoadImage.h"   // first: it clears the Win32 LoadImage macro

#include "sleigh_arch.hh"

#include <atomic>
#include <memory>
#include <ostream>
#include <string>

namespace sfe
{
namespace decomp
{

class SaturnArchitecture : public ghidra::SleighArchitecture
{
public:
    SaturnArchitecture(std::shared_ptr<const MemorySnapshot> snapshot, std::ostream* errors);

    // init() plus the options that init would otherwise reset. Throws ghidra::LowlevelError.
    void Initialise(ghidra::uint4 maxInstructions, std::atomic<bool>* cancel);

    // The translator's load image: where flow ran into memory that is not captured.
    const CaptureLoadImage& FetchImage() const { return *mFetchImage; }

    // Materialise the embedded spec bundle under 'configDir' (SpecBundle, PLAN.md A5) and point
    // the engine's process-global spec search path at it. Once per process: later calls return
    // the first outcome. Engine thread only. False, with 'error' naming the path, when the
    // decompiler is unavailable.
    static bool StartLibrary(const std::string& configDir, std::string& error);

    // Tests only: called just before the translator is built (or reset, when reused), so a test
    // can fail an allocation inside Sleigh::reset precisely. Null, the default, everywhere else.
    static void SetBuildTranslatorHookForTests(void (*hook)());

private:
    void buildLoader(ghidra::DocumentStorage& store) override;
    ghidra::Translate* buildTranslator(ghidra::DocumentStorage& store) override;
    void resolveArchitecture() override;
    void postSpecFile() override;

    // Ours until buildLoader hands it to Architecture::loader, which deletes it; so an init that
    // fails earlier frees it here.
    std::unique_ptr<CaptureLoadImage> mDataImage;
    // Ours for the Architecture's life. The process-global translator keeps a pointer to it
    // until the next Architecture resets it, exactly as it does to a stock loader.
    std::unique_ptr<CaptureLoadImage> mFetchImage;
};

}  // namespace decomp
}  // namespace sfe
