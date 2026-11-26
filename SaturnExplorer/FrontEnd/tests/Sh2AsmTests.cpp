// Tests for the SH-2 assembler. The core is an exhaustive round trip against Sh2Disasm: for every
// 16-bit opcode, assembling the text the disassembler prints must give the opcode back, at several
// addresses (branch and PC-relative text is address-dependent). The assembler keeps its own table, so
// agreement here means two independent descriptions of the encoding match.
#include "Debug/Sh2Asm.h"
#include "Debug/Sh2Disasm.h"

#include <cstdio>
#include <map>
#include <string>

using namespace sfe;

namespace
{

int gFail = 0;
void Check(bool ok, const std::string& what)
{
    if (!ok) { std::printf("FAIL: %s\n", what.c_str()); ++gFail; }
}

std::string Hex4(unsigned v) { char b[16]; std::snprintf(b, sizeof(b), "0x%04X", v); return b; }
std::string Hex8(unsigned v) { char b[16]; std::snprintf(b, sizeof(b), "0x%08X", v); return b; }

Sh2AsmContext Ctx(uint32_t addr)
{
    Sh2AsmContext c;
    c.address = addr;
    return c;
}

Sh2AsmContext WithSyms(uint32_t addr)
{
    Sh2AsmContext c = Ctx(addr);
    c.resolveSymbol = [](const std::string& n, uint32_t& a, std::string& err) {
        if (n == "foo") { a = 0x06004100; return true; }
        if (n == "dup") { err = "'dup' is ambiguous: 0x06001000, 0x06002000"; return false; }
        err = "unknown symbol '" + n + "'";
        return false;
    };
    return c;
}

void Ok(const std::string& text, const Sh2AsmContext& c, unsigned want)
{
    const Sh2AsmResult r = Sh2Assemble(text, c);
    if (!r.ok) { Check(false, "'" + text + "' should assemble but: " + r.error); return; }
    Check(r.opcode == want, "'" + text + "' = " + Hex4(r.opcode) + " want " + Hex4(want));
}

// Must fail with 'needle' somewhere in the message, at column 'col' (SIZE_MAX = don't care).
void Bad(const std::string& text, const Sh2AsmContext& c, const char* needle, size_t col = (size_t)-1)
{
    const Sh2AsmResult r = Sh2Assemble(text, c);
    if (r.ok) { Check(false, "'" + text + "' should fail but gave " + Hex4(r.opcode)); return; }
    Check(r.error.find(needle) != std::string::npos,
          "'" + text + "' error '" + r.error + "' lacks '" + needle + "'");
    if (col != (size_t)-1) Check(r.errorColumn == col, "'" + text + "' column " + std::to_string(r.errorColumn) +
                                                         " want " + std::to_string(col));
}

bool HasWarning(const Sh2AsmResult& r, const char* needle)
{
    for (const std::string& w : r.warnings) if (w.find(needle) != std::string::npos) return true;
    return false;
}

std::string TextOf(const DisassembledInstruction& d)
{
    return d.Operands.empty() ? d.Mnemonic : d.Mnemonic + " " + d.Operands;
}

// ---------------------------------------------------------------------------------------------
void RoundTrip()
{
    const uint32_t addrs[] = { 0x06004000, 0x06004002, 0x00200000, 0x26004000, 0x060FFFFE };
    int illegalSeen = 0, validSeen = 0, failures = 0;
    for (uint32_t addr : addrs)
    {
        std::map<std::string, unsigned> seen;   // text -> opcode, to detect two opcodes sharing a spelling
        for (unsigned op = 0; op <= 0xFFFF; ++op)
        {
            const DisassembledInstruction d = Sh2Decode(addr, (uint16_t)op);
            const std::string text = TextOf(d);
            (d.IsValid ? validSeen : illegalSeen)++;
            auto ins = seen.insert({ text, op });
            if (!ins.second) { Check(false, "ambiguous text '" + text + "' for " + Hex4(ins.first->second) + " and " + Hex4(op)); }

            const Sh2AsmResult r = Sh2Assemble(text, Ctx(addr));
            if (!r.ok || r.opcode != op)
            {
                if (failures++ < 20)
                    Check(false, "round trip @" + Hex8(addr) + " " + Hex4(op) + " '" + text + "' -> " +
                                 (r.ok ? Hex4(r.opcode) : "error: " + r.error));
                else ++gFail;
                continue;
            }
            // The decoded copy handed back must be the disassembler's reading of the opcode.
            if (r.decoded.Mnemonic != d.Mnemonic || r.decoded.Operands != d.Operands)
                Check(false, "decoded mismatch for " + text);
        }
    }
    Check(validSeen > 0 && illegalSeen > 0, "round trip saw both valid and illegal opcodes");
    std::printf("round trip: %d valid, %d illegal (.word) decodes over %d addresses\n", validSeen, illegalSeen,
                (int)(sizeof(addrs) / sizeof(addrs[0])));
}

// Text printed for the instruction after a delayed branch uses "@(disp,pc)" and must assemble too,
// with afterDelayedBranch set (warnings allowed, errors not).
void RoundTripAfterBranch()
{
    const uint32_t addrs[] = { 0x06004000, 0x06004002, 0x26004000 };
    int checked = 0, failures = 0;
    for (uint32_t addr : addrs)
    {
        // One branch with a static target, one without.
        const DisassembledInstruction branches[2] = { Sh2Decode(addr - 2, 0xA010), Sh2Decode(addr - 2, 0x412B) };
        for (const DisassembledInstruction& br : branches)
        {
            for (unsigned op = 0; op <= 0xFFFF; ++op)
            {
                const DisassembledInstruction d = Sh2DecodeAfterBranch(addr, (uint16_t)op, br);
                Sh2AsmContext c = Ctx(addr);
                c.afterDelayedBranch = true;
                c.slotTargetKnown = br.HasBranchTarget;
                c.slotBranchTarget = br.BranchTarget;
                const Sh2AsmResult r = Sh2Assemble(TextOf(d), c);
                ++checked;
                if (!r.ok || r.opcode != op)
                {
                    if (failures++ < 20)
                        Check(false, "after-branch round trip " + Hex4(op) + " '" + TextOf(d) + "' -> " +
                                     (r.ok ? Hex4(r.opcode) : "error: " + r.error));
                    else ++gFail;
                }
            }
        }
    }
    std::printf("after-branch round trip: %d opcodes checked\n", checked);
}

// ---------------------------------------------------------------------------------------------
void Syntax()
{
    const Sh2AsmContext c = Ctx(0x06004000);
    Ok("MOV.L @R1+,R2", c, 0x6216);
    Ok("   mov.l\t @r1+ ,  r2   ", c, 0x6216);
    Ok("add #16,r1", c, 0x7110);
    Ok("add #0x10,r1", c, 0x7110);
    Ok("add #H'10,r1", c, 0x7110);
    Ok("add #$10,r1", c, 0x7110);
    Ok("add #-1,r1", c, 0x71FF);
    Ok("add #-128,r1", c, 0x7180);
    Ok("add #255,r1", c, 0x71FF);
    Ok("mov #-1,r3", c, 0xE3FF);
    Ok("cmp/eq #0x7F,r0", c, 0x887F);
    Ok("nop ; comment", c, 0x0009);
    Ok("06004000  mov r1,r2", c, 0x6213);
    Ok("06004000: nop", c, 0x0009);
    Ok("bt.s 0x06004004", c, 0x8D00);
    Ok("bf.s 0x06004004", c, 0x8F00);
    Ok("mov.l r1,@(0x3C,r2)", c, 0x121F);
    Ok("mov.l r1,@(0,r2)", c, 0x1210);
    Ok("mov.l r1,@r2", c, 0x2212);
    Ok("mov.b r0,@(r0,r5)", c, 0x0504);
    Ok("mov.b @(15,r5),r0", c, 0x845F);
    Ok("mov.w r0,@(0x1E,r5)", c, 0x815F);
    Ok("mov.l @(0x3FC,gbr),r0", c, 0xC6FF);
    Ok("tst.b #0xFF,@(r0,gbr)", c, 0xCCFF);
    Ok("stc sr,r4", c, 0x0402);
    Ok("ldc.l @r4+,vbr", c, 0x4427);
    Ok("sts.l pr,@-r15", c, 0x4F22);
    Ok("mac.w @r1+,@r2+", c, 0x421F);
    Ok(".word 0xFFFF", c, 0xFFFF);
    Ok(".word 0x0009", c, 0x0009);
    Ok(".WORD 65535", c, 0xFFFF);

    // Sign-extension note for the raw byte form.
    {
        const Sh2AsmResult r = Sh2Assemble("add #0xFF,r1", c);
        Check(r.ok && HasWarning(r, "-1"), "add #0xFF notes the sign extension");
        const Sh2AsmResult r2 = Sh2Assemble("add #0x7F,r1", c);
        Check(r2.ok && r2.warnings.empty(), "add #0x7F has no note");
    }
}

void Errors()
{
    const Sh2AsmContext c = Ctx(0x06004000);
    Bad("", c, "empty", 0);
    Bad("   ; just a comment", c, "empty", 0);
    Bad("frob r1,r2", c, "unknown mnemonic", 0);
    Bad("  frob r1,r2", c, "unknown mnemonic", 2);
    Bad("mov r1", c, "operand", 4);
    Bad("mov r1,r2,r3", c, "operand");
    Bad("nop r1", c, "operand");
    Bad("add r1", c, "operand");
    Bad("mov r16,r1", c, "out of range", 4);
    Bad("mov r1,r99", c, "out of range", 7);
    Bad("mov.b r1,@(4,r5)", c, "r0");
    Bad("mov.b @(4,r5),r1", c, "r0");
    Bad("and #1,r2", c, "r0");
    Bad("mov.l @(r1,r2),r3", c, "r0");
    Bad("add #256,r1", c, "-128..255");
    Bad("add #-129,r1", c, "-128..255");
    Bad("tst #-1,r0", c, "0..255");
    Bad("trapa #256", c, "0..255");
    Bad("mov.l r1,@(2,r2)", c, "multiple of 4");
    Bad("mov.l r1,@(0x40,r2)", c, "out of range");
    Bad("mov.w r0,@(3,r2)", c, "multiple of 2");
    Bad("mov.b r0,@(16,r2)", c, "out of range");
    Bad("mov.l @(0x400,gbr),r0", c, "out of range");
    Bad("mov.l @(0x401,gbr),r0", c, "multiple of 4");
    Bad("nop foo", c, "operand");
    Bad("mov r1,r2 r3", c, "unexpected", 10);
    Bad("mov r1 r2", c, "takes 2 operands");
    Bad("add #1x,r1", c, "malformed");
    Bad("add #99999999999999999999,r1", c, "too large");
    Bad("mov r1,", c, "missing operand");
    Bad("mov (r1,r2", c, "missing ')'");
    Bad("mov @r1,@r2", c, "do not fit");
    Bad(".word 0x10000", c, "range");
    Bad(".word", c, "operand");
    Bad("mov.l #0x12345678,r1", c, "literal-pool");
    Bad("mov.w #5,r1", c, "literal-pool");
    Bad("mov #0x100,r1", c, "literal-pool");
    Bad("mov #-129,r1", c, "literal-pool");
    Bad("bra foo", c, "symbol");   // no resolver
}

void Branches()
{
    const uint32_t A = 0x06004000, PC = A + 4;
    const Sh2AsmContext c = Ctx(A);
    auto tgt = [&](int d) { return "0x" + Hex8(PC + d).substr(2); };
    // bra/bsr: -4096..+4094
    Ok("bra " + tgt(4094), c, 0xA7FF);
    Ok("bra " + tgt(-4096), c, 0xA800);
    Ok("bsr " + tgt(0), c, 0xB000);
    Bad("bra " + tgt(4096), c, "out of range");
    Bad("bra " + tgt(-4098), c, "out of range");
    Bad("bra " + tgt(1), c, "not 2-byte aligned");
    Bad("bra " + tgt(-3), c, "not 2-byte aligned");
    // conditional: -256..+254
    for (const char* mn : { "bt", "bf", "bt/s", "bf/s" })
    {
        const unsigned base = std::string(mn) == "bt" ? 0x8900 : std::string(mn) == "bf" ? 0x8B00
                            : std::string(mn) == "bt/s" ? 0x8D00 : 0x8F00;
        Ok(std::string(mn) + " " + tgt(254), c, base | 0x7F);
        Ok(std::string(mn) + " " + tgt(-256), c, base | 0x80);
        Ok(std::string(mn) + " " + tgt(-2), c, base | 0xFF);
        Bad(std::string(mn) + " " + tgt(256), c, "out of range");
        Bad(std::string(mn) + " " + tgt(-258), c, "out of range");
        Bad(std::string(mn) + " " + tgt(3), c, "aligned");
    }
    // The error states the distance and the limit.
    Bad("bt " + tgt(300), c, "reaches -256..+254");
    // Mirror hint when the target is in range only in another mirror.
    {
        const Sh2AsmContext m = Ctx(0x26004000);
        Bad("bra 0x06004100", m, "mirror");
        Ok("bra 0x26004100", m, 0xA07E);
    }
    // Symbols, with offsets.
    {
        const Sh2AsmContext s = WithSyms(A);
        Ok("bra foo", s, 0xA07E);
        Ok("bra foo+2", s, 0xA07F);
        Ok("bra foo - 2", s, 0xA07D);
        Bad("bra dup", s, "ambiguous", 4);
        Bad("bra nosuch", s, "unknown symbol", 4);
        Bad("bra foo+", s, "expected");
    }
    // Wrap-around arithmetic like the decoder.
    {
        const Sh2AsmContext w = Ctx(0xFFFFFFF0);
        Ok("bra 0x00000000", w, 0xA006);
    }
}

void PcRelative()
{
    const Sh2AsmContext c = Ctx(0x06004000);   // PC = 0x06004004
    Ok("mov.w @(0x06004004),r1", c, 0x9100);
    Ok("mov.w @(0x06004202),r1", c, 0x91FF);
    Bad("mov.w @(0x06004204),r1", c, "reachable");
    Bad("mov.w @(0x06004101),r1", c, "aligned");
    Bad("mov.w @(0x06004002),r1", c, "backward");
    Ok("mov.w @(0x1C,pc),r3", c, 0x930E);
    Bad("mov.w @(0x1B,pc),r3", c, "multiple of 2");
    Bad("mov.w @(0x200,pc),r3", c, "out of range");
    // Long forms mask PC to a longword: at ...02 the base is 0x06004004 as well.
    const Sh2AsmContext c2 = Ctx(0x06004002);
    Ok("mov.l @(0x06004004),r1", c2, 0xD100);
    Ok("mov.l @(0x06004400),r1", c2, 0xD1FF);
    Bad("mov.l @(0x06004404),r1", c2, "reachable");
    Bad("mov.l @(0x06004006),r1", c2, "aligned");
    Bad("mov.l @(0x06004000),r1", c2, "backward");
    Ok("mova @(0x06004004),r0", c2, 0xC700);
    Ok("mova @(0x06004008),r0", c2, 0xC701);
    Bad("mova @(0x06004008),r1", c2, "r0");
    Bad("mova @(0x06004402),r0", c2, "aligned");
    Ok("mov.l @(0x3FC,pc),r2", c2, 0xD2FF);
    Bad("mov.l @(0x2,pc),r2", c2, "multiple of 4");
    // The error names the reachable range.
    Bad("mov.w @(0x07000000),r1", c, "0x06004004..0x06004202");
    // Absolute and raw spellings agree.
    {
        const Sh2AsmResult a = Sh2Assemble("mov.l @(0x06004104),r5", c2);
        const Sh2AsmResult b = Sh2Assemble("mov.l @(0x100,pc),r5", c2);
        Check(a.ok && b.ok && a.opcode == b.opcode, "absolute and raw pc spellings give one opcode");
    }
    // Symbol as the absolute operand.
    Ok("mov.l @(foo),r1", WithSyms(0x06004002), 0xD13F);
}

void DelaySlots()
{
    // A branch right after a delayed branch.
    {
        Sh2AsmContext c = Ctx(0x06004002);
        c.afterDelayedBranch = true;
        const Sh2AsmResult r = Sh2Assemble("rts", c);
        Check(r.ok && HasWarning(r, "illegal slot"), "rts after a delayed branch warns");
        const Sh2AsmResult r2 = Sh2Assemble("nop", c);
        Check(r2.ok && r2.warnings.empty(), "nop after a delayed branch does not warn");
        const Sh2AsmResult r3 = Sh2Assemble("bt 0x06004006", c);
        Check(r3.ok && HasWarning(r3, "illegal slot"), "bt after a delayed branch warns");
    }
    // PC-relative load with an absolute operand after a delayed branch.
    {
        Sh2AsmContext c = Ctx(0x06004002);
        c.afterDelayedBranch = true;
        c.slotTargetKnown = true;
        c.slotBranchTarget = 0x06005000;
        const Sh2AsmResult r = Sh2Assemble("mov.l @(0x06004104),r5", c);   // disp 0x100
        Check(r.ok && HasWarning(r, "delay slot") && HasWarning(r, "0x06005100"),
              "slot warning names the in-slot address");
        c.slotTargetKnown = false;
        const Sh2AsmResult r2 = Sh2Assemble("mov.l @(0x06004104),r5", c);
        Check(r2.ok && HasWarning(r2, "delayed branch"), "ambiguous pc-relative warns without a slot target");
        const Sh2AsmResult r3 = Sh2Assemble("mov.l @(0x100,pc),r5", c);
        Check(r3.ok && r3.warnings.empty(), "raw pc form is unambiguous: no warning");
    }
    // A delayed branch whose following word is itself a branch.
    {
        Sh2AsmContext c = Ctx(0x06004000);
        c.haveNext = true;
        c.nextOpcode = 0xA000;   // bra
        const Sh2AsmResult r = Sh2Assemble("rts", c);
        Check(r.ok && HasWarning(r, "delay slot"), "delayed branch warns about a branch in its slot");
        c.nextOpcode = 0x0009;
        const Sh2AsmResult r2 = Sh2Assemble("rts", c);
        Check(r2.ok && r2.warnings.empty(), "delayed branch with a nop in its slot is quiet");
        c.haveNext = false;
        const Sh2AsmResult r3 = Sh2Assemble("rts", c);
        Check(r3.ok && r3.warnings.empty(), "unknown next word: no warning");
    }
}

// The decode-back guard: results carry the decoder's reading of what was assembled.
void DecodedCopy()
{
    const Sh2AsmResult r = Sh2Assemble("mov.l r1,@(0x3C,r2)", Ctx(0x06004000));
    Check(r.ok && r.decoded.Mnemonic == "mov.l" && r.decoded.Operands == "r1,@(0x3C,r2)" && r.decoded.IsValid,
          "decoded copy filled in");
    const Sh2AsmResult w = Sh2Assemble(".word 0xFFFF", Ctx(0x06004000));
    Check(w.ok && !w.decoded.IsValid && w.decoded.Mnemonic == ".word", ".word decodes as illegal");
}

}  // namespace

int main()
{
    Syntax();
    Errors();
    Branches();
    PcRelative();
    DelaySlots();
    DecodedCopy();
    RoundTrip();
    RoundTripAfterBranch();
    if (gFail) { std::printf("%d failure(s)\n", gFail); return 1; }
    std::printf("Sh2Asm tests passed\n");
    return 0;
}
