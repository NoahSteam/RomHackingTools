// RegisterHint — where a register-target call or jump at the halt is about to go (PLAN.md Step 4).
//
// A computed call, jsr @rN with rN loaded from a table or a structure, decompiles to
// (*pfn)(...): its target is not static, and the decompiler does not pretend it is. But when the
// CPU is stopped exactly on that instruction, the register file says where it is going this time.
// That is offered as a hint ("r2 = 0x06031510 at the halt"), never folded into the output.
//
// Only at the instruction itself: before it, the register may still change; in its delay slot,
// the jump has already read it and the slot may have overwritten it.
//
// No ImGui, no Ghidra.
#pragma once

#include <cstdint>

#include "Decompiler/MemorySnapshot.h"

namespace sfe
{
namespace decomp
{

struct RegisterHint
{
    bool valid = false;
    uint32_t insn = 0;            // the jsr/jmp/braf/bsrf the CPU is stopped on
    int reg = 0;                  // rN
    uint32_t value = 0;           // rN at the halt
    uint32_t target = 0;          // where it goes: rN, or PC + 4 + rN for braf/bsrf
    const char* mnemonic = "";    // "jsr", "jmp", "braf", "bsrf"
};

// The hint for 'cpu' in 'snap': valid when its registers were captured and its PC is on a
// register-target call or jump in captured memory.
RegisterHint RegisterTargetAtHalt(const MemorySnapshot& snap, int cpu);

}  // namespace decomp
}  // namespace sfe
