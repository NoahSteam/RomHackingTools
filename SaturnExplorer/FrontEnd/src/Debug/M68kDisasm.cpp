#include "Debug/M68kDisasm.h"

#include <cstdio>

namespace sfe
{
namespace
{

const char* kCC[16] = { "t",  "f",  "hi", "ls", "cc", "cs", "ne", "eq",
                        "vc", "vs", "pl", "mi", "ge", "lt", "gt", "le" };

std::string Hex(uint32_t v)
{
    char b[16];
    std::snprintf(b, sizeof(b), "$%X", v);
    return b;
}

// Signed displacement rendered as "$X" / "-$X".
std::string HexS(int32_t v)
{
    char b[16];
    if (v < 0) std::snprintf(b, sizeof(b), "-$%X", (unsigned)(-v));
    else       std::snprintf(b, sizeof(b), "$%X", (unsigned)v);
    return b;
}

std::string Dn(int r) { return std::string("d") + char('0' + (r & 7)); }
std::string An(int r) { return std::string("a") + char('0' + (r & 7)); }

// Sequential big-endian word reader over the instruction bytes; 'bad' latches if it runs
// past the available bytes, so an incomplete encoding degrades to a dc.w.
struct Reader
{
    const uint8_t* b;
    size_t         n;
    size_t         off = 0;
    uint32_t       base;   // address of the opcode word (68K PC)
    bool           bad = false;

