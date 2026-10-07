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

// The PC-relative operand forms: mov.w @(disp,pc), mov.l @(disp,pc) and mova @(disp,pc).
enum class Sh2PcRel : uint8_t { None, Word, Long, Mova };

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
    bool                    HasDelaySlot = false;     // the next instruction runs before the branch lands
    // PC-relative operand. Operands holds the address as if the instruction sat on its own, which is
    // wrong in the delay slot of a taken branch (see Sh2DecodeInDelaySlot); the displacement is kept
    // so it can be resolved again against the PC the hardware actually uses.
    Sh2PcRel                PcRel = Sh2PcRel::None;
    uint32_t                PcRelDisp = 0;            // byte displacement, already scaled
    bool                    PcRelAmbiguous = false;   // in a delay slot whose branch may or may not be taken
};

// Decode one opcode located at 'address'. Never throws; unknown encodings return a
// ".word 0xXXXX" instruction with IsValid=false.
DisassembledInstruction Sh2Decode(uint32_t address, uint16_t opcode);

// Decode the instruction in the delay slot of 'branch'. Only a PC-relative operand differs: the
// hardware reads PC in the slot of a taken branch as the branch destination plus two, not the slot's
// own address plus four. A bra/bsr destination is static, so the operand is resolved against it.
// For a conditional or register-indirect branch the destination is not known here, so the operand is
// shown as "@(disp,pc)" with PcRelAmbiguous set rather than printing an address that may be wrong.
DisassembledInstruction Sh2DecodeInDelaySlot(uint32_t address, uint16_t opcode,
                                             const DisassembledInstruction& branch);

// Convenience: read a big-endian opcode from 'bytes' (>= 2 bytes) at 'address'.
// Returns an "????" instruction with IsValid=false if fewer than 2 bytes remain.
DisassembledInstruction Sh2DecodeAt(uint32_t address, const uint8_t* bytes, size_t size);

}  // namespace sfe
