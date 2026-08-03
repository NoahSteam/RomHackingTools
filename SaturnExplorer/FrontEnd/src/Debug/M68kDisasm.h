// Motorola 68000 disassembler — pure, self-contained (no emulator, no ImGui). Decodes one
// instruction of the Saturn SCSP's MC68EC000 sound CPU, whose program lives in Sound RAM.
// Unlike the SH-2 (fixed 2-byte), 68000 instructions are variable length (2-10 bytes,
// big-endian): an opcode word optionally followed by immediate/displacement/extension words,
// so decoding must consume the extension words and report how many bytes it used.
//
// Coverage is the common integer instruction set a sound driver actually uses (moves, ALU,
// shifts/rotates, bit ops, branches, jumps, and the usual misc ops) across all 68000 effective-
// address modes. Unhandled encodings return a "dc.w $XXXX" instruction with IsValid=false,
// so the panel always advances by a legal 2 bytes rather than desyncing.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace sfe
{

struct M68kInstruction
{
    uint32_t    Address = 0;
    uint16_t    Opcode = 0;                // the first (operation) word
    int         Length = 2;               // total bytes consumed (2..10, always even)
    std::string Mnemonic;                 // e.g. "move.l", "bne", "jsr"
    std::string Operands;                 // e.g. "d0,(a1)+", "$1234(a5)"
    bool        HasBranchTarget = false;  // BranchTarget is valid (Bcc/BRA/BSR/DBcc)
    uint32_t    BranchTarget = 0;         // resolved PC-relative target
    bool        IsBranch = false;         // any control-flow change
    bool        IsCall = false;           // bsr / jsr
    bool        IsReturn = false;         // rts / rte / rtr
    bool        IsConditional = false;    // Bcc / DBcc / Scc
    bool        IsValid = false;          // false -> unknown/illegal (rendered as dc.w)
};

// Decode one instruction at 'address' from big-endian 'bytes' (the opcode word plus any
// extension words). Never throws; consumes at most 'size' bytes. If fewer than 2 bytes remain
// (or an encoding needs more extension bytes than are available) it returns a 2-byte
// dc.w with IsValid=false. 'address' is the 68K PC (a Sound-RAM offset) used to resolve
// PC-relative operands and branch targets.
M68kInstruction M68kDecodeAt(uint32_t address, const uint8_t* bytes, size_t size);

}  // namespace sfe
