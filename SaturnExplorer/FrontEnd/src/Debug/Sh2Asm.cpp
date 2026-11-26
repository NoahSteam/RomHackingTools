#include "Debug/Sh2Asm.h"

#include <algorithm>
#include <cctype>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace sfe
{

namespace
{

// ---------------------------------------------------------------------------------------------
// Errors and small helpers
// ---------------------------------------------------------------------------------------------

struct AsmError
{
    std::string msg;
    size_t      col = 0;
};

bool Fail(AsmError& e, size_t col, const std::string& msg)
{
    e.msg = msg;
    e.col = col;
    return false;
}

std::string Fmt(const char* fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    return buf;
}

bool IsBlank(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }
bool IsDigit(char c) { return c >= '0' && c <= '9'; }
bool IsHex(char c) { return IsDigit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }
bool IsAlpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
bool IsSymStart(char c) { return IsAlpha(c) || c == '_' || c == '.'; }
bool IsSymChar(char c) { return IsSymStart(c) || IsDigit(c); }
char Lower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c; }

int HexVal(char c)
{
    if (IsDigit(c)) return c - '0';
    return Lower(c) - 'a' + 10;
}

void SkipBlanks(const std::string& s, size_t& p, size_t e)
{
    while (p < e && IsBlank(s[p])) ++p;
}

// Shrink [b,e) so it has no blank at either end.
void Trim(const std::string& s, size_t& b, size_t& e)
{
    while (b < e && IsBlank(s[b])) ++b;
    while (e > b && IsBlank(s[e - 1])) --e;
}

std::string LowerRange(const std::string& s, size_t b, size_t e)
{
    std::string r;
    for (size_t i = b; i < e; ++i) r += Lower(s[i]);
    return r;
}

// ---------------------------------------------------------------------------------------------
// Numbers and expressions
// ---------------------------------------------------------------------------------------------

// Values are held in int64 so that "-128" and "0xFFFFFFFF" both fit; anything past 40 bits is a typo.
const int64_t kNumberLimit = 0xFFFFFFFFFFLL;

bool NumberStartsAt(const std::string& s, size_t p, size_t e)
{
    if (p >= e) return false;
    if (IsDigit(s[p]) || s[p] == '$') return true;
    if ((s[p] == 'h' || s[p] == 'H') && p + 1 < e && s[p + 1] == '\'') return true;
    return false;
}

// Parses an optionally signed number at p (0x.., H'.., $.., decimal). The sign may be followed by
// blanks so that "sym - 4" reads naturally.
bool ParseNumberAt(const std::string& s, size_t& p, size_t e, int64_t& out, AsmError& err)
{
    bool neg = false;
    if (p < e && (s[p] == '-' || s[p] == '+'))
    {
        neg = s[p] == '-';
        ++p;
        SkipBlanks(s, p, e);
    }
    const size_t start = p;
    int base = 10;
    if (p + 1 < e && s[p] == '0' && (s[p + 1] == 'x' || s[p + 1] == 'X')) { base = 16; p += 2; }
    else if (p + 1 < e && (s[p] == 'h' || s[p] == 'H') && s[p + 1] == '\'') { base = 16; p += 2; }
    else if (p < e && s[p] == '$') { base = 16; p += 1; }

    const size_t digits = p;
    int64_t v = 0;
    while (p < e && (base == 16 ? IsHex(s[p]) : IsDigit(s[p])))
    {
        v = v * base + HexVal(s[p]);
        if (v > kNumberLimit) return Fail(err, start, "number is too large");
        ++p;
    }
    if (p == digits)
        return Fail(err, start, base == 16 ? "expected hex digits after the prefix" : "expected a number");
    // "12abc" or "0x1G": reject instead of silently stopping at the bad character.
    if (p < e && (IsSymChar(s[p]) || s[p] == '\''))
        return Fail(err, p, "malformed number '" + s.substr(start, std::min<size_t>(e, p + 1) - start) + "'");
    out = neg ? -v : v;
    return true;
}

struct Expr
{
    int64_t val = 0;       // number as written, or the resolved symbol address plus offset
    bool    isSymbol = false;
    size_t  col = 0;
};

// number | symbol [(+|-) number]. Must consume all of [b,e).
bool ParseExpr(const std::string& s, size_t b, size_t e, const Sh2AsmContext& ctx, Expr& out, AsmError& err)
{
    size_t p = b;
    out.col = b;
    if (p < e && (s[p] == '-' || s[p] == '+'))
    {
        if (!ParseNumberAt(s, p, e, out.val, err)) return false;
    }
    else if (NumberStartsAt(s, p, e))
    {
        if (!ParseNumberAt(s, p, e, out.val, err)) return false;
    }
    else if (p < e && IsSymStart(s[p]))
    {
        const size_t sb = p;
        while (p < e && IsSymChar(s[p])) ++p;
        const std::string name = s.substr(sb, p - sb);
        int64_t off = 0;
        SkipBlanks(s, p, e);
        if (p < e && (s[p] == '+' || s[p] == '-'))
        {
            if (!ParseNumberAt(s, p, e, off, err)) return false;
        }
        // Trailing junk is reported before the lookup: "r2 r3" should say "unexpected 'r3'", not
        // complain that the symbol 'r2' does not exist.
        SkipBlanks(s, p, e);
        if (p < e) return Fail(err, p, "unexpected '" + s.substr(p, e - p) + "' after operand");
        uint32_t addr = 0;
        std::string why;
        if (!ctx.resolveSymbol)
            return Fail(err, sb, "unknown symbol '" + name + "' (no symbol table available)");
        if (!ctx.resolveSymbol(name, addr, why))
            return Fail(err, sb, why.empty() ? "unknown symbol '" + name + "'" : why);
        out.val = (int64_t)(uint32_t)((int64_t)addr + off);   // wraps like the target's 32-bit arithmetic
        out.isSymbol = true;
    }
    else
        return Fail(err, p, "expected a number or symbol");

    SkipBlanks(s, p, e);
    if (p < e) return Fail(err, p, "unexpected '" + s.substr(p, e - p) + "' after operand");
    return true;
}

// ---------------------------------------------------------------------------------------------
// Operands
// ---------------------------------------------------------------------------------------------

enum class OK { Reg, Imm, Ind, Inc, Dec, R0Idx, Disp, GbrDisp, R0Gbr, PcRaw, PcAbs, Ctrl, Expr };

enum CtrlId { C_SR, C_GBR, C_VBR, C_MACH, C_MACL, C_PR, C_COUNT };
const char* const kCtrlNames[C_COUNT] = { "sr", "gbr", "vbr", "mach", "macl", "pr" };

struct Operand
{
    OK      kind = OK::Reg;
    int     reg = 0;          // register for Reg/Ind/Inc/Dec/R0Idx/Disp
    int64_t val = 0;          // immediate, displacement, or absolute address
    int     ctrl = 0;         // CtrlId for Ctrl
    size_t  col = 0;
};

// 1: s[b,e) is exactly r0..r15 (reg set); 0: not register-shaped; -1: register-shaped but out of range.
int ParseRegister(const std::string& s, size_t b, size_t e, int& reg)
{
    Trim(s, b, e);
    if (e - b < 2 || Lower(s[b]) != 'r') return 0;
    int v = 0;
    for (size_t i = b + 1; i < e; ++i)
    {
        if (!IsDigit(s[i])) return 0;
        v = (std::min)(v * 10 + (s[i] - '0'), 1000);
    }
    if (v > 15) return -1;
    reg = v;
    return 1;
}

bool RegOrError(const std::string& s, size_t b, size_t e, int& reg, const char* what, AsmError& err)
{
    Trim(s, b, e);
    const int r = ParseRegister(s, b, e, reg);
    if (r == 1) return true;
    if (r == -1) return Fail(err, b, "register '" + s.substr(b, e - b) + "' out of range (r0..r15)");
    return Fail(err, b, std::string("expected a register ") + what);
}

bool ParseOperand(const std::string& s, size_t b, size_t e, const Sh2AsmContext& ctx, Operand& op, AsmError& err)
{
    Trim(s, b, e);
    op.col = b;
    if (b >= e) return Fail(err, b, "missing operand");

    if (s[b] == '#')
    {
        op.kind = OK::Imm;
        size_t p = b + 1;
        SkipBlanks(s, p, e);
        if (!ParseNumberAt(s, p, e, op.val, err)) return false;
        SkipBlanks(s, p, e);
        if (p < e) return Fail(err, p, "unexpected '" + s.substr(p, e - p) + "' after immediate");
        return true;
    }

    if (s[b] == '@')
    {
        const size_t p = b + 1;
        if (p >= e) return Fail(err, p, "expected a register or '(' after '@'");
        if (s[p] == '-')
        {
            op.kind = OK::Dec;
            return RegOrError(s, p + 1, e, op.reg, "after '@-'", err);
        }
        if (s[p] == '(')
        {
            if (s[e - 1] != ')') return Fail(err, e - 1, "missing ')'");
            const size_t ib = p + 1, ie = e - 1;
            // Split the inside on its (single) comma.
            size_t comma = std::string::npos;
            for (size_t i = ib; i < ie; ++i)
            {
                if (s[i] == '(' || s[i] == ')') return Fail(err, i, "unexpected parenthesis");
                if (s[i] == ',')
                {
                    if (comma != std::string::npos) return Fail(err, i, "too many parts inside '@( )'");
                    comma = i;
                }
            }
            if (comma == std::string::npos)
            {
                // @(address): the absolute spelling of a PC-relative load or mova.
                Expr ex;
                size_t xb = ib, xe = ie;
                Trim(s, xb, xe);
                if (xb >= xe) return Fail(err, ib, "empty '@( )'");
                int dummy = 0;
                if (ParseRegister(s, xb, xe, dummy) != 0)
                    return Fail(err, xb, "a register alone is not valid inside '@( )'; use @rN or @(0,rN)");
                if (!ParseExpr(s, xb, xe, ctx, ex, err)) return false;
                if (ex.val < -(int64_t)0x80000000LL || ex.val > 0xFFFFFFFFLL)
                    return Fail(err, xb, "address is outside the 32-bit range");
                op.kind = OK::PcAbs;
                op.val = (int64_t)(uint32_t)ex.val;
                return true;
            }
            size_t ab = ib, ae = comma, bb = comma + 1, be = ie;
            Trim(s, ab, ae);
            Trim(s, bb, be);
            if (ab >= ae) return Fail(err, ib, "missing first part of '@(a,b)'");
            if (bb >= be) return Fail(err, comma + 1, "missing second part of '@(a,b)'");
            const std::string second = LowerRange(s, bb, be);
            int baseReg = 0;
            const int secondIsReg = ParseRegister(s, bb, be, baseReg);
            if (secondIsReg == -1)
                return Fail(err, bb, "register '" + s.substr(bb, be - bb) + "' out of range (r0..r15)");
            const bool gbr = second == "gbr", pc = second == "pc";
            if (secondIsReg != 1 && !gbr && !pc)
                return Fail(err, bb, "expected a register, gbr or pc after the comma");

            int firstReg = 0;
            const int firstIsReg = ParseRegister(s, ab, ae, firstReg);
            if (firstIsReg == -1)
                return Fail(err, ab, "register '" + s.substr(ab, ae - ab) + "' out of range (r0..r15)");
            if (firstIsReg == 1)
            {
                if (pc) return Fail(err, ab, "'@(rN,pc)' is not an SH-2 addressing mode");
                if (firstReg != 0) return Fail(err, ab, "the index register must be r0");
                if (gbr) { op.kind = OK::R0Gbr; return true; }
                op.kind = OK::R0Idx;
                op.reg = baseReg;
                return true;
            }
            Expr ex;
            if (!ParseExpr(s, ab, ae, ctx, ex, err)) return false;
            if (ex.isSymbol) return Fail(err, ab, "a symbol is not valid as a displacement");
            op.val = ex.val;
            if (pc) op.kind = OK::PcRaw;
            else if (gbr) op.kind = OK::GbrDisp;
            else { op.kind = OK::Disp; op.reg = baseReg; }
            return true;
        }
        // @rN or @rN+
        size_t re = e;
        bool plus = false;
        if (s[e - 1] == '+') { plus = true; --re; }
        op.kind = plus ? OK::Inc : OK::Ind;
        return RegOrError(s, p, re, op.reg, "after '@'", err);
    }

    int reg = 0;
    const int r = ParseRegister(s, b, e, reg);
    if (r == 1) { op.kind = OK::Reg; op.reg = reg; return true; }
    if (r == -1) return Fail(err, b, "register '" + s.substr(b, e - b) + "' out of range (r0..r15)");

    const std::string low = LowerRange(s, b, e);
    for (int c = 0; c < C_COUNT; ++c)
        if (low == kCtrlNames[c]) { op.kind = OK::Ctrl; op.ctrl = c; return true; }

    Expr ex;
    if (!ParseExpr(s, b, e, ctx, ex, err)) return false;
    if (ex.val < -(int64_t)0x80000000LL || ex.val > 0xFFFFFFFFLL)
        return Fail(err, b, "address is outside the 32-bit range");
    op.kind = OK::Expr;
    op.val = (int64_t)(uint32_t)ex.val;
    return true;
}

// ---------------------------------------------------------------------------------------------
// Instruction table. Deliberately written from the SH-2 programming manual's opcode map and not
// derived from Sh2Disasm.cpp, so that decode-back and the exhaustive round trip test two
// independent descriptions of the encoding.
// ---------------------------------------------------------------------------------------------

enum class S
{
    Rn, Rm, R0,                 // register in bits 8-11, bits 4-7, or r0 only
    IndN, IndM, IncN, IncM, DecN,  // @rN, @rN+, @-rN
    R0IdxN, R0IdxM,             // @(r0,rN)
    DispN, DispM,               // @(disp,rN), 4-bit field scaled by arg
    GbrDisp, R0Gbr,            // @(disp,gbr) 8-bit field scaled by arg, @(r0,gbr)
    ImmS, ImmU,                 // #imm8: signed-or-raw, unsigned
    Ctrl,                       // sr/gbr/vbr/mach/macl/pr, arg = CtrlId
    PcW, PcL, Mova,             // PC-relative operand
    Br8, Br12                   // branch target, 8-bit / 12-bit displacement
};

struct Spec { S s; uint8_t arg; };

const Spec sRn = { S::Rn, 0 }, sRm = { S::Rm, 0 }, sR0 = { S::R0, 0 };
const Spec sIndN = { S::IndN, 0 }, sIndM = { S::IndM, 0 }, sIncN = { S::IncN, 0 }, sIncM = { S::IncM, 0 };
const Spec sDecN = { S::DecN, 0 };
const Spec sR0IdxN = { S::R0IdxN, 0 }, sR0IdxM = { S::R0IdxM, 0 };
const Spec sDispN4 = { S::DispN, 4 }, sDispM1 = { S::DispM, 1 }, sDispM2 = { S::DispM, 2 }, sDispM4 = { S::DispM, 4 };
const Spec sGbrD1 = { S::GbrDisp, 1 }, sGbrD2 = { S::GbrDisp, 2 }, sGbrD4 = { S::GbrDisp, 4 };
const Spec sR0Gbr = { S::R0Gbr, 0 };
const Spec sImmS = { S::ImmS, 0 }, sImmU = { S::ImmU, 0 };
const Spec cSR = { S::Ctrl, C_SR }, cGBR = { S::Ctrl, C_GBR }, cVBR = { S::Ctrl, C_VBR };
const Spec cMACH = { S::Ctrl, C_MACH }, cMACL = { S::Ctrl, C_MACL }, cPR = { S::Ctrl, C_PR };
const Spec sPcW = { S::PcW, 0 }, sPcL = { S::PcL, 0 }, sMova = { S::Mova, 0 };
const Spec sBr8 = { S::Br8, 0 }, sBr12 = { S::Br12, 0 };
const Spec sNone = { S::Rn, 0 };   // placeholder, never read (count < n)

struct Form
{
    const char* mn;
    uint16_t    base;
    int         n;
    Spec        sp[2];
    bool        signExt;    // #imm is sign-extended by the hardware (note shown for 0x80..0xFF)
};

#define F0(mn, base)              { mn, base, 0, { sNone, sNone }, false }
#define F1(mn, base, a)           { mn, base, 1, { a, sNone }, false }
#define F2(mn, base, a, b)        { mn, base, 2, { a, b }, false }
#define F2S(mn, base, a, b)       { mn, base, 2, { a, b }, true }

const Form kForms[] =
{
    // 0x0xxx
    F2("stc", 0x0002, cSR, sRn), F2("stc", 0x0012, cGBR, sRn), F2("stc", 0x0022, cVBR, sRn),
    F1("bsrf", 0x0003, sRn), F1("braf", 0x0023, sRn),
    F2("mov.b", 0x0004, sRm, sR0IdxN), F2("mov.w", 0x0005, sRm, sR0IdxN), F2("mov.l", 0x0006, sRm, sR0IdxN),
    F2("mul.l", 0x0007, sRm, sRn),
    F0("clrt", 0x0008), F0("sett", 0x0018), F0("clrmac", 0x0028),
    F0("nop", 0x0009), F0("div0u", 0x0019), F1("movt", 0x0029, sRn),
    F2("sts", 0x000A, cMACH, sRn), F2("sts", 0x001A, cMACL, sRn), F2("sts", 0x002A, cPR, sRn),
    F0("rts", 0x000B), F0("sleep", 0x001B), F0("rte", 0x002B),
    F2("mov.b", 0x000C, sR0IdxM, sRn), F2("mov.w", 0x000D, sR0IdxM, sRn), F2("mov.l", 0x000E, sR0IdxM, sRn),
    F2("mac.l", 0x000F, sIncM, sIncN),
    // 0x1xxx
    F2("mov.l", 0x1000, sRm, sDispN4),
    // 0x2xxx
    F2("mov.b", 0x2000, sRm, sIndN), F2("mov.w", 0x2001, sRm, sIndN), F2("mov.l", 0x2002, sRm, sIndN),
    F2("mov.b", 0x2004, sRm, sDecN), F2("mov.w", 0x2005, sRm, sDecN), F2("mov.l", 0x2006, sRm, sDecN),
    F2("div0s", 0x2007, sRm, sRn), F2("tst", 0x2008, sRm, sRn), F2("and", 0x2009, sRm, sRn),
    F2("xor", 0x200A, sRm, sRn), F2("or", 0x200B, sRm, sRn), F2("cmp/str", 0x200C, sRm, sRn),
    F2("xtrct", 0x200D, sRm, sRn), F2("mulu.w", 0x200E, sRm, sRn), F2("muls.w", 0x200F, sRm, sRn),
    // 0x3xxx
    F2("cmp/eq", 0x3000, sRm, sRn), F2("cmp/hs", 0x3002, sRm, sRn), F2("cmp/ge", 0x3003, sRm, sRn),
    F2("div1", 0x3004, sRm, sRn), F2("dmulu.l", 0x3005, sRm, sRn), F2("cmp/hi", 0x3006, sRm, sRn),
    F2("cmp/gt", 0x3007, sRm, sRn), F2("sub", 0x3008, sRm, sRn), F2("subc", 0x300A, sRm, sRn),
    F2("subv", 0x300B, sRm, sRn), F2("add", 0x300C, sRm, sRn), F2("dmuls.l", 0x300D, sRm, sRn),
    F2("addc", 0x300E, sRm, sRn), F2("addv", 0x300F, sRm, sRn),
    // 0x4xxx
    F1("shll", 0x4000, sRn), F1("shlr", 0x4001, sRn),
    F2("sts.l", 0x4002, cMACH, sDecN), F2("stc.l", 0x4003, cSR, sDecN),
    F1("rotl", 0x4004, sRn), F1("rotr", 0x4005, sRn),
    F2("lds.l", 0x4006, sIncN, cMACH), F2("ldc.l", 0x4007, sIncN, cSR),
    F1("shll2", 0x4008, sRn), F1("shlr2", 0x4009, sRn),
    F2("lds", 0x400A, sRn, cMACH), F1("jsr", 0x400B, sIndN), F2("ldc", 0x400E, sRn, cSR),
    F2("mac.w", 0x400F, sIncM, sIncN),
    F1("dt", 0x4010, sRn), F1("cmp/pz", 0x4011, sRn),
    F2("sts.l", 0x4012, cMACL, sDecN), F2("stc.l", 0x4013, cGBR, sDecN),
    F1("cmp/pl", 0x4015, sRn),
    F2("lds.l", 0x4016, sIncN, cMACL), F2("ldc.l", 0x4017, sIncN, cGBR),
    F1("shll8", 0x4018, sRn), F1("shlr8", 0x4019, sRn),
    F2("lds", 0x401A, sRn, cMACL), F1("tas.b", 0x401B, sIndN), F2("ldc", 0x401E, sRn, cGBR),
    F1("shal", 0x4020, sRn), F1("shar", 0x4021, sRn),
    F2("sts.l", 0x4022, cPR, sDecN), F2("stc.l", 0x4023, cVBR, sDecN),
    F1("rotcl", 0x4024, sRn), F1("rotcr", 0x4025, sRn),
    F2("lds.l", 0x4026, sIncN, cPR), F2("ldc.l", 0x4027, sIncN, cVBR),
    F1("shll16", 0x4028, sRn), F1("shlr16", 0x4029, sRn),
    F2("lds", 0x402A, sRn, cPR), F1("jmp", 0x402B, sIndN), F2("ldc", 0x402E, sRn, cVBR),
    // 0x5xxx
    F2("mov.l", 0x5000, sDispM4, sRn),
    // 0x6xxx
    F2("mov.b", 0x6000, sIndM, sRn), F2("mov.w", 0x6001, sIndM, sRn), F2("mov.l", 0x6002, sIndM, sRn),
    F2("mov", 0x6003, sRm, sRn),
    F2("mov.b", 0x6004, sIncM, sRn), F2("mov.w", 0x6005, sIncM, sRn), F2("mov.l", 0x6006, sIncM, sRn),
    F2("not", 0x6007, sRm, sRn), F2("swap.b", 0x6008, sRm, sRn), F2("swap.w", 0x6009, sRm, sRn),
    F2("negc", 0x600A, sRm, sRn), F2("neg", 0x600B, sRm, sRn),
    F2("extu.b", 0x600C, sRm, sRn), F2("extu.w", 0x600D, sRm, sRn),
    F2("exts.b", 0x600E, sRm, sRn), F2("exts.w", 0x600F, sRm, sRn),
    // 0x7xxx
    F2S("add", 0x7000, sImmS, sRn),
    // 0x8xxx
    F2("mov.b", 0x8000, sR0, sDispM1), F2("mov.w", 0x8100, sR0, sDispM2),
    F2("mov.b", 0x8400, sDispM1, sR0), F2("mov.w", 0x8500, sDispM2, sR0),
    F2S("cmp/eq", 0x8800, sImmS, sR0),
    F1("bt", 0x8900, sBr8), F1("bf", 0x8B00, sBr8), F1("bt/s", 0x8D00, sBr8), F1("bf/s", 0x8F00, sBr8),
    // 0x9xxx - 0xBxxx
    F2("mov.w", 0x9000, sPcW, sRn),
    F1("bra", 0xA000, sBr12), F1("bsr", 0xB000, sBr12),
    // 0xCxxx
    F2("mov.b", 0xC000, sR0, sGbrD1), F2("mov.w", 0xC100, sR0, sGbrD2), F2("mov.l", 0xC200, sR0, sGbrD4),
    F1("trapa", 0xC300, sImmU),
    F2("mov.b", 0xC400, sGbrD1, sR0), F2("mov.w", 0xC500, sGbrD2, sR0), F2("mov.l", 0xC600, sGbrD4, sR0),
    F2("mova", 0xC700, sMova, sR0),
    F2("tst", 0xC800, sImmU, sR0), F2("and", 0xC900, sImmU, sR0),
    F2("xor", 0xCA00, sImmU, sR0), F2("or", 0xCB00, sImmU, sR0),
    F2("tst.b", 0xCC00, sImmU, sR0Gbr), F2("and.b", 0xCD00, sImmU, sR0Gbr),
    F2("xor.b", 0xCE00, sImmU, sR0Gbr), F2("or.b", 0xCF00, sImmU, sR0Gbr),
    // 0xDxxx, 0xExxx
    F2("mov.l", 0xD000, sPcL, sRn),
    F2S("mov", 0xE000, sImmS, sRn),
};
const size_t kFormCount = sizeof(kForms) / sizeof(kForms[0]);

// Whether operand 'op' has the shape a spec expects (register numbers are checked at encode time).
bool ShapeMatches(const Spec& sp, const Operand& op)
{
    switch (sp.s)
    {
    case S::Rn: case S::Rm: case S::R0:          return op.kind == OK::Reg;
    case S::IndN: case S::IndM:                  return op.kind == OK::Ind;
    case S::IncN: case S::IncM:                  return op.kind == OK::Inc;
    case S::DecN:                                return op.kind == OK::Dec;
    case S::R0IdxN: case S::R0IdxM:              return op.kind == OK::R0Idx;
    case S::DispN: case S::DispM:                return op.kind == OK::Disp;
    case S::GbrDisp:                             return op.kind == OK::GbrDisp;
    case S::R0Gbr:                               return op.kind == OK::R0Gbr;
    case S::ImmS: case S::ImmU:                  return op.kind == OK::Imm;
    case S::Ctrl:                                return op.kind == OK::Ctrl && op.ctrl == sp.arg;
    case S::PcW: case S::PcL: case S::Mova:      return op.kind == OK::PcRaw || op.kind == OK::PcAbs;
    case S::Br8: case S::Br12:                   return op.kind == OK::Expr;
    }
    return false;
}

std::string SpecText(const Spec& sp)
{
    switch (sp.s)
    {
    case S::Rn: case S::Rm:       return "rN";
    case S::R0:                   return "r0";
    case S::IndN: case S::IndM:   return "@rN";
    case S::IncN: case S::IncM:   return "@rN+";
    case S::DecN:                 return "@-rN";
    case S::R0IdxN: case S::R0IdxM: return "@(r0,rN)";
    case S::DispN: case S::DispM: return "@(disp,rN)";
    case S::GbrDisp:              return "@(disp,gbr)";
    case S::R0Gbr:                return "@(r0,gbr)";
    case S::ImmS: case S::ImmU:   return "#imm";
    case S::Ctrl:                 return kCtrlNames[sp.arg];
    case S::PcW: case S::PcL: case S::Mova: return "@(address)";
    case S::Br8: case S::Br12:    return "target";
    }
    return "?";
}

std::string FormText(const Form& f)
{
    std::string r = f.mn;
    for (int i = 0; i < f.n; ++i) r += (i ? "," : " ") + SpecText(f.sp[i]);
    return r;
}

const char* kLiteralPoolMsg =
    "needs a literal-pool slot; SH-2 loads 32-bit constants with mov.l @(disp,pc),rN "
    "\xE2\x80\x94 use Edit Literal on that row or type the @(addr) form";

// ---------------------------------------------------------------------------------------------
// Encoding of one candidate form
// ---------------------------------------------------------------------------------------------

struct Encoded
{
    uint16_t    op = 0;
    std::string canon;                  // operand text the decoder is expected to print back
    std::vector<std::string> warnings;
};

std::string Hex(int64_t v) { return Fmt("0x%llX", (unsigned long long)v); }

// Reachable absolute range text for a PC-relative operand, for error messages.
std::string RangeText(uint32_t base, uint32_t maxDisp)
{
    return Fmt("0x%08X..0x%08X", base, (uint32_t)(base + maxDisp));
}

bool EncodePcRel(const Spec& sp, const Operand& o, const Sh2AsmContext& ctx, uint16_t& field,
                 uint32_t& ea, Encoded& out, AsmError& err)
{
    const bool word = sp.s == S::PcW;
    const uint32_t scale = word ? 2 : 4;
    const uint32_t maxDisp = word ? 510 : 1020;
    // mov.w uses PC+4 as is; mov.l and mova mask it to a longword, a quirk of the SH-2 that makes
    // the reachable window depend on the instruction's alignment.
    const uint32_t pc = ctx.address + 4;
    const uint32_t base = word ? pc : (pc & ~3u);
    const char* what = sp.s == S::PcW ? "mov.w" : sp.s == S::PcL ? "mov.l" : "mova";

    uint32_t disp = 0;
    if (o.kind == OK::PcRaw)
    {
        if (o.val < 0 || o.val > (int64_t)maxDisp)
            return Fail(err, o.col, Fmt("%s displacement %s out of range (0..%u, multiple of %u)", what,
                                        Hex(o.val).c_str(), maxDisp, scale));
        if ((uint32_t)o.val % scale)
            return Fail(err, o.col, Fmt("%s displacement %s must be a multiple of %u", what, Hex(o.val).c_str(), scale));
        disp = (uint32_t)o.val;
        ea = base + disp;
    }
    else
    {
        ea = (uint32_t)o.val;
        const int64_t d = (int64_t)(int32_t)(ea - base);
        if (d < 0)
            return Fail(err, o.col, Fmt("%s cannot reach backward: 0x%08X is before 0x%08X; reachable addresses %s", what,
                                        ea, base, RangeText(base, maxDisp).c_str()));
        if (d % scale)
            return Fail(err, o.col, Fmt("%s operand 0x%08X is not %u-byte aligned relative to 0x%08X; reachable addresses %s",
                                        what, ea, scale, base, RangeText(base, maxDisp).c_str()));
        if (d > (int64_t)maxDisp)
            return Fail(err, o.col, Fmt("%s operand 0x%08X is %lld bytes too far; reachable addresses %s", what, ea,
                                        (long long)(d - maxDisp), RangeText(base, maxDisp).c_str()));
        disp = (uint32_t)d;

        if (ctx.afterDelayedBranch)
        {
            // Memory cannot say whether this runs in the branch's delay slot or is jumped to; the
            // hardware's PC differs between the two, so tell the user which one they got.
            std::string w = Fmt("follows a delayed branch: assembled as reached directly (PC-relative address 0x%08X)", ea);
            if (ctx.slotTargetKnown)
            {
                const uint32_t spc = ctx.slotBranchTarget + 2;
                const uint32_t slotEa = (word ? spc : (spc & ~3u)) + disp;
                w += Fmt("; if it executes in that delay slot it loads from 0x%08X", slotEa);
            }
            out.warnings.push_back(w);
        }
    }
    field = (uint16_t)(disp / scale);
    out.canon += Fmt("@(0x%08X)", ea);
    return true;
}

bool EncodeBranch(const Spec& sp, const Operand& o, const char* mn, const Sh2AsmContext& ctx, uint16_t& field,
                  uint32_t& target, AsmError& err)
{
    const bool wide = sp.s == S::Br12;
    const int64_t lo = wide ? -4096 : -256, hi = wide ? 4094 : 254;
    const uint32_t pc = ctx.address + 4;
    target = (uint32_t)o.val;
    const int64_t d = (int64_t)(int32_t)(target - pc);
    if (d & 1)
        return Fail(err, o.col, Fmt("%s target 0x%08X is not 2-byte aligned", mn, target));
    if (d < lo || d > hi)
    {
        std::string m = Fmt("%s target 0x%08X is %lld bytes %s PC+4 (0x%08X): out of range; %s reaches %lld..+%lld bytes "
                            "(0x%08X..0x%08X)", mn, target, (long long)(d < 0 ? -d : d), d < 0 ? "behind" : "ahead of", pc,
                            mn, (long long)lo, (long long)hi, (uint32_t)(pc + lo), (uint32_t)(pc + hi));
        // A mirror alias of the same location (cache-through vs. not) is a common cause: say so
        // rather than silently folding, because the address shown carries this instruction's mirror.
        const int64_t fd = (int64_t)(int32_t)((target & 0x1FFFFFFFu) - (pc & 0x1FFFFFFFu));
        if (((target ^ pc) & 0xE0000000u) && fd >= lo && fd <= hi && !(fd & 1))
            m += Fmt("; the target is in range if the address mirror bits are ignored (use 0x%08X)",
                     (uint32_t)(pc + fd));
        return Fail(err, o.col, m);
    }
    field = (uint16_t)((d / 2) & (wide ? 0xFFF : 0xFF));
    return true;
}

bool EncodeForm(const Form& f, const Operand* ops, const Sh2AsmContext& ctx, Encoded& out, AsmError& err)
{
    out.op = f.base;
    out.canon.clear();
    for (int i = 0; i < f.n; ++i)
    {
        const Spec& sp = f.sp[i];
        const Operand& o = ops[i];
        if (i) out.canon += ",";
        switch (sp.s)
        {
        case S::Rn: out.op |= (uint16_t)(o.reg << 8); out.canon += Fmt("r%d", o.reg); break;
        case S::Rm: out.op |= (uint16_t)(o.reg << 4); out.canon += Fmt("r%d", o.reg); break;
        case S::R0:
            if (o.reg != 0)
                return Fail(err, o.col, Fmt("this form of %s requires r0 (got r%d)", f.mn, o.reg));
            out.canon += "r0";
            break;
        case S::IndN: out.op |= (uint16_t)(o.reg << 8); out.canon += Fmt("@r%d", o.reg); break;
        case S::IndM: out.op |= (uint16_t)(o.reg << 4); out.canon += Fmt("@r%d", o.reg); break;
        case S::IncN: out.op |= (uint16_t)(o.reg << 8); out.canon += Fmt("@r%d+", o.reg); break;
        case S::IncM: out.op |= (uint16_t)(o.reg << 4); out.canon += Fmt("@r%d+", o.reg); break;
        case S::DecN: out.op |= (uint16_t)(o.reg << 8); out.canon += Fmt("@-r%d", o.reg); break;
        case S::R0IdxN: out.op |= (uint16_t)(o.reg << 8); out.canon += Fmt("@(r0,r%d)", o.reg); break;
        case S::R0IdxM: out.op |= (uint16_t)(o.reg << 4); out.canon += Fmt("@(r0,r%d)", o.reg); break;
        case S::DispN: case S::DispM: case S::GbrDisp:
        {
            const int64_t scale = sp.arg;
            const bool gbr = sp.s == S::GbrDisp;
            const int64_t maxField = gbr ? 255 : 15;
            if (o.val < 0 || o.val > maxField * scale)
                return Fail(err, o.col, Fmt("displacement %s out of range for %s (0..%lld%s)", Hex(o.val).c_str(), f.mn,
                                            (long long)(maxField * scale),
                                            scale > 1 ? Fmt(", multiple of %lld", (long long)scale).c_str() : ""));
            if (o.val % scale)
                return Fail(err, o.col, Fmt("displacement %s must be a multiple of %lld for %s (valid: 0..%lld)",
                                            Hex(o.val).c_str(), (long long)scale, f.mn, (long long)(maxField * scale)));
            out.op |= (uint16_t)(o.val / scale);
            if (gbr) out.canon += Fmt("@(0x%llX,gbr)", (long long)o.val);
            else
            {
                out.op |= (uint16_t)(o.reg << (sp.s == S::DispN ? 8 : 4));
                out.canon += Fmt("@(0x%llX,r%d)", (long long)o.val, o.reg);
            }
            break;
        }
        case S::R0Gbr: out.canon += "@(r0,gbr)"; break;
        case S::ImmS:
        {
            const bool isMov = std::strcmp(f.mn, "mov") == 0;
            if (o.val < -128 || o.val > 255)
            {
                if (isMov) return Fail(err, o.col, std::string(kLiteralPoolMsg) + " (mov #imm holds -128..255)");
                return Fail(err, o.col, Fmt("immediate %lld out of range for %s (valid: -128..255)", (long long)o.val, f.mn));
            }
            const uint8_t b = (uint8_t)(o.val & 0xFF);
            out.op |= b;
            out.canon += Fmt("#0x%X", b);
            if (f.signExt && o.val >= 0x80)
                out.warnings.push_back(Fmt("note: #0x%X is sign-extended by %s to %d", b, f.mn, (int)(int8_t)b));
            break;
        }
        case S::ImmU:
            if (o.val < 0 || o.val > 255)
                return Fail(err, o.col, Fmt("immediate %lld out of range for %s (valid: 0..255)", (long long)o.val, f.mn));
            out.op |= (uint16_t)o.val;
            out.canon += Fmt("#0x%X", (unsigned)o.val);
            break;
        case S::Ctrl: out.canon += kCtrlNames[sp.arg]; break;
        case S::PcW: case S::PcL: case S::Mova:
        {
            uint16_t field = 0;
            uint32_t ea = 0;
            if (!EncodePcRel(sp, o, ctx, field, ea, out, err)) return false;
            out.op |= field;
            break;
        }
        case S::Br8: case S::Br12:
        {
            uint16_t field = 0;
            uint32_t target = 0;
            if (!EncodeBranch(sp, o, f.mn, ctx, field, target, err)) return false;
            out.op |= field;
            out.canon += Fmt("0x%08X", target);
            break;
        }
        }
    }
    return true;
}

// Does this operand text hold only what ParseOperand would accept for "mov.l #imm,rN"-style input?
bool IsLiteralPoolAttempt(const std::string& mn, int count, const Operand* ops)
{
    return (mn == "mov.l" || mn == "mov.w") && count == 2 && ops[0].kind == OK::Imm;
}

}  // namespace

