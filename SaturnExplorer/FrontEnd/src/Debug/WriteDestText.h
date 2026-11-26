// The three status strings for "where did my edit go", shared by every panel that writes
// memory through IMemoryBackend (the hex editor, the Structs panel) so the wording cannot drift.
#pragma once

#include "Debug/MemoryBackend.h"

namespace sfe
{

// "Queued" is as far as a panel can know for a live source: the emulator applies the poke at its
// next frame gate and does not confirm it. A scrubbed frame's edit is held for Play From Here and
// no emulator has it yet; one the source never sees changes this view only. Each says which.
inline const char* WriteDestText(IMemoryBackend::WriteDest d)
{
    using Dest = IMemoryBackend::WriteDest;
    return d == Dest::Emulator ? "Queued for emulator"
         : d == Dest::Staged   ? "Staged for Play From Here"
                               : "Modified (this view only)";
}

}  // namespace sfe
