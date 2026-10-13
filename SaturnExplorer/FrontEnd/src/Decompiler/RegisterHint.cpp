#include "Decompiler/RegisterHint.h"

namespace sfe
{
namespace decomp
{

RegisterHint RegisterTargetAtHalt(const MemorySnapshot& snap, int cpu)
{
    RegisterHint h;
    if (cpu < 0 || cpu > 1 || !snap.haveRegs[cpu]) return h;
    const se_sh2_regs& regs = snap.regs[cpu];
    uint16_t op = 0;
    if (!snap.ReadU16(regs.pc, op)) return h;
    const int n = (op >> 8) & 0xF;
    bool pcRelative = false;
    switch (op & 0xF0FF)
    {
    case 0x400B: h.mnemonic = "jsr"; break;
    case 0x402B: h.mnemonic = "jmp"; break;
    case 0x0023: h.mnemonic = "braf"; pcRelative = true; break;
    case 0x0003: h.mnemonic = "bsrf"; pcRelative = true; break;
    default: return h;
    }
    h.valid = true;
    h.insn = regs.pc;
    h.reg = n;
    h.value = regs.r[n];
    h.target = pcRelative ? regs.pc + 4 + regs.r[n] : regs.r[n];
    return h;
}

}  // namespace decomp
}  // namespace sfe
