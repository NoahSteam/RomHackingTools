// Sh2RegInfo — names, one-line meanings and value extraction for the SH-2 register file,
// plus the SR flag decode. Pure (no ImGui, no context): the Registers panel's "SH-2" tab
// renders it, and the tests exercise the decode directly.
//
// The order here is se_sh2_regs' own field order (r0..r15, pc, pr, sr, gbr, vbr, mach,
// macl) so an index is meaningful on both sides and Sh2RegValue is a straight lookup.
#pragma once

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>

#include "saturnexplorer/SeTypes.h"   // se_sh2_regs

namespace sfe
{

struct Sh2RegField
{
    const char* name;
    const char* desc;
};

// Indices into the table / Sh2RegValue for the registers the panel treats specially.
enum
{
    kSh2RegPc   = 16,
    kSh2RegPr   = 17,
    kSh2RegSr   = 18,
    kSh2RegGbr  = 19,
    kSh2RegVbr  = 20,
    kSh2RegMach = 21,
    kSh2RegMacl = 22,
    kSh2RegCount = 23
};

// Calling-convention notes are the Hitachi/SH ABI the Saturn's compilers follow: R4-R7 pass
// the first four arguments, R0 returns, R8-R14 are callee-saved. Games hand-written in
// assembly need not obey it, so the wording stays "by convention".
inline const Sh2RegField* Sh2RegTable()
{
    static const Sh2RegField kRegs[kSh2RegCount] = {
        {"R0",   "General register. Also the return value, and the implicit operand of the\n"
                 "R0-indexed addressing modes (@(R0,Rn), @(disp,GBR))."},
        {"R1",   "General register. Caller-saved scratch by convention."},
        {"R2",   "General register. Caller-saved scratch by convention."},
        {"R3",   "General register. Caller-saved scratch by convention."},
        {"R4",   "General register. 1st argument by convention."},
        {"R5",   "General register. 2nd argument by convention."},
        {"R6",   "General register. 3rd argument by convention."},
        {"R7",   "General register. 4th argument by convention."},
        {"R8",   "General register. Callee-saved by convention."},
        {"R9",   "General register. Callee-saved by convention."},
        {"R10",  "General register. Callee-saved by convention."},
        {"R11",  "General register. Callee-saved by convention."},
        {"R12",  "General register. Callee-saved by convention."},
        {"R13",  "General register. Callee-saved by convention."},
        {"R14",  "General register. Frame pointer by convention."},
        {"R15",  "General register R15 — the stack pointer (SP). Pushes pre-decrement it."},
        {"PC",   "Program counter: the instruction being executed. While halted this is the\n"
                 "instruction that has not run yet."},
        {"PR",   "Procedure register: the return address saved by BSR / BSRF / JSR."},
        {"SR",   "Status register: the T and S flags, the I3-I0 interrupt mask, and M/Q for\n"
                 "the stepwise divide."},
        {"GBR",  "Global base register: the base for @(disp,GBR) addressing, which the BIOS\n"
                 "and I/O helpers use to reach hardware registers in one instruction."},
        {"VBR",  "Vector base register: the base of the exception / interrupt vector table."},
        {"MACH", "Multiply-and-accumulate result, high 32 bits."},
        {"MACL", "Multiply-and-accumulate result, low 32 bits. A plain MUL lands here."},
    };
    return kRegs;
}

// Index of the register 'name' names, case-insensitively and as a whole word ("r4", "R4",
// "gbr" all match; "r1" never matches inside "r15"), or -1 for anything else.
//
// Name lookup belongs with the table rather than in each caller, which otherwise carries
// its own copy of the special-register vocabulary — and a register added here would then
// silently go unrecognised there, with nothing failing to say so.
inline int Sh2RegIndexFromName(const char* name, size_t len)
{
    if (name == nullptr || len == 0) return -1;
    const Sh2RegField* table = Sh2RegTable();
    for (int i = 0; i < kSh2RegCount; ++i)
    {
        const char* n = table[i].name;   // the table spells them upper case
        size_t k = 0;
        while (k < len && n[k] != '\0' && std::toupper((unsigned char)name[k]) == n[k]) ++k;
        if (k == len && n[k] == '\0') return i;
    }
    return -1;
}
inline int Sh2RegIndexFromName(const std::string& name)
{
    return Sh2RegIndexFromName(name.c_str(), name.size());
}

// Value of register 'index' (0..kSh2RegCount-1). Out-of-range yields 0.
inline uint32_t Sh2RegValue(const se_sh2_regs& r, int index)
{
    if (index < 0 || index >= kSh2RegCount) return 0;
    if (index < 16) return r.r[index];
    switch (index)
    {
    case kSh2RegPc:   return r.pc;
    case kSh2RegPr:   return r.pr;
    case kSh2RegSr:   return r.sr;
    case kSh2RegGbr:  return r.gbr;
    case kSh2RegVbr:  return r.vbr;
    case kSh2RegMach: return r.mach;
    default:          return r.macl;
    }
}

// The SR fields, as the compact line shown in the panel's Notes column. Bit layout:
// T = bit 0, S = bit 1, I3-I0 = bits 4-7, Q = bit 8, M = bit 9.
inline std::string Sh2SrSummary(uint32_t sr)
{
    char b[64];
    std::snprintf(b, sizeof b, "T=%u S=%u I=%u M=%u Q=%u",
                  (unsigned)(sr & 1u), (unsigned)((sr >> 1) & 1u),
                  (unsigned)((sr >> 4) & 0xFu),
                  (unsigned)((sr >> 9) & 1u), (unsigned)((sr >> 8) & 1u));
    return std::string(b);
}

// The same fields spelled out, for the SR row's hover. Interrupts at or below the mask are
// blocked, so I=15 means "all masked" and I=0 means "everything gets through".
inline std::string Sh2SrDetail(uint32_t sr)
{
    const unsigned t = sr & 1u, s = (sr >> 1) & 1u, i = (sr >> 4) & 0xFu;
    const unsigned q = (sr >> 8) & 1u, m = (sr >> 9) & 1u;
    char b[320];
    std::snprintf(b, sizeof b,
                  "T  = %u  (condition / carry-out bit tested by BT and BF)\n"
                  "S  = %u  (saturation mode for MAC)\n"
                  "I  = %u  (interrupt mask: levels <= %u are blocked%s)\n"
                  "M  = %u, Q = %u  (stepwise-divide state for DIV0x / DIV1)",
                  t, s, i, i, i == 0 ? "; none" : (i == 15 ? "; all but NMI" : ""), m, q);
    return std::string(b);
}

}  // namespace sfe
