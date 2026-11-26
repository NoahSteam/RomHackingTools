// SH-2 single-line assembler -- pure C++14, no ImGui and no emulator dependency (builds for web).
//
// It accepts exactly the text Sh2Decode prints (see Sh2Disasm.h), plus a few aliases, and returns the
// one 16-bit opcode. It never allocates or moves a literal pool: PC-relative operands must name an
// address that is already reachable.
//
// Accepted syntax
//   * Case-insensitive mnemonics, registers and control registers; any run of blanks between tokens.
//   * An optional leading "XXXXXXXX" (8 hex digits, optional ':') address, so the panel's
//     "Copy Instruction" text pastes back. A ';' starts a comment that runs to the end of the line.
//   * Numbers: 0x1F, H'1F, $1F, decimal, with an optional leading '-' or '+'.
//   * Symbols (branch targets and absolute PC-relative operands): "name" or "name+n" / "name-n",
//     resolved through Sh2AsmContext::resolveSymbol.
//   * "bt.s"/"bf.s" are aliases for "bt/s"/"bf/s".
//   * ".word 0xNNNN" assembles to that word unchecked (every illegal encoding the decoder prints
//     round-trips through it).
//   * "#imm" takes 0..255 as raw bits; add/mov/cmp-eq also take -128..-1.
//   * Displacements are the scaled byte values the decoder prints; they must be a multiple of the
//     scale and in range.
//   * PC-relative loads/mova take either an absolute address "@(0x0600ABCD),rN" or the raw
//     displacement "@(0x1C,pc),rN". Branches take an absolute target address.
#pragma once

#include "Debug/Sh2Disasm.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace sfe
{

struct Sh2AsmContext
{
    uint32_t address = 0;                 // where the opcode will live (PC-relative forms depend on it)

    // The previous instruction (at address-2) is a delayed branch, so this one may sit in its delay slot.
    bool     afterDelayedBranch = false;
    bool     slotTargetKnown = false;     // that branch's destination is static
    uint32_t slotBranchTarget = 0;        // ... and is this

    bool     haveNext = false;            // the word at address+2 is known
    uint16_t nextOpcode = 0;

    // Resolve a symbol name to an address. Return false and set 'err' for an unknown or ambiguous
    // name. May be empty, in which case symbols are rejected.
    std::function<bool(const std::string& name, uint32_t& addr, std::string& err)> resolveSymbol;
};

struct Sh2AsmResult
{
    bool        ok = false;
    uint16_t    opcode = 0;               // valid only when ok
    std::string error;                    // set when !ok
    size_t      errorColumn = 0;          // 0-based offset into the input line the error points at
    std::vector<std::string> warnings;    // set when ok: delay-slot problems, sign-extension notes
    DisassembledInstruction decoded;      // when ok: Sh2Decode(ctx.address, opcode) (or the slot-aware decode)
};

// Assemble one line. Never throws. On success the opcode has been decoded back with Sh2Decode and
// checked against the request; a mismatch is reported as an internal error, never as bytes.
Sh2AsmResult Sh2Assemble(const std::string& line, const Sh2AsmContext& ctx);

}  // namespace sfe
