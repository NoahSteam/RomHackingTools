// SH-2 disassembler — pure, self-contained (no emulator, no ImGui). Decodes a
// single 16-bit big-endian SH-2 opcode into structured fields so the Assembly
// panel can syntax-colour and follow branches. Hitachi SH-2 (SuperH) as used by
// the Saturn's master/slave CPUs; instructions are 2 bytes, big-endian.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

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
    // PC-relative operand. Operands holds the address as if the instruction were reached any ordinary
    // way. That is wrong when it runs in the delay slot of a taken branch, where the hardware reads PC
    // as the branch destination plus two. Disassembling memory cannot tell the two apart: an
    // instruction that follows a delayed branch may equally be jumped to directly. So there it is shown
    // as "@(disp,pc)" and both candidates are kept (see Sh2DecodeAfterBranch).
    Sh2PcRel                PcRel = Sh2PcRel::None;
    uint32_t                PcRelDisp = 0;            // byte displacement, already scaled
    bool                    PcRelAmbiguous = false;   // placed after a delayed branch: PC depends on how it was reached
    uint32_t                PcRelDirectAddress = 0;   // the address when reached any other way (ambiguous only)
    bool                    PcRelHasSlotAddress = false;   // the branch destination is static
    uint32_t                PcRelSlotAddress = 0;     // the address when in the slot of that branch taken
};

// Decode one opcode located at 'address'. Never throws; unknown encodings return a
// ".word 0xXXXX" instruction with IsValid=false.
DisassembledInstruction Sh2Decode(uint32_t address, uint16_t opcode);

// Decode an instruction that sits directly after 'branch' in memory. Unless it is a PC-relative
// load or mova that is just Sh2Decode. If it is, whether it runs in the branch's delay slot is not
// something memory shows -- another path may jump straight to it -- so the operand is not resolved to
// one absolute address. It is shown as "@(disp,pc)", flagged PcRelAmbiguous, with the address for
// each way it can be reached: PcRelDirectAddress, and PcRelSlotAddress when the branch destination is
// static (bra/bsr/bt.s/bf.s; not jmp/jsr/braf/bsrf/rts/rte). A caller that knows execution really is
// in the slot may resolve it itself from those.
DisassembledInstruction Sh2DecodeAfterBranch(uint32_t address, uint16_t opcode,
                                             const DisassembledInstruction& branch);

// One row of a decoded code window.
struct Sh2WindowLine
{
    uint32_t                addr = 0;
    uint16_t                op = 0;
    bool                    readable = false;   // the window's bytes reach this instruction
    DisassembledInstruction ins;
};

// Decode 'count' instructions at 'base' from 'size' bytes. 'prevBytes' is the 2 bytes at base - 2,
// or null when they are unavailable: the first row follows an instruction outside the window, and
// must be treated as after a delayed branch exactly as it would be if that instruction were inside it.
std::vector<Sh2WindowLine> Sh2DecodeWindow(uint32_t base, const uint8_t* bytes, size_t size,
                                           int count, const uint8_t* prevBytes);

// Convenience: read a big-endian opcode from 'bytes' (>= 2 bytes) at 'address'.
// Returns an "????" instruction with IsValid=false if fewer than 2 bytes remain.
DisassembledInstruction Sh2DecodeAt(uint32_t address, const uint8_t* bytes, size_t size);

}  // namespace sfe