// ---------------------------------------------------------------------------------------------
// Public entry
// ---------------------------------------------------------------------------------------------

Sh2AsmResult Sh2Assemble(const std::string& line, const Sh2AsmContext& ctx)
{
    Sh2AsmResult res;
    AsmError err;
    auto fail = [&](size_t col, const std::string& m) {
        res.ok = false;
        res.error = m;
        res.errorColumn = col;
        return res;
    };

    // A ';' starts a comment. Nothing in the syntax uses it, so no quoting is needed.
    size_t b = 0, e = line.size();
    const size_t semi = line.find(';');
    if (semi != std::string::npos) e = semi;
    Trim(line, b, e);
    if (b >= e) return fail(0, "empty line");

    // Accept the "Copy Instruction" layout: an 8-digit hex address first.
    {
        size_t p = b;
        while (p < e && p - b < 8 && IsHex(line[p])) ++p;
        if (p - b == 8 && (p >= e || IsBlank(line[p]) || line[p] == ':'))
        {
            size_t q = p;
            if (q < e && line[q] == ':') ++q;
            SkipBlanks(line, q, e);
            if (q < e) b = q;
        }
    }

    // Mnemonic: letters, digits and the '.', '/' used by "mov.l", "bt/s", "cmp/eq".
    size_t me = b;
    while (me < e && (IsSymChar(line[me]) || line[me] == '/')) ++me;
    if (me == b) return fail(b, "expected a mnemonic");
    std::string mn = LowerRange(line, b, me);
    if (mn == "bt.s") mn = "bt/s";
    if (mn == "bf.s") mn = "bf/s";

    // Split the operand list on top-level commas.
    struct Range { size_t b, e; };
    std::vector<Range> parts;
    {
        size_t ob = me, oe = e;
        Trim(line, ob, oe);
        if (ob < oe)
        {
            int depth = 0;
            size_t start = ob;
            for (size_t i = ob; i <= oe; ++i)
            {
                if (i < oe)
                {
                    if (line[i] == '(') ++depth;
                    else if (line[i] == ')')
                    {
                        if (--depth < 0) return fail(i, "unbalanced ')'");
                    }
                }
                if (i == oe || (line[i] == ',' && depth == 0))
                {
                    if (depth != 0) return fail(oe - 1, "missing ')'");
                    parts.push_back({ start, i });
                    start = i + 1;
                }
            }
        }
    }

    if (ctx.address & 1) return fail(0, "instruction address must be even");

    // .word: the way every encoding the decoder calls illegal is written (and rewritten).
    if (mn == ".word")
    {
        if (parts.size() != 1) return fail(me, ".word takes exactly one operand");
        size_t p = parts[0].b, pe = parts[0].e;
        Trim(line, p, pe);
        int64_t v = 0;
        if (!ParseNumberAt(line, p, pe, v, err)) return fail(err.col, err.msg);
        SkipBlanks(line, p, pe);
        if (p < pe) return fail(p, "unexpected '" + line.substr(p, pe - p) + "' after operand");
        if (v < 0 || v > 0xFFFF)
            return fail(parts[0].b, "value out of range for .word (valid: 0..0xFFFF)");
        res.ok = true;
        res.opcode = (uint16_t)v;
        res.decoded = Sh2Decode(ctx.address, res.opcode);
        return res;
    }

    bool known = false;
    for (size_t i = 0; i < kFormCount; ++i)
        if (mn == kForms[i].mn) { known = true; break; }
    if (!known) return fail(b, "unknown mnemonic '" + line.substr(b, me - b) + "'");

    // Operand count against the forms this mnemonic has.
    {
        bool countOk = false;
        std::string counts;
        int seen[3] = { 0, 0, 0 };
        for (size_t i = 0; i < kFormCount; ++i)
            if (mn == kForms[i].mn)
            {
                if (kForms[i].n == (int)parts.size()) countOk = true;
                seen[kForms[i].n] = 1;
            }
        if (!countOk)
        {
            for (int k = 0; k < 3; ++k)
                if (seen[k]) counts += (counts.empty() ? "" : " or ") + std::to_string(k);
            return fail(parts.empty() ? me : parts[0].b,
                        Fmt("'%s' takes %s operand%s, got %d", mn.c_str(), counts.c_str(),
                            counts == "1" ? "" : "s", (int)parts.size()));
        }
    }

    Operand ops[2];
    for (size_t i = 0; i < parts.size(); ++i)
        if (!ParseOperand(line, parts[i].b, parts[i].e, ctx, ops[i], err)) return fail(err.col, err.msg);

    const int count = (int)parts.size();
    if (IsLiteralPoolAttempt(mn, count, ops)) return fail(ops[0].col, kLiteralPoolMsg);

    // Try every form of this mnemonic whose operand shapes fit. The first that encodes wins; if none
    // does, the first encoding error is the most specific message available.
    Encoded enc;
    const Form* chosen = nullptr;
    AsmError firstErr;
    bool haveErr = false;
    int bestMatched = -1;
    size_t bestMismatchCol = parts.empty() ? me : parts[0].b;
    std::string forms;
    for (size_t i = 0; i < kFormCount && !chosen; ++i)
    {
        const Form& f = kForms[i];
        if (mn != f.mn || f.n != count) continue;
        forms += (forms.empty() ? "" : "; ") + FormText(f);
        int matched = 0;
        while (matched < count && ShapeMatches(f.sp[matched], ops[matched])) ++matched;
        if (matched < count)
        {
            if (matched > bestMatched) { bestMatched = matched; bestMismatchCol = ops[matched].col; }
            continue;
        }
        Encoded tryEnc;
        AsmError e2;
        if (EncodeForm(f, ops, ctx, tryEnc, e2)) { enc = tryEnc; chosen = &f; }
        else if (!haveErr) { firstErr = e2; haveErr = true; }
    }
    if (!chosen)
    {
        if (haveErr) return fail(firstErr.col, firstErr.msg);
        return fail(bestMismatchCol, "operands do not fit any form of '" + mn + "'; valid forms: " + forms);
    }

    // Decode back and require exactly what was asked for. This is what keeps a table typo from ever
    // reaching memory as a wrong opcode.
    const DisassembledInstruction d = Sh2Decode(ctx.address, enc.op);
    if (!d.IsValid || d.Opcode != enc.op || d.Mnemonic != chosen->mn || d.Operands != enc.canon)
        return fail(b, Fmt("internal error: '%s %s' encoded as 0x%04X which decodes as '%s %s'", chosen->mn,
                           enc.canon.c_str(), enc.op, d.Mnemonic.c_str(), d.Operands.c_str()));

    res.ok = true;
    res.opcode = enc.op;
    res.decoded = d;
    res.warnings = enc.warnings;

    // Delay-slot awareness: warnings only, because the same word may also be reached directly.
    if (ctx.afterDelayedBranch && d.IsBranch)
        res.warnings.push_back("illegal slot instruction: " + std::string(chosen->mn) +
                               " changes the PC but follows a delayed branch, so it may sit in its delay slot");
    if (d.HasDelaySlot && ctx.haveNext)
    {
        const DisassembledInstruction nx = Sh2Decode(ctx.address + 2, ctx.nextOpcode);
        if (nx.IsBranch)
            res.warnings.push_back(Fmt("the instruction in the delay slot (%s %s) is a branch, which is an illegal slot instruction",
                                       nx.Mnemonic.c_str(), nx.Operands.c_str()));
    }
    return res;
}

}  // namespace sfe
