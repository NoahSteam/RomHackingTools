// SH-2 disassembler — pure, self-contained (no emulator, no ImGui). Decodes a
// single 16-bit big-endian SH-2 opcode into structured fields so the Assembly
// panel can syntax-colour and follow branches. Hitachi SH-2 (SuperH) as used by
// the Saturn's master/slave CPUs; instructions are 2 bytes, big-endian.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace sfe
{

struct DisassembledInstruction
{
    uint32_t                Address = 0;
    uint16_t                Opcode = 0;
    std::string             Mnemonic;                 // e.g. "mov.l", "bt"
    std::string             Operands;                 // e.g. "r0,@(0x1234,r3)"
    bool                    HasBranchTarget = false;  // BranchTarget is valid
    uint32_t                BranchTarget = 0;         // resolved PC-relative target
    bool                    IsBranch = false;         // any control-flow change
    bool                    IsCall = false;           // bsr/bsrf/jsr/trapa
    bool                    IsReturn = false;         // rts/rte
    bool                    IsConditional = false;    // bt/bf/bt.s/bf.s
    bool                    IsValid = false;          // false -> illegal/unknown opcode
};

// Decode one opcode located at 'address'. Never throws; unknown encodings return a
// ".word 0xXXXX" instruction with IsValid=false.
DisassembledInstruction Sh2Decode(uint32_t address, uint16_t opcode);

// Convenience: read a big-endian opcode from 'bytes' (>= 2 bytes) at 'address'.
// Returns an "????" instruction with IsValid=false if fewer than 2 bytes remain.
DisassembledInstruction Sh2DecodeAt(uint32_t address, const uint8_t* bytes, size_t size);

}  // namespace sfe
