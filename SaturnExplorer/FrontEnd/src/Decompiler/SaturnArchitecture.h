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
#include <ostream>
#include <string>

namespace sfe
{
namespace decomp
{

class SaturnArchitecture : public ghidra::SleighArchitecture
{
public:
    // 'image' is owned by the Architecture once init() has run (Ghidra deletes its loader).
    SaturnArchitecture(CaptureLoadImage* image, std::ostream* errors);

    // init() plus the options that init would otherwise reset. Throws ghidra::LowlevelError.
    void Initialise(ghidra::uint4 maxInstructions, std::atomic<bool>* cancel);

    CaptureLoadImage* Image() const { return mImage; }

    // Materialise the embedded spec bundle under 'configDir' (SpecBundle, PLAN.md A5) and point
    // the engine's process-global spec search path at it. Once per process: later calls return
    // the first outcome. Engine thread only. False, with 'error' naming the path, when the
    // decompiler is unavailable.
    static bool StartLibrary(const std::string& configDir, std::string& error);

private:
    void buildLoader(ghidra::DocumentStorage& store) override;
    void resolveArchitecture() override;
    void postSpecFile() override;

    CaptureLoadImage* mImage;
};

}  // namespace decomp
}  // namespace sfe
