// The status strings for "where did my edit go", shared by every panel that writes
// memory through IMemoryBackend (the hex editor, the Structs panel, the assembler) so the wording cannot drift.
#pragma once

#include <cstdint>
#include <string>

#include "Debug/MemoryBackend.h"

namespace sfe
{

// "Queued" is as far as a panel can know for a live source: the emulator applies the poke at its
// next frame gate and does not confirm it. An edit on a recorded frame is kept on that frame, which is
// the frame the user is looking at, so the text names it ("Kept on recorded frame #N"); no emulator has
// it until Play From Here on that frame. One the source never sees changes this view only. Each says
// which. 'recordedFrame' is the number of the recorded frame on screen (0 when not known).
inline std::string WriteDestText(IMemoryBackend::WriteDest d, uint64_t recordedFrame = 0)
{
    using Dest = IMemoryBackend::WriteDest;
    if (d == Dest::Emulator) return "Queued for emulator";
    if (d == Dest::Staged)
        return recordedFrame ? "Kept on recorded frame #" + std::to_string(static_cast<unsigned long long>(recordedFrame))
                             : std::string("Kept on this recorded frame");
    return "Modified (this view only)";
}

// The same, asking the backend which destination 'address' has and which recorded frame is up.
inline std::string WriteDestText(const IMemoryBackend& be, uint32_t address)
{
    return WriteDestText(be.WriteDestination(address), be.RecordedFrameEdits().frame);
}

}  // namespace sfe
