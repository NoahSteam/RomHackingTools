// Unit tests for the 68000 disassembler. Each case is a hand-verified big-endian encoding
// checked for mnemonic, operands, byte length, and (for branches) the resolved target.
#include "Debug/M68kDisasm.h"

#include <cstdint>
#include <cstdio>
#include <vector>

using namespace sfe;

namespace
{
int gFail = 0;

// Decode 'words' (big-endian) at 'addr' and assert the rendered text + length.
void Case(uint32_t addr, std::vector<uint16_t> words, const char* mn, const char* ops,
          int len)
{
    std::vector<uint8_t> b;
    for (uint16_t w : words) { b.push_back(uint8_t(w >> 8)); b.push_back(uint8_t(w & 0xFF)); }
    M68kInstruction i = M68kDecodeAt(addr, b.data(), b.size());
    if (i.Mnemonic != mn || i.Operands != ops || i.Length != len)
    {
        std::printf("FAIL: %04X -> \"%s %s\" (len %d), expected \"%s %s\" (len %d)\n",
                    words[0], i.Mnemonic.c_str(), i.Operands.c_str(), i.Length, mn, ops, len);
        ++gFail;
    }
}

void CheckFlag(bool ok, const char* what)
{
    if (!ok) { std::printf("FAIL: %s\n", what); ++gFail; }
}
}  // namespace

int main()
{
    // --- Simple / implied ---
    Case(0x1000, {0x4E71}, "nop", "", 2);
    Case(0x1000, {0x4E75}, "rts", "", 2);
    Case(0x1000, {0x4E73}, "rte", "", 2);
    Case(0x1000, {0x4E77}, "rtr", "", 2);

    // --- MOVEQ ---
    Case(0x1000, {0x7201}, "moveq", "#$1,d1", 2);
    Case(0x1000, {0x70FF}, "moveq", "#-$1,d0", 2);   // sign-extended immediate

    // --- MOVE / MOVEA across EA modes + lengths ---
    Case(0x1000, {0x3200}, "move.w", "d0,d1", 2);
    Case(0x1000, {0x2010}, "move.l", "(a0),d0", 2);
    Case(0x1000, {0x1018}, "move.b", "(a0)+,d0", 2);
    Case(0x1000, {0x3248}, "movea.w", "a0,a1", 2);
    Case(0x1000, {0x203C, 0x1234, 0x5678}, "move.l", "#$12345678,d0", 6);
    Case(0x1000, {0x3028, 0x1234}, "move.w", "$1234(a0),d0", 4);

    // --- ALU ---
    Case(0x1000, {0xD240}, "add.w", "d0,d1", 2);
    Case(0x1000, {0x5240}, "addq.w", "#$1,d0", 2);
    Case(0x1000, {0xB081}, "cmp.l", "d1,d0", 2);
    Case(0x1000, {0x0640, 0x0010}, "addi.w", "#$10,d0", 4);

    // --- misc: LEA (incl. PC-relative), JMP/JSR, CLR, TST, SWAP ---
    Case(0x1000, {0x43D0}, "lea", "(a0),a1", 2);
    Case(0x1000, {0x41FA, 0x0010}, "lea", "$10(pc),a0", 4);
    Case(0x1000, {0x4E90}, "jsr", "(a0)", 2);
    Case(0x1000, {0x4ED0}, "jmp", "(a0)", 2);
    Case(0x1000, {0x4280}, "clr.l", "d0", 2);
    Case(0x1000, {0x4A40}, "tst.w", "d0", 2);
    Case(0x1000, {0x4840}, "swap", "d0", 2);

    // --- MOVEM (register list + predecrement reversal) ---
    Case(0x1000, {0x48E7, 0xC000}, "movem.l", "d0-d1,-(a7)", 4);
    Case(0x1000, {0x4CDF, 0x0003}, "movem.l", "(a7)+,d0-d1", 4);

    // --- Bit op ---
    Case(0x1000, {0x0800, 0x0005}, "btst", "#$5,d0", 4);

    // --- Branches: target resolution ---
    Case(0x1000, {0x6604}, "bne", "$1006", 2);           // 0x1000+2+4
    Case(0x1000, {0x6100, 0x0010}, "bsr", "$1012", 4);   // 0x1000+2+0x10
    Case(0x1000, {0x51C8, 0xFFFC}, "dbf", "d0,$FFE", 4); // 0x1000+2-4

    // --- Unknown / illegal falls back to a safe 2-byte dc.w ---
    Case(0x1000, {0xA000}, "dc.w", "$A000", 2);
    Case(0x1000, {0xF000}, "dc.w", "$F000", 2);

    // --- Flags on control-flow instructions ---
    {
        uint8_t b[2] = {0x4E, 0x75};   // rts
        M68kInstruction i = M68kDecodeAt(0x1000, b, 2);
        CheckFlag(i.IsReturn && i.IsBranch, "rts sets IsReturn+IsBranch");
    }
    {
        uint8_t b[2] = {0x66, 0x04};   // bne
        M68kInstruction i = M68kDecodeAt(0x1000, b, 2);
        CheckFlag(i.IsBranch && i.IsConditional && i.HasBranchTarget && i.BranchTarget == 0x1006,
                  "bne is conditional branch with target");
    }
    {
        uint8_t b[2] = {0x61, 0x00};   // bsr needs a following word; only 2 bytes -> truncated
        M68kInstruction i = M68kDecodeAt(0x1000, b, 2);
        CheckFlag(i.Mnemonic == "dc.w" && i.Length == 2, "truncated bsr degrades to dc.w");
    }

    if (gFail == 0) std::printf("All M68kDisasm tests passed.\n");
    return gFail == 0 ? 0 : 1;
}
