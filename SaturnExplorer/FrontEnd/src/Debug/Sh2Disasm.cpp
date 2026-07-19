#include "Debug/Sh2Disasm.h"

#include <cstdarg>
#include <cstdio>

namespace sfe
{

namespace
{
// Small operand formatter into the instruction, keeping call sites terse.
struct Out
{
    DisassembledInstruction& ins;
    void set(const char* mn) { ins.Mnemonic = mn; ins.IsValid = true; }
    void ops(const char* fmt, ...)
    {
        char buf[64];
        va_list ap; va_start(ap, fmt);
        std::vsnprintf(buf, sizeof(buf), fmt, ap);
        va_end(ap);
        ins.Operands = buf;
    }
};

int32_t s8ext(uint16_t v) { return (int32_t)(int8_t)(v & 0xFF); }
int32_t s12(uint16_t v)   { int32_t d = v & 0xFFF; if (d & 0x800) d |= ~0xFFF; return d; }
}  // namespace

DisassembledInstruction Sh2Decode(uint32_t address, uint16_t op)
{
    DisassembledInstruction ins;
    ins.Address = address;
    ins.Opcode = op;
    Out o{ ins };

    const int n = (op >> 8) & 0xF;
    const int m = (op >> 4) & 0xF;
    const int imm = op & 0xFF;
    const int d4 = op & 0xF;

    switch (op >> 12)
    {
    case 0x0:
        switch (op & 0xF)
        {
        case 0x2:
            switch (m) {
            case 0: o.set("stc");  o.ops("sr,r%d", n);  return ins;
            case 1: o.set("stc");  o.ops("gbr,r%d", n); return ins;
            case 2: o.set("stc");  o.ops("vbr,r%d", n); return ins;
            }
            break;
        case 0x3:
            switch (m) {
            case 0: o.set("bsrf"); o.ops("r%d", n); ins.IsBranch = ins.IsCall = true; return ins;
            case 2: o.set("braf"); o.ops("r%d", n); ins.IsBranch = true; return ins;
            }
            break;
        case 0x4: o.set("mov.b"); o.ops("r%d,@(r0,r%d)", m, n); return ins;
        case 0x5: o.set("mov.w"); o.ops("r%d,@(r0,r%d)", m, n); return ins;
        case 0x6: o.set("mov.l"); o.ops("r%d,@(r0,r%d)", m, n); return ins;
        case 0x7: o.set("mul.l"); o.ops("r%d,r%d", m, n); return ins;
        case 0x8:
            switch (op) {
            case 0x0008: o.set("clrt");  ins.Operands.clear(); return ins;
            case 0x0018: o.set("sett");  ins.Operands.clear(); return ins;
            case 0x0028: o.set("clrmac");ins.Operands.clear(); return ins;
            }
            break;
        case 0x9:
            if (op == 0x0009) { o.set("nop");   ins.Operands.clear(); return ins; }
            if (op == 0x0019) { o.set("div0u"); ins.Operands.clear(); return ins; }
            if ((op & 0xF0FF) == 0x0029) { o.set("movt"); o.ops("r%d", n); return ins; }
            break;
        case 0xA:
            switch (m) {
            case 0: o.set("sts"); o.ops("mach,r%d", n); return ins;
            case 1: o.set("sts"); o.ops("macl,r%d", n); return ins;
            case 2: o.set("sts"); o.ops("pr,r%d", n);   return ins;
            }
            break;
        case 0xB:
            switch (op) {
            case 0x000B: o.set("rts");   ins.Operands.clear(); ins.IsBranch = ins.IsReturn = true; return ins;
            case 0x001B: o.set("sleep"); ins.Operands.clear(); return ins;
            case 0x002B: o.set("rte");   ins.Operands.clear(); ins.IsBranch = ins.IsReturn = true; return ins;
            }
            break;
        case 0xC: o.set("mov.b"); o.ops("@(r0,r%d),r%d", m, n); return ins;
        case 0xD: o.set("mov.w"); o.ops("@(r0,r%d),r%d", m, n); return ins;
        case 0xE: o.set("mov.l"); o.ops("@(r0,r%d),r%d", m, n); return ins;
        case 0xF: o.set("mac.l"); o.ops("@r%d+,@r%d+", m, n); return ins;
        }
        break;

    case 0x1:  o.set("mov.l"); o.ops("r%d,@(0x%X,r%d)", m, d4 * 4, n); return ins;

    case 0x2:
        switch (op & 0xF) {
        case 0x0: o.set("mov.b");  o.ops("r%d,@r%d", m, n); return ins;
        case 0x1: o.set("mov.w");  o.ops("r%d,@r%d", m, n); return ins;
        case 0x2: o.set("mov.l");  o.ops("r%d,@r%d", m, n); return ins;
        case 0x4: o.set("mov.b");  o.ops("r%d,@-r%d", m, n); return ins;
        case 0x5: o.set("mov.w");  o.ops("r%d,@-r%d", m, n); return ins;
        case 0x6: o.set("mov.l");  o.ops("r%d,@-r%d", m, n); return ins;
        case 0x7: o.set("div0s");  o.ops("r%d,r%d", m, n); return ins;
        case 0x8: o.set("tst");    o.ops("r%d,r%d", m, n); return ins;
        case 0x9: o.set("and");    o.ops("r%d,r%d", m, n); return ins;
        case 0xA: o.set("xor");    o.ops("r%d,r%d", m, n); return ins;
        case 0xB: o.set("or");     o.ops("r%d,r%d", m, n); return ins;
        case 0xC: o.set("cmp/str");o.ops("r%d,r%d", m, n); return ins;
        case 0xD: o.set("xtrct");  o.ops("r%d,r%d", m, n); return ins;
        case 0xE: o.set("mulu.w"); o.ops("r%d,r%d", m, n); return ins;
        case 0xF: o.set("muls.w"); o.ops("r%d,r%d", m, n); return ins;
        }
        break;

    case 0x3:
        switch (op & 0xF) {
        case 0x0: o.set("cmp/eq"); o.ops("r%d,r%d", m, n); return ins;
        case 0x2: o.set("cmp/hs"); o.ops("r%d,r%d", m, n); return ins;
        case 0x3: o.set("cmp/ge"); o.ops("r%d,r%d", m, n); return ins;
        case 0x4: o.set("div1");   o.ops("r%d,r%d", m, n); return ins;
        case 0x5: o.set("dmulu.l");o.ops("r%d,r%d", m, n); return ins;
        case 0x6: o.set("cmp/hi"); o.ops("r%d,r%d", m, n); return ins;
        case 0x7: o.set("cmp/gt"); o.ops("r%d,r%d", m, n); return ins;
        case 0x8: o.set("sub");    o.ops("r%d,r%d", m, n); return ins;
        case 0xA: o.set("subc");   o.ops("r%d,r%d", m, n); return ins;
        case 0xB: o.set("subv");   o.ops("r%d,r%d", m, n); return ins;
        case 0xC: o.set("add");    o.ops("r%d,r%d", m, n); return ins;
        case 0xD: o.set("dmuls.l");o.ops("r%d,r%d", m, n); return ins;
        case 0xE: o.set("addc");   o.ops("r%d,r%d", m, n); return ins;
        case 0xF: o.set("addv");   o.ops("r%d,r%d", m, n); return ins;
        }
        break;

    case 0x4:
        switch (op & 0xFF) {
        case 0x00: o.set("shll");  o.ops("r%d", n); return ins;
        case 0x01: o.set("shlr");  o.ops("r%d", n); return ins;
        case 0x02: o.set("sts.l"); o.ops("mach,@-r%d", n); return ins;
        case 0x03: o.set("stc.l"); o.ops("sr,@-r%d", n); return ins;
        case 0x04: o.set("rotl");  o.ops("r%d", n); return ins;
        case 0x05: o.set("rotr");  o.ops("r%d", n); return ins;
        case 0x06: o.set("lds.l"); o.ops("@r%d+,mach", n); return ins;
        case 0x07: o.set("ldc.l"); o.ops("@r%d+,sr", n); return ins;
        case 0x08: o.set("shll2"); o.ops("r%d", n); return ins;
        case 0x09: o.set("shlr2"); o.ops("r%d", n); return ins;
        case 0x0A: o.set("lds");   o.ops("r%d,mach", n); return ins;
        case 0x0B: o.set("jsr");   o.ops("@r%d", n); ins.IsBranch = ins.IsCall = true; return ins;
        case 0x0E: o.set("ldc");   o.ops("r%d,sr", n); return ins;
        case 0x10: o.set("dt");    o.ops("r%d", n); return ins;
        case 0x11: o.set("cmp/pz");o.ops("r%d", n); return ins;
        case 0x12: o.set("sts.l"); o.ops("macl,@-r%d", n); return ins;
        case 0x13: o.set("stc.l"); o.ops("gbr,@-r%d", n); return ins;
        case 0x15: o.set("cmp/pl");o.ops("r%d", n); return ins;
        case 0x16: o.set("lds.l"); o.ops("@r%d+,macl", n); return ins;
        case 0x17: o.set("ldc.l"); o.ops("@r%d+,gbr", n); return ins;
        case 0x18: o.set("shll8"); o.ops("r%d", n); return ins;
        case 0x19: o.set("shlr8"); o.ops("r%d", n); return ins;
        case 0x1A: o.set("lds");   o.ops("r%d,macl", n); return ins;
        case 0x1B: o.set("tas.b"); o.ops("@r%d", n); return ins;
        case 0x1E: o.set("ldc");   o.ops("r%d,gbr", n); return ins;
        case 0x20: o.set("shal");  o.ops("r%d", n); return ins;
        case 0x21: o.set("shar");  o.ops("r%d", n); return ins;
        case 0x22: o.set("sts.l"); o.ops("pr,@-r%d", n); return ins;
        case 0x23: o.set("stc.l"); o.ops("vbr,@-r%d", n); return ins;
        case 0x24: o.set("rotcl"); o.ops("r%d", n); return ins;
        case 0x25: o.set("rotcr"); o.ops("r%d", n); return ins;
        case 0x26: o.set("lds.l"); o.ops("@r%d+,pr", n); return ins;
        case 0x27: o.set("ldc.l"); o.ops("@r%d+,vbr", n); return ins;
        case 0x28: o.set("shll16");o.ops("r%d", n); return ins;
        case 0x29: o.set("shlr16");o.ops("r%d", n); return ins;
        case 0x2A: o.set("lds");   o.ops("r%d,pr", n); return ins;
        case 0x2B: o.set("jmp");   o.ops("@r%d", n); ins.IsBranch = true; return ins;
        case 0x2E: o.set("ldc");   o.ops("r%d,vbr", n); return ins;
        case 0x0F: o.set("mac.w"); o.ops("@r%d+,@r%d+", m, n); return ins;
        }
        break;

    case 0x5:  o.set("mov.l"); o.ops("@(0x%X,r%d),r%d", d4 * 4, m, n); return ins;

    case 0x6:
        switch (op & 0xF) {
        case 0x0: o.set("mov.b"); o.ops("@r%d,r%d", m, n); return ins;
        case 0x1: o.set("mov.w"); o.ops("@r%d,r%d", m, n); return ins;
        case 0x2: o.set("mov.l"); o.ops("@r%d,r%d", m, n); return ins;
        case 0x3: o.set("mov");   o.ops("r%d,r%d", m, n); return ins;
        case 0x4: o.set("mov.b"); o.ops("@r%d+,r%d", m, n); return ins;
        case 0x5: o.set("mov.w"); o.ops("@r%d+,r%d", m, n); return ins;
        case 0x6: o.set("mov.l"); o.ops("@r%d+,r%d", m, n); return ins;
        case 0x7: o.set("not");   o.ops("r%d,r%d", m, n); return ins;
        case 0x8: o.set("swap.b");o.ops("r%d,r%d", m, n); return ins;
        case 0x9: o.set("swap.w");o.ops("r%d,r%d", m, n); return ins;
        case 0xA: o.set("negc");  o.ops("r%d,r%d", m, n); return ins;
        case 0xB: o.set("neg");   o.ops("r%d,r%d", m, n); return ins;
        case 0xC: o.set("extu.b");o.ops("r%d,r%d", m, n); return ins;
        case 0xD: o.set("extu.w");o.ops("r%d,r%d", m, n); return ins;
        case 0xE: o.set("exts.b");o.ops("r%d,r%d", m, n); return ins;
        case 0xF: o.set("exts.w");o.ops("r%d,r%d", m, n); return ins;
        }
        break;

    case 0x7:  o.set("add"); o.ops("#0x%X,r%d", (uint8_t)imm, n); return ins;

    case 0x8:
        switch ((op >> 8) & 0xF) {
        case 0x0: o.set("mov.b"); o.ops("r0,@(0x%X,r%d)", d4, m); return ins;
        case 0x1: o.set("mov.w"); o.ops("r0,@(0x%X,r%d)", d4 * 2, m); return ins;
        case 0x4: o.set("mov.b"); o.ops("@(0x%X,r%d),r0", d4, m); return ins;
        case 0x5: o.set("mov.w"); o.ops("@(0x%X,r%d),r0", d4 * 2, m); return ins;
        case 0x8: o.set("cmp/eq");o.ops("#0x%X,r0", (uint8_t)imm); return ins;
        // Conditional PC-relative branches: same 8-bit-disp target, differ only by
        // mnemonic (bt/bf and their delayed-slot variants).
        case 0x9: case 0xB: case 0xD: case 0xF: {
            const int sub = (op >> 8) & 0xF;
            const char* mn = sub == 0x9 ? "bt" : sub == 0xB ? "bf"
                           : sub == 0xD ? "bt.s" : "bf.s";
            const uint32_t t = address + 4 + s8ext(op) * 2;
            o.set(mn); o.ops("0x%08X", t);
            ins.HasBranchTarget = true; ins.BranchTarget = t;
            ins.IsBranch = ins.IsConditional = true; return ins; }
        }
        break;

    case 0x9: { const uint32_t t = address + 4 + (op & 0xFF) * 2;
                o.set("mov.w"); o.ops("@(0x%08X),r%d", t, n); return ins; }

    case 0xA: { const uint32_t t = address + 4 + s12(op) * 2;
                o.set("bra"); o.ops("0x%08X", t); ins.HasBranchTarget = true; ins.BranchTarget = t;
                ins.IsBranch = true; return ins; }

    case 0xB: { const uint32_t t = address + 4 + s12(op) * 2;
                o.set("bsr"); o.ops("0x%08X", t); ins.HasBranchTarget = true; ins.BranchTarget = t;
                ins.IsBranch = ins.IsCall = true; return ins; }

    case 0xC:
        switch ((op >> 8) & 0xF) {
        case 0x0: o.set("mov.b"); o.ops("r0,@(0x%X,gbr)", imm); return ins;
        case 0x1: o.set("mov.w"); o.ops("r0,@(0x%X,gbr)", imm * 2); return ins;
        case 0x2: o.set("mov.l"); o.ops("r0,@(0x%X,gbr)", imm * 4); return ins;
        case 0x3: o.set("trapa"); o.ops("#0x%X", imm); ins.IsCall = true; return ins;
        case 0x4: o.set("mov.b"); o.ops("@(0x%X,gbr),r0", imm); return ins;
        case 0x5: o.set("mov.w"); o.ops("@(0x%X,gbr),r0", imm * 2); return ins;
        case 0x6: o.set("mov.l"); o.ops("@(0x%X,gbr),r0", imm * 4); return ins;
        case 0x7: { const uint32_t t = (address & ~3u) + 4 + imm * 4;
                    o.set("mova"); o.ops("@(0x%08X),r0", t); return ins; }
        case 0x8: o.set("tst");   o.ops("#0x%X,r0", imm); return ins;
        case 0x9: o.set("and");   o.ops("#0x%X,r0", imm); return ins;
        case 0xA: o.set("xor");   o.ops("#0x%X,r0", imm); return ins;
        case 0xB: o.set("or");    o.ops("#0x%X,r0", imm); return ins;
        case 0xC: o.set("tst.b"); o.ops("#0x%X,@(r0,gbr)", imm); return ins;
        case 0xD: o.set("and.b"); o.ops("#0x%X,@(r0,gbr)", imm); return ins;
        case 0xE: o.set("xor.b"); o.ops("#0x%X,@(r0,gbr)", imm); return ins;
        case 0xF: o.set("or.b");  o.ops("#0x%X,@(r0,gbr)", imm); return ins;
        }
        break;

    case 0xD: { const uint32_t t = (address & ~3u) + 4 + (op & 0xFF) * 4;
                o.set("mov.l"); o.ops("@(0x%08X),r%d", t, n); return ins; }

    case 0xE:  o.set("mov"); o.ops("#0x%X,r%d", (uint8_t)imm, n); return ins;

    case 0xF:  break;   // no FPU on plain SH-2: illegal
    }

    // Fell through: illegal / unimplemented.
    ins.Mnemonic = ".word";
    o.ops("0x%04X", op);
    ins.IsValid = false;
    return ins;
}

DisassembledInstruction Sh2DecodeAt(uint32_t address, const uint8_t* bytes, size_t size)
{
    if (!bytes || size < 2)
    {
        DisassembledInstruction ins;
        ins.Address = address;
        ins.Mnemonic = "????";
        ins.IsValid = false;
        return ins;
    }
    const uint16_t op = (uint16_t)((bytes[0] << 8) | bytes[1]);   // big-endian
    return Sh2Decode(address, op);
}

}  // namespace sfe
