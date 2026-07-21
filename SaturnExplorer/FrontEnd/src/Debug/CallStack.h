// CallStack — the debugger's per-CPU view of the call chain that led to the halted
// instruction (see CALL_STACK.md). Two sources feed it: a reliable shadow stack
// recorded by an instrumented emulator (Phase 2/3, every frame ● Confirmed), and a
// heuristic reconstruction from the raw stack image + registers (this phase), which
// works on a plain savestate with no emulator at all. Every frame carries the
// confidence of its source so the UI never presents a guess as ground truth.
//
// Emulator-agnostic and ImGui-free: the model + reconstructor depend only on the
// register struct and the IMemoryBackend, so they are unit-testable.
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "saturnexplorer/SaturnExplorer.h"   // se_sh2_regs
#include "Debug/MemoryBackend.h"

namespace sfe
{

enum class FrameConfidence
{
    Confirmed,   // recorded call/return event (shadow stack)    ●
    Probable,    // saved return addr preceded by a bsr/jsr       ◐
    Heuristic    // a stack word that merely looks like a pointer ○
};

struct CallStackFrame
{
    int             cpu = 0;                 // 0 master, 1 slave
    uint32_t        callSite = 0;            // the bsr/jsr that made the call (0 if unknown)
    uint32_t        functionAddress = 0;     // entry point of the frame's function (best known)
    uint32_t        returnAddress = 0;       // where this frame returns to
    uint32_t        stackPointer = 0;        // R15 at / for this frame
    uint64_t        cycle = 0;               // when the call happened (shadow stack only)
    uint32_t        frameNumber = 0;         // emulated frame of the call (shadow only)
    FrameConfidence confidence = FrameConfidence::Heuristic;
};

// Persisted address -> function-name store (the "Rename Function" action). Names that
// aren't set fall back to "sub_XXXXXX". A tiny "addr name" text file, like the assembly
// comment store.
class FunctionNames
{
public:
    // Resolved display name for a function entry: the user's rename if any, else
    // "sub_06XXXXXX".
    std::string NameOf(uint32_t address) const;
    bool        HasName(uint32_t address) const;
    void        Rename(uint32_t address, const std::string& name);  // empty clears

    void Load();
    void Save() const;

private:
    std::map<uint32_t, std::string> mNames;
};

class CallStack
{
public:
    // Heuristic reconstruction for 'cpu' from its registers + a memory image. Walks the
    // stack from R15 upward: frame #0 is the PC (returning to PR); each stack word that
    // lands in a code region becomes a caller frame, ◐ Probable when the two bytes
    // before its target decode as a call, else ○ Heuristic. Bounded in depth + span so a
    // wild SP can't run away. Replaces any existing frames for 'cpu'.
    void Reconstruct(int cpu, const se_sh2_regs& regs, IMemoryBackend& mem);

    // Install confirmed frames from a shadow stack (Phase 2). Replaces 'cpu' frames.
    void SetConfirmed(int cpu, std::vector<CallStackFrame> frames);

    void Clear(int cpu);
    void ClearAll();

    const std::vector<CallStackFrame>& Frames(int cpu) const { return mFrames[Idx(cpu)]; }
    bool Empty(int cpu) const { return mFrames[Idx(cpu)].empty(); }

    int  Selected(int cpu) const { return mSelected[Idx(cpu)]; }
    void Select(int cpu, int frame);

private:
    static int Idx(int cpu) { return (cpu == 1) ? 1 : 0; }

    std::vector<CallStackFrame> mFrames[2];
    int                         mSelected[2] = {0, 0};
};

// True when 'addr' points into a region SH-2 code plausibly lives in (HWRAM / LWRAM /
// boot ROM), even-aligned. Exposed for the reconstructor + tests.
bool IsPlausibleCodeAddress(uint32_t addr);

// Decode whether the 16-bit big-endian opcode 'op' is an SH-2 call (bsr/bsrf/jsr).
bool IsSh2CallOpcode(uint16_t op);

}  // namespace sfe