    uint16_t Word()
    {
        if (off + 2 > n) { bad = true; return 0; }
        uint16_t v = (uint16_t(b[off]) << 8) | b[off + 1];
        off += 2;
        return v;
    }
    uint32_t Long()
    {
        uint32_t hi = Word();
        uint32_t lo = Word();
        return (hi << 16) | lo;
    }
    uint32_t CurPc() const { return base + uint32_t(off); }   // PC at the next word to read
};

// Brief extension word (index modes): "(disp,An,Xn.w/l)".
std::string BriefIndex(Reader& r, const std::string& an, bool pcRel)
{
    const uint16_t ext = r.Word();
    const bool     addr = (ext & 0x8000) != 0;
    const int      xr = (ext >> 12) & 7;
    const bool     lng = (ext & 0x0800) != 0;
    const int8_t   disp = int8_t(ext & 0xFF);
    std::string    x = (addr ? An(xr) : Dn(xr)) + (lng ? ".l" : ".w");
    std::string    d = disp ? HexS(disp) : "$0";
    return d + "(" + an + "," + x + ")";
    (void)pcRel;
}

// Effective address. size: 0=byte 1=word 2=long (governs immediate width). Consumes any
// extension words via 'r'. 'target'/'hasTarget' report a PC-relative resolved address.
std::string Ea(Reader& r, int mode, int reg, int size, uint32_t* target, bool* hasTarget)
{
    if (hasTarget) *hasTarget = false;
    switch (mode)
    {
        case 0: return Dn(reg);
        case 1: return An(reg);
        case 2: return "(" + An(reg) + ")";
        case 3: return "(" + An(reg) + ")+";
        case 4: return "-(" + An(reg) + ")";
        case 5:
        {
            int16_t d = int16_t(r.Word());
            return HexS(d) + "(" + An(reg) + ")";
        }
        case 6: return BriefIndex(r, An(reg), false);
        case 7:
            switch (reg)
            {
                case 0: { uint16_t a = r.Word(); return Hex(uint32_t(int16_t(a)) & 0xFFFFFF) + ".w"; }
                case 1: { uint32_t a = r.Long(); return Hex(a) + ".l"; }
                case 2:
                {
                    uint32_t pc = r.CurPc();
                    int16_t  d = int16_t(r.Word());
                    uint32_t t = pc + uint32_t(int32_t(d));
                    if (target)    *target = t;
                    if (hasTarget) *hasTarget = true;
                    return HexS(d) + "(pc)";
                }
                case 3: return BriefIndex(r, "pc", true);
                case 4:
                {
                    uint32_t v;
                    if (size == 2) v = r.Long();
                    else           v = r.Word() & (size == 0 ? 0xFF : 0xFFFF);
                    return "#" + Hex(v);
                }
                default: break;
            }
            break;
        default: break;
    }
    r.bad = true;
    return "?";
}

const char* SzSuffix(int s) { return s == 0 ? ".b" : s == 1 ? ".w" : ".l"; }

// Immediate-source size for ORI/ANDI/... (bits 7-6): 0=.b 1=.w 2=.l.
uint32_t ImmBySize(Reader& r, int size)
{
    if (size == 2) return r.Long();
    return r.Word() & (size == 0 ? 0xFF : 0xFFFF);
}

// MOVEM register-list mask -> "d0-d3/a0/a6". 'reversed' for predecrement (-(An)) order.
std::string RegList(uint16_t mask, bool reversed)
{
    // Normalize to natural order d0..d7,a0..a7 in a 16-slot table.
    bool bit[16];
    for (int i = 0; i < 16; ++i)
        bit[i] = (mask & (1 << (reversed ? (15 - i) : i))) != 0;
    std::string out;
    for (int half = 0; half < 2; ++half)   // 0 = D group, 1 = A group
    {
        int i = 0;
        while (i < 8)
        {
            if (!bit[half * 8 + i]) { ++i; continue; }
            int j = i;
            while (j + 1 < 8 && bit[half * 8 + j + 1]) ++j;
            std::string a = half ? An(i) : Dn(i);
            if (!out.empty()) out += "/";
            if (j > i) out += a + "-" + (half ? An(j) : Dn(j));
            else       out += a;
            i = j + 1;
        }
    }
    return out.empty() ? "" : out;
}

}  // namespace

M68kInstruction M68kDecodeAt(uint32_t address, const uint8_t* bytes, size_t size)
{
    M68kInstruction ins;
    ins.Address = address;
    if (!bytes || size < 2)
    {
        ins.Mnemonic = "dc.w";
        ins.Operands = "$0000";
        ins.Length = 2;
        return ins;
    }

    Reader r{ bytes, size, 0, address, false };
    const uint16_t op = r.Word();
    ins.Opcode = op;

    std::string mn, ops;
    bool handled = false;

    const int line = (op >> 12) & 0xF;
    const int mode = (op >> 3) & 7;    // EA mode  (for the low 6 bits)
    const int regf = op & 7;           // EA register
    auto ea = [&](int sz, uint32_t* t, bool* h) { return Ea(r, mode, regf, sz, t, h); };

    switch (line)
    {
        case 0x1: case 0x2: case 0x3:   // MOVE.b / MOVE.l / MOVE.w
        {
            const int sz = (line == 1) ? 0 : (line == 2) ? 2 : 1;   // 1=.b 2=.l 3=.w
            std::string src = Ea(r, mode, regf, sz, nullptr, nullptr);
            const int dmode = (op >> 6) & 7;
            const int dreg  = (op >> 9) & 7;
            std::string dst = Ea(r, dmode, dreg, sz, nullptr, nullptr);
            mn = (dmode == 1) ? std::string("movea") + SzSuffix(sz)
                              : std::string("move") + SzSuffix(sz);
            ops = src + "," + dst;
            handled = true;
            break;
        }
        case 0x7:   // MOVEQ
            if (!(op & 0x0100))
            {
                mn = "moveq";
                ops = "#" + HexS(int8_t(op & 0xFF)) + "," + Dn((op >> 9) & 7);
                handled = true;
            }
            break;
        case 0x6:   // Bcc / BRA / BSR
        {
            const int cond = (op >> 8) & 0xF;
            int32_t  disp = int8_t(op & 0xFF);
            uint32_t tpc = r.CurPc();     // PC of the (possible) extension word
            if ((op & 0xFF) == 0x00)      disp = int16_t(r.Word());
            else if ((op & 0xFF) == 0xFF) { disp = int32_t(r.Long()); }   // 68020+; decode anyway
            mn = cond == 0 ? "bra" : cond == 1 ? "bsr" : std::string("b") + kCC[cond];
            ins.BranchTarget = tpc + uint32_t(disp);
            ins.HasBranchTarget = true;
            ins.IsBranch = true;
            ins.IsCall = (cond == 1);
            ins.IsConditional = (cond >= 2);
            ops = Hex(ins.BranchTarget);
            handled = true;
            break;
        }
        case 0x5:   // ADDQ/SUBQ, Scc, DBcc
        {
            const int sz = (op >> 6) & 3;
            if (sz == 3)   // Scc or DBcc
            {
                const int cond = (op >> 8) & 0xF;
                if (mode == 1)   // DBcc
                {
                    uint32_t tpc = r.CurPc();
                    int16_t  d = int16_t(r.Word());
                    mn = std::string("db") + (cond == 1 ? "f" : kCC[cond]);
                    ins.BranchTarget = tpc + uint32_t(int32_t(d));
                    ins.HasBranchTarget = true;
                    ins.IsBranch = true;
                    ins.IsConditional = true;
                    ops = Dn(regf) + "," + Hex(ins.BranchTarget);
                }
                else             // Scc
                {
                    mn = std::string("s") + kCC[cond];
                    ops = ea(0, nullptr, nullptr);
                    ins.IsConditional = true;
                }
            }
            else            // ADDQ / SUBQ
            {
                int data = (op >> 9) & 7; if (data == 0) data = 8;
                mn = std::string((op & 0x0100) ? "subq" : "addq") + SzSuffix(sz);
                ops = "#" + Hex(uint32_t(data)) + "," + ea(sz, nullptr, nullptr);
            }
            handled = true;
            break;
        }
        case 0x0:   // MOVEP / immediate ALU / bit ops
        {
            const int sz = (op >> 6) & 3;
            const char* imm3[8] = { "ori", "andi", "subi", "addi", "?", "eori", "cmpi", "?" };
            const int grp = (op >> 9) & 7;
            if ((op & 0xF138) == 0x0108)   // MOVEP (shares bit 8 with the bit ops)
            {
                const int    dr = (op >> 9) & 7, ar = op & 7;
                const bool   toMem = (op & 0x0080) != 0;
                const bool   lng = (op & 0x0040) != 0;
                std::string  m = HexS(int16_t(r.Word())) + "(" + An(ar) + ")";
                mn = std::string("movep") + (lng ? ".l" : ".w");
                ops = toMem ? (Dn(dr) + "," + m) : (m + "," + Dn(dr));
                handled = true;
            }
            else if ((op & 0x0100) || grp == 4)   // dynamic (Dn) or static (#) bit op
            {
                const bool stat = (grp == 4) && !(op & 0x0100);
                const int  btype = (op >> 6) & 3;   // 0 btst 1 bchg 2 bclr 3 bset
                const char* bn[4] = { "btst", "bchg", "bclr", "bset" };
                std::string bit;
                if (stat) { bit = "#" + Hex(r.Word() & 0xFF); }
                else      { bit = Dn((op >> 9) & 7); }
                // Bit ops on Dn are long, on memory are byte.
                std::string dst = ea((mode == 0) ? 2 : 0, nullptr, nullptr);
                mn = bn[btype];
                ops = bit + "," + dst;
                handled = true;
            }
            else if (grp < 8 && imm3[grp][0] != '?' && sz != 3)
            {
                uint32_t imm = ImmBySize(r, sz);
                // ORI/ANDI/EORI to CCR/SR special-case (#imm,ccr|sr).
                if ((mode == 7 && regf == 4) && (grp == 0 || grp == 1 || grp == 5) &&
                    (sz == 0 || sz == 1))
                {
                    mn = imm3[grp];
                    ops = "#" + Hex(imm) + "," + (sz == 0 ? "ccr" : "sr");
                }
                else
                {
                    mn = std::string(imm3[grp]) + SzSuffix(sz);
                    ops = "#" + Hex(imm) + "," + ea(sz, nullptr, nullptr);
                }
                handled = true;
            }
            break;
        }
        case 0x4:   // misc — order matters: check specific encodings before the CLR/NEG/…
                    // size-field handlers they overlap with (size 3 = MOVE to/from SR/CCR).
            handled = true;
            if ((op & 0xFFF8) == 0x4E50) { mn = "link"; ops = An(regf) + ",#" + HexS(int16_t(r.Word())); }
            else if ((op & 0xFFF8) == 0x4E58) { mn = "unlk"; ops = An(regf); }
            else if ((op & 0xFFF0) == 0x4E40) { mn = "trap"; ops = "#" + Hex(op & 0xF); }
            else if (op == 0x4E71) { mn = "nop"; }
            else if (op == 0x4E70) { mn = "reset"; }
            else if (op == 0x4E73) { mn = "rte"; ins.IsReturn = ins.IsBranch = true; }
            else if (op == 0x4E75) { mn = "rts"; ins.IsReturn = ins.IsBranch = true; }
            else if (op == 0x4E77) { mn = "rtr"; ins.IsReturn = ins.IsBranch = true; }
            else if (op == 0x4E76) { mn = "trapv"; }
            else if (op == 0x4E72) { mn = "stop"; ops = "#" + Hex(r.Word()); }
            else if ((op & 0xFFF0) == 0x4E60) { // move usp (An <-> USP), bit 3 = direction
                mn = "move"; ops = (op & 0x08) ? std::string("usp,") + An(regf) : An(regf) + ",usp"; }
            else if ((op & 0xFFC0) == 0x4EC0) { mn = "jmp"; ops = ea(2, &ins.BranchTarget, &ins.HasBranchTarget); ins.IsBranch = true; }
            else if ((op & 0xFFC0) == 0x4E80) { mn = "jsr"; ops = ea(2, &ins.BranchTarget, &ins.HasBranchTarget); ins.IsBranch = ins.IsCall = true; }
            else if ((op & 0xFFF8) == 0x4840) { mn = "swap"; ops = Dn(regf); }   // before PEA
            else if ((op & 0xFFB8) == 0x4880) { mn = (op & 0x0040) ? "ext.l" : "ext.w"; ops = Dn(regf); }  // before MOVEM
            else if ((op & 0xFFC0) == 0x4840) { mn = "pea"; ops = ea(2, nullptr, nullptr); }
            else if ((op & 0xF1C0) == 0x41C0) { mn = "lea"; ops = ea(2, nullptr, nullptr) + "," + An((op >> 9) & 7); }
            else if ((op & 0xFB80) == 0x4880) { // MOVEM
                const bool toMem = !(op & 0x0400);
                const int  sz = (op & 0x0040) ? 2 : 1;
                uint16_t   mask = r.Word();
                std::string list = RegList(mask, toMem && mode == 4);
                std::string mem = ea(sz, nullptr, nullptr);
                mn = std::string("movem") + SzSuffix(sz);
                ops = toMem ? (list + "," + mem) : (mem + "," + list);
            }
            // MOVE to/from SR/CCR (size field == 3) — must precede CLR/NEG/NEGX/NOT.
            else if ((op & 0xFFC0) == 0x40C0) { mn = "move"; ops = std::string("sr,") + ea(1,nullptr,nullptr); }
            else if ((op & 0xFFC0) == 0x44C0) { mn = "move"; ops = ea(1,nullptr,nullptr) + ",ccr"; }
            else if ((op & 0xFFC0) == 0x46C0) { mn = "move"; ops = ea(1,nullptr,nullptr) + ",sr"; }
            else if ((op & 0xFF00) == 0x4200) { int s=(op>>6)&3; if(s==3){handled=false;} else { mn=std::string("clr")+SzSuffix(s); ops=ea(s,nullptr,nullptr);} }
            else if ((op & 0xFF00) == 0x4400) { int s=(op>>6)&3; if(s==3){handled=false;} else { mn=std::string("neg")+SzSuffix(s); ops=ea(s,nullptr,nullptr);} }
            else if ((op & 0xFF00) == 0x4000) { int s=(op>>6)&3; if(s==3){handled=false;} else { mn=std::string("negx")+SzSuffix(s); ops=ea(s,nullptr,nullptr);} }
            else if ((op & 0xFF00) == 0x4600) { int s=(op>>6)&3; if(s==3){handled=false;} else { mn=std::string("not")+SzSuffix(s); ops=ea(s,nullptr,nullptr);} }
            else if ((op & 0xFF00) == 0x4A00) { int s=(op>>6)&3;
                if (s==3) { mn="tas"; ops=ea(0,nullptr,nullptr); }
                else { mn=std::string("tst")+SzSuffix(s); ops=ea(s,nullptr,nullptr); } }
            else if ((op & 0xF140) == 0x4100) { // CHK
                mn = "chk"; ops = ea(1,nullptr,nullptr) + "," + Dn((op>>9)&7); }
            else handled = false;
            break;
        case 0x8: case 0x9: case 0xB: case 0xC: case 0xD:   // OR/SUB/CMP/AND/ADD families
        {
            const int reg = (op >> 9) & 7;
            const int opmode = (op >> 6) & 7;
            const char* fam = line == 0x8 ? "or" : line == 0x9 ? "sub"
                            : line == 0xB ? "cmp" : line == 0xC ? "and" : "add";
            // Address-register forms: opmode 3 (.w) / 7 (.l) -> ADDA/SUBA/CMPA.
            if ((line == 0x9 || line == 0xD || line == 0xB) && (opmode == 3 || opmode == 7))
            {
                const int sz = (opmode == 7) ? 2 : 1;
                mn = std::string(fam) + "a" + SzSuffix(sz);
                ops = ea(sz, nullptr, nullptr) + "," + An(reg);
                handled = true;
                break;
            }
            // MUL/DIV (line C/8 with opmode 3/7).
            if (line == 0xC && opmode == 3) { mn = "mulu"; ops = ea(1,nullptr,nullptr) + "," + Dn(reg); handled = true; break; }
            if (line == 0xC && opmode == 7) { mn = "muls"; ops = ea(1,nullptr,nullptr) + "," + Dn(reg); handled = true; break; }
            if (line == 0x8 && opmode == 3) { mn = "divu"; ops = ea(1,nullptr,nullptr) + "," + Dn(reg); handled = true; break; }
            if (line == 0x8 && opmode == 7) { mn = "divs"; ops = ea(1,nullptr,nullptr) + "," + Dn(reg); handled = true; break; }
            // EXG (line C, opmodes 0x08/0x09/0x11 in bits 8-3).
            if (line == 0xC && (op & 0x0130) == 0x0100 &&
                ((op & 0xF8) == 0x40 || (op & 0xF8) == 0x48 || (op & 0xF8) == 0x88))
            {
                const int rx = (op >> 9) & 7, ry = op & 7;
                if ((op & 0xF8) == 0x40)      ops = Dn(rx) + "," + Dn(ry);
                else if ((op & 0xF8) == 0x48) ops = An(rx) + "," + An(ry);
                else                          ops = Dn(rx) + "," + An(ry);
                mn = "exg"; handled = true; break;
            }
            // CMPM (line B, opmode 0/1/2 with mode 001).
            if (line == 0xB && (op & 0x0138) == 0x0108)
            {
                const int sz = (op >> 6) & 3;
                mn = std::string("cmpm") + SzSuffix(sz);
                ops = "(" + An(op & 7) + ")+,(" + An((op >> 9) & 7) + ")+";
                handled = true; break;
            }
            // EOR (line B, direction to EA, opmode 4/5/6).
            if (line == 0xB && (opmode & 4))
            {
                const int sz = opmode & 3;
                mn = std::string("eor") + SzSuffix(sz);
                ops = Dn(reg) + "," + ea(sz, nullptr, nullptr);
                handled = true; break;
            }
            // ADDX/SUBX (line 9/D, opmode 4/5/6 with mode 000 or 001).
            if ((line == 0x9 || line == 0xD) && (opmode & 4) && (mode == 0 || mode == 1))
            {
                const int sz = opmode & 3;
                mn = std::string(line == 0x9 ? "subx" : "addx") + SzSuffix(sz);
                if (op & 0x08) ops = "-(" + An(regf) + "),-(" + An(reg) + ")";
                else           ops = Dn(regf) + "," + Dn(reg);
                handled = true; break;
            }
            // ABCD/SBCD (line C/8, pattern xxxx rrr1 0000 xrrr).
            if ((line == 0xC || line == 0x8) && (op & 0x01F0) == 0x0100)
            {
                mn = (line == 0xC) ? "abcd" : "sbcd";
                if (op & 0x08) ops = "-(" + An(regf) + "),-(" + An(reg) + ")";
                else           ops = Dn(regf) + "," + Dn(reg);
                handled = true; break;
            }
            // Standard ALU: <ea>,Dn (opmode 0/1/2) or Dn,<ea> (opmode 4/5/6).
            const int sz = opmode & 3;
            if (sz != 3)
            {
                mn = std::string(fam) + SzSuffix(sz);
                if (opmode & 4) ops = Dn(reg) + "," + ea(sz, nullptr, nullptr);
                else            ops = ea(sz, nullptr, nullptr) + "," + Dn(reg);
                handled = true;
            }
            break;
        }
        case 0xE:   // shifts / rotates
        {
            const char* names[4] = { "as", "ls", "rox", "ro" };
            const int sz = (op >> 6) & 3;
            if (sz == 3)   // memory shift by 1
            {
                const int type = (op >> 9) & 3;
                const bool left = (op & 0x0100) != 0;
                mn = std::string(names[type]) + (left ? "l" : "r");
                ops = ea(1, nullptr, nullptr);
                handled = true;
            }
            else
            {
                const int type = (op >> 3) & 3;
                const bool left = (op & 0x0100) != 0;
                const bool byreg = (op & 0x0020) != 0;
                int cnt = (op >> 9) & 7;
                mn = std::string(names[type]) + (left ? "l" : "r") + SzSuffix(sz);
                std::string shift = byreg ? Dn(cnt) : ("#" + Hex(uint32_t(cnt == 0 ? 8 : cnt)));
                ops = shift + "," + Dn(regf);
                handled = true;
            }
            break;
        }
        default: break;
    }

    if (!handled || r.bad)
    {
        // Unknown or truncated: emit a safe 2-byte dc.w so the panel always advances.
        ins.Mnemonic = "dc.w";
        ins.Operands = Hex(op);
        ins.Length = 2;
        ins.IsValid = false;
        // A dc.w carries no control flow.
        ins.IsBranch = ins.IsCall = ins.IsReturn = ins.IsConditional = false;
        ins.HasBranchTarget = false;
        return ins;
    }

    ins.Mnemonic = mn;
    ins.Operands = ops;
    ins.Length = int(r.off);
    ins.IsValid = true;
    return ins;
}

}  // namespace sfe
