// Sh2Operands — the Assembly panel's operand cell: how an instruction's operand text is
// split, drawn, hovered, and resolved against the live registers.
//
// It lives apart from AssemblyPanel.cpp because the hover behaviour here is exactly the
// kind that needs a headless ImGui test (FrontEnd/tests/Sh2OperandHoverTests.cpp) and the
// panel itself is not separable from App. Depends only on imgui.h, the disassembler, and
// the register struct — no emulator, no memory backend, and no knowledge of the panel's
// own widgets (it reports a right-click; the panel owns the menu that opens).
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "saturnexplorer/SaturnExplorer.h"

#include "Debug/Sh2Disasm.h"

namespace sfe
{

// The byte range [begin, end) of one operand within DisassembledInstruction::Operands.
struct Sh2OperandSpan
{
    size_t begin = 0;
    size_t end = 0;
};

// The span of operand 'index', false when there is no such operand. A comma inside
// parentheses does not separate operands: "@(r0,r4)" and "@(0x10,r3)" are single
// addressing modes, so each is one operand and one hover target. Splitting them would be
// worse than useless — neither half means anything alone, and the value the user is after
// is the address the group computes.
//
// Indexed rather than returning a vector because this runs for every visible row, every
// frame; nothing here allocates.
bool Sh2OperandAt(const std::string& operands, int index, Sh2OperandSpan& out);

// Registers r0..r15 referenced by 'operand', as a bit mask. Matches whole tokens, so "r1"
// is never found inside "r15".
uint16_t Sh2OperandRegMask(const std::string& operand);

// Effective address of a memory operand ("@..."), from the operand text and the live
// registers. The mnemonic's .b/.w/.l suffix gives the access width (outWidth 1/2/4).
// False when the operand is not a memory access or is not statically resolvable.
bool ResolveSh2MemOperand(const std::string& operand, const std::string& mnemonic,
                          const se_sh2_regs& r, uint32_t& outAddr, uint32_t& outWidth);

// As above for a whole instruction. 'index' picks one operand — the one the pointer was
// over — so a menu acting on "mac.l @r4+,@r5+" can act on the half the user aimed at;
// -1 falls back to the first operand that resolves, for callers with no pointer to go on.
bool ResolveSh2MemOperand(const DisassembledInstruction& ins, int index, const se_sh2_regs& r,
                          uint32_t& outAddr, uint32_t& outWidth);

// Reads 'n' big-endian bytes at 'addr' into 'outValue'; false if the read fails.
using Sh2MemReader = std::function<bool(uint32_t addr, uint32_t n, uint32_t& outValue)>;

// The hover-preview lines for operand 'index' of 'ins': the registers it references and,
// for a memory operand, the effective address and the value there. Empty when the operand
// has nothing to resolve (a bare immediate or branch target), so the caller can skip the
// tooltip rather than open an empty one.
std::vector<std::string> Sh2OperandHoverLines(const DisassembledInstruction& ins, int index,
                                              const se_sh2_regs& r, const Sh2MemReader& readMem);

struct Sh2OperandsDrawn
{
    int      hovered = -1;        // index of the hovered operand, or -1 for none
    bool     rightClicked = false;// a token was right-clicked: open the row menu on 'hovered'
    bool     clicked = false;     // a branch-target link was clicked
    uint32_t clickTarget = 0;
};

// Draw the operand text with per-token syntax colouring, at the current cursor. Every
// token is its own ImGui item, but hover is reported per *operand*: the returned index is
// the operand any hovered token belongs to, which is what makes both sides of
// "mov.l @(r0,r4),r1" independently hoverable. The comma separating two operands is
// punctuation and belongs to neither, so it reports nothing hovered.
Sh2OperandsDrawn DrawSh2Operands(const DisassembledInstruction& ins);

}  // namespace sfe
