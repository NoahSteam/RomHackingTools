// Area 2 oracle (REVIEW_PROGRAM.md): SH-2 instruction semantics and the SLEIGH spec, checked against
// Mednafen's SH-2 core -- the hardware reference -- in two ways.
//
// 1. Exhaustive decode. Every one of the 65,536 sixteen-bit words is decoded three times: by the
//    SLEIGH translator the decompiler uses (the checked-in sh-2.sla through SaturnArchitecture,
//    disassembly only), by the project's own decoder Sh2Disasm (the debugger's listing), and by a
//    table transcribed from Mednafen's sh7095_opdefs.inc (the op ids and what each one is) and
//    sh7095_idecodetab.inc (which word is which op). The three must agree on validity, mnemonic and
//    operands; every disagreement is a finding, attributed to the side that differs from Mednafen.
//
// 2. Differential execution. Every instruction form runs on the vendored p-code emulator with the
//    shipped translator, from random register, flag and memory states, and the result is compared
//    with a reference model transcribed from Mednafen's sh7095_ops.inc (and the PC rules of
//    sh7095.inc): all sixteen registers, SR (T, S, Q, M and the rest), GBR, VBR, MACH, MACL, PR,
//    the next PC and every word of memory the instruction wrote. Branches run with their delay slot
//    (the slot is drawn from every non-branch form), taken and not taken, and the PC the slot sees
//    is the hardware's, not the slot's own address plus four.
//
// What it cannot see is listed in 15_DECOMPILER_STEP5.md ("Area 2, oracle"): exceptions (address
// error, illegal instruction, illegal slot), SLEEP (a user op), misaligned accesses, code at the
// top of the address space, and anything that depends on the bus rather than flat memory.
//
// One difference from the hardware is known and accepted (ledger A2-4): after a taken delayed branch
// other than bra and bsr the spec cannot place a PC-relative slot's literal. The oracle runs a case
// that differs a second time against a reference with that one rule, counts it as a known limit if
// that agrees exactly (any other difference in the same case still fails), and checks that the
// decompiler marks a function with such a slot unsupported.
//
// usage: SaturnExplorerSh2DecodeTests <scratch dir>
// environment: SE_DECODE_CASES=<n>   differential cases per form (default 2000)
//              SE_DECODE_SEED=<n>    seed of the differential run (default fixed)
//              SE_DECODE_FORM=<s>    only the forms whose name contains s
//              SE_DECODE_SHOW=<n>    failing cases printed per form (default 2)
//              SE_DECODE_DUMP="<hex> ..."  print the three decoders' text for these words
//              SE_DECODE_PCODE=1     print the p-code of the named shapes selected by SE_DECODE_FORM
//              SE_MEDNAFEN_SS=<dir>  Mednafen's ss/ directory: re-checks the transcribed word-to-op
//                                    table against sh7095_idecodetab.inc, all 65,536 words
#include "DecompilerTestSupport.h"

#include "Debug/Sh2Disasm.h"
#include "Decompiler/SaturnArchitecture.h"
#include "Decompiler/Sh2Decompiler.h"
#include "emulate.hh"
#include "Sh2EmulatorHooks.h"
#include "memstate.hh"

#include <algorithm>
#include <cstring>
#include <fstream>
#include <map>
#include <set>
#include <sstream>

using namespace sfe::decomp;

namespace
{

std::string gConfig;

// ---- Mednafen's instruction set (sh7095_opdefs.inc, sh7095_idecodetab.inc) ----------------------
//
// An op is what Mednafen's decoder returns for a word. 'ops' is the operand list in Mednafen's
// own terms, position-named: Rn is bits 8..11 and Rm bits 4..7 whatever the manual calls them
// (MOV.B R0,@(disp,Rn) keeps its register in bits 4..7 and is written R0,@(D4B,Rm) here).
// Tokens: #S8 #U8 immediate; D4B/D4W/D4L and D8B/D8W/D8L displacement times 1, 2, 4; PCW PCL the
// address a PC-relative load reads; BR8 BR12 a branch target; CTRL SR/GBR/VBR and SYS MACH/MACL/PR
// chosen by bits 4..5. 'mem' is the size in bytes of the memory the op reads or writes (0: none).
struct OpDef
{
    int id;
    const char* name;
    const char* mnem;
    const char* ops;
    int mem;
};

// A word is op 'op' when (word & mask) == value; every other word is Mednafen's op 0, illegal.
struct Pat
{
    uint16_t mask, value;
    uint8_t op;
};

const OpDef kOps[] = {
    { 0x01, "MOV_IMM_REG", "mov", "#S8,Rn", 0 },
    { 0x02, "MOV_W_PCREL_REG", "mov.w", "PCW,Rn", 0 },
    { 0x03, "MOV_L_PCREL_REG", "mov.l", "PCL,Rn", 0 },
    { 0x04, "MOV_REG_REG", "mov", "Rm,Rn", 0 },
    { 0x05, "MOV_B_REG_REGINDIR", "mov.b", "Rm,@Rn", 1 },
    { 0x06, "MOV_W_REG_REGINDIR", "mov.w", "Rm,@Rn", 2 },
    { 0x07, "MOV_L_REG_REGINDIR", "mov.l", "Rm,@Rn", 4 },
    { 0x08, "MOV_B_REGINDIR_REG", "mov.b", "@Rm,Rn", 1 },
    { 0x09, "MOV_W_REGINDIR_REG", "mov.w", "@Rm,Rn", 2 },
    { 0x0A, "MOV_L_REGINDIR_REG", "mov.l", "@Rm,Rn", 4 },
    { 0x0B, "MOV_B_REG_REGINDIRPD", "mov.b", "Rm,@-Rn", 1 },
    { 0x0C, "MOV_W_REG_REGINDIRPD", "mov.w", "Rm,@-Rn", 2 },
    { 0x0D, "MOV_L_REG_REGINDIRPD", "mov.l", "Rm,@-Rn", 4 },
    { 0x0E, "MOV_B_REGINDIRPI_REG", "mov.b", "@Rm+,Rn", 1 },
    { 0x0F, "MOV_W_REGINDIRPI_REG", "mov.w", "@Rm+,Rn", 2 },
    { 0x10, "MOV_L_REGINDIRPI_REG", "mov.l", "@Rm+,Rn", 4 },
    { 0x11, "MOV_B_REG0_REGINDIRDISP", "mov.b", "R0,@(D4B,Rm)", 1 },
    { 0x12, "MOV_W_REG0_REGINDIRDISP", "mov.w", "R0,@(D4W,Rm)", 2 },
    { 0x13, "MOV_L_REG_REGINDIRDISP", "mov.l", "Rm,@(D4L,Rn)", 4 },
    { 0x14, "MOV_B_REGINDIRDISP_REG0", "mov.b", "@(D4B,Rm),R0", 1 },
    { 0x15, "MOV_W_REGINDIRDISP_REG0", "mov.w", "@(D4W,Rm),R0", 2 },
    { 0x16, "MOV_L_REGINDIRDISP_REG", "mov.l", "@(D4L,Rm),Rn", 4 },
    { 0x17, "MOV_B_REG_IDXREGINDIR", "mov.b", "Rm,@(R0,Rn)", 1 },
    { 0x18, "MOV_W_REG_IDXREGINDIR", "mov.w", "Rm,@(R0,Rn)", 2 },
    { 0x19, "MOV_L_REG_IDXREGINDIR", "mov.l", "Rm,@(R0,Rn)", 4 },
    { 0x1A, "MOV_B_IDXREGINDIR_REG", "mov.b", "@(R0,Rm),Rn", 1 },
    { 0x1B, "MOV_W_IDXREGINDIR_REG", "mov.w", "@(R0,Rm),Rn", 2 },
    { 0x1C, "MOV_L_IDXREGINDIR_REG", "mov.l", "@(R0,Rm),Rn", 4 },
    { 0x1D, "MOV_B_REG0_GBRINDIRDISP", "mov.b", "R0,@(D8B,GBR)", 1 },
    { 0x1E, "MOV_W_REG0_GBRINDIRDISP", "mov.w", "R0,@(D8W,GBR)", 2 },
    { 0x1F, "MOV_L_REG0_GBRINDIRDISP", "mov.l", "R0,@(D8L,GBR)", 4 },
    { 0x20, "MOV_B_GBRINDIRDISP_REG0", "mov.b", "@(D8B,GBR),R0", 1 },
    { 0x21, "MOV_W_GBRINDIRDISP_REG0", "mov.w", "@(D8W,GBR),R0", 2 },
    { 0x22, "MOV_L_GBRINDIRDISP_REG0", "mov.l", "@(D8L,GBR),R0", 4 },
    { 0x23, "MOVA_PCREL_REG0", "mova", "PCL,R0", 0 },
    { 0x24, "MOVT_REG", "movt", "Rn", 0 },
    { 0x25, "SWAP_B_REG_REG", "swap.b", "Rm,Rn", 0 },
    { 0x26, "SWAP_W_REG_REG", "swap.w", "Rm,Rn", 0 },
    { 0x27, "XTRCT_REG_REG", "xtrct", "Rm,Rn", 0 },
    { 0x28, "ADD_REG_REG", "add", "Rm,Rn", 0 },
    { 0x29, "ADD_IMM_REG", "add", "#S8,Rn", 0 },
    { 0x2A, "ADDC_REG_REG", "addc", "Rm,Rn", 0 },
    { 0x2B, "ADDV_REG_REG", "addv", "Rm,Rn", 0 },
    { 0x2C, "CMP_EQ_IMM_REG0", "cmp/eq", "#S8,R0", 0 },
    { 0x2D, "CMP_EQ_REG_REG", "cmp/eq", "Rm,Rn", 0 },
    { 0x2E, "CMP_HS_REG_REG", "cmp/hs", "Rm,Rn", 0 },
    { 0x2F, "CMP_GE_REG_REG", "cmp/ge", "Rm,Rn", 0 },
    { 0x30, "CMP_HI_REG_REG", "cmp/hi", "Rm,Rn", 0 },
    { 0x31, "CMP_GT_REG_REG", "cmp/gt", "Rm,Rn", 0 },
    { 0x32, "CMP_PZ_REG", "cmp/pz", "Rn", 0 },
    { 0x33, "CMP_PL_REG", "cmp/pl", "Rn", 0 },
    { 0x34, "CMP_STR_REG_REG", "cmp/str", "Rm,Rn", 0 },
    { 0x35, "DIV1_REG_REG", "div1", "Rm,Rn", 0 },
    { 0x36, "DIV0S_REG_REG", "div0s", "Rm,Rn", 0 },
    { 0x37, "DIV0U", "div0u", "", 0 },
    { 0x38, "DMULS_L_REG_REG", "dmuls.l", "Rm,Rn", 0 },
    { 0x39, "DMULU_L_REG_REG", "dmulu.l", "Rm,Rn", 0 },
    { 0x3A, "DT", "dt", "Rn", 0 },
    { 0x3B, "EXTS_B_REG_REG", "exts.b", "Rm,Rn", 0 },
    { 0x3C, "EXTS_W_REG_REG", "exts.w", "Rm,Rn", 0 },
    { 0x3D, "EXTU_B_REG_REG", "extu.b", "Rm,Rn", 0 },
    { 0x3E, "EXTU_W_REG_REG", "extu.w", "Rm,Rn", 0 },
    { 0x3F, "MAC_L", "mac.l", "@Rm+,@Rn+", 4 },
    { 0x40, "MAC_W", "mac.w", "@Rm+,@Rn+", 2 },
    { 0x41, "MUL_L_REG_REG", "mul.l", "Rm,Rn", 0 },
    { 0x42, "MULS_W_REG_REG", "muls.w", "Rm,Rn", 0 },
    { 0x43, "MULU_W_REG_REG", "mulu.w", "Rm,Rn", 0 },
    { 0x44, "NEG_REG_REG", "neg", "Rm,Rn", 0 },
    { 0x45, "NEGC_REG_REG", "negc", "Rm,Rn", 0 },
    { 0x46, "SUB_REG_REG", "sub", "Rm,Rn", 0 },
    { 0x47, "SUBC_REG_REG", "subc", "Rm,Rn", 0 },
    { 0x48, "SUBV_REG_REG", "subv", "Rm,Rn", 0 },
    { 0x49, "AND_REG_REG", "and", "Rm,Rn", 0 },
    { 0x4A, "AND_IMM_REG0", "and", "#U8,R0", 0 },
    { 0x4B, "AND_B_IMM_IDXGBRINDIR", "and.b", "#U8,@(R0,GBR)", 1 },
    { 0x4C, "NOT_REG_REG", "not", "Rm,Rn", 0 },
    { 0x4D, "OR_REG_REG", "or", "Rm,Rn", 0 },
    { 0x4E, "OR_IMM_REG0", "or", "#U8,R0", 0 },
    { 0x4F, "OR_B_IMM_IDXGBRINDIR", "or.b", "#U8,@(R0,GBR)", 1 },
    { 0x50, "TAS_B_REGINDIR", "tas.b", "@Rn", 1 },
    { 0x51, "TST_REG_REG", "tst", "Rm,Rn", 0 },
    { 0x52, "TST_IMM_REG0", "tst", "#U8,R0", 0 },
    { 0x53, "TST_B_IMM_IDXGBRINDIR", "tst.b", "#U8,@(R0,GBR)", 1 },
    { 0x54, "XOR_REG_REG", "xor", "Rm,Rn", 0 },
    { 0x55, "XOR_IMM_REG0", "xor", "#U8,R0", 0 },
    { 0x56, "XOR_B_IMM_IDXGBRINDIR", "xor.b", "#U8,@(R0,GBR)", 1 },
    { 0x57, "ROTL_REG", "rotl", "Rn", 0 },
    { 0x58, "ROTR_REG", "rotr", "Rn", 0 },
    { 0x59, "ROTCL_REG", "rotcl", "Rn", 0 },
    { 0x5A, "ROTCR_REG", "rotcr", "Rn", 0 },
    { 0x5B, "SHAR_REG", "shar", "Rn", 0 },
    { 0x5C, "SHLL_REG", "shll", "Rn", 0 },
    { 0x5D, "SHLR_REG", "shlr", "Rn", 0 },
    { 0x5E, "SHLL2_REG", "shll2", "Rn", 0 },
    { 0x5F, "SHLR2_REG", "shlr2", "Rn", 0 },
    { 0x60, "SHLL8_REG", "shll8", "Rn", 0 },
    { 0x61, "SHLR8_REG", "shlr8", "Rn", 0 },
    { 0x62, "SHLL16_REG", "shll16", "Rn", 0 },
    { 0x63, "SHLR16_REG", "shlr16", "Rn", 0 },
    { 0x64, "BF", "bf", "BR8", 0 },
    { 0x65, "BF_S", "bf/s", "BR8", 0 },
    { 0x66, "BT", "bt", "BR8", 0 },
    { 0x67, "BT_S", "bt/s", "BR8", 0 },
    { 0x68, "BRA", "bra", "BR12", 0 },
    { 0x69, "BRAF_REG", "braf", "Rn", 0 },
    { 0x6A, "BSR", "bsr", "BR12", 0 },
    { 0x6B, "BSRF_REG", "bsrf", "Rn", 0 },
    { 0x6C, "JMP_REGINDIR", "jmp", "@Rn", 0 },
    { 0x6D, "JSR_REGINDIR", "jsr", "@Rn", 0 },
    { 0x6E, "RTS", "rts", "", 0 },
    { 0x6F, "CLRT", "clrt", "", 0 },
    { 0x70, "CLRMAC", "clrmac", "", 0 },
    { 0x71, "LDC", "ldc", "Rn,CTRL", 0 },
    { 0x72, "LDC_L", "ldc.l", "@Rn+,CTRL", 4 },
    { 0x73, "LDS", "lds", "Rn,SYS", 0 },
    { 0x74, "LDS_L", "lds.l", "@Rn+,SYS", 4 },
    { 0x75, "NOP", "nop", "", 0 },
    { 0x76, "RTE", "rte", "", 4 },
    { 0x77, "SETT", "sett", "", 0 },
    { 0x78, "SLEEP", "sleep", "", 0 },
    { 0x79, "STC", "stc", "CTRL,Rn", 0 },
    { 0x7A, "STC_L", "stc.l", "CTRL,@-Rn", 4 },
    { 0x7B, "STS", "sts", "SYS,Rn", 0 },
    { 0x7C, "STS_L", "sts.l", "SYS,@-Rn", 4 },
    { 0x7D, "TRAPA", "trapa", "#U8", 4 },
};
const Pat kPats[] = {
    { 0xF000, 0xE000, 0x01 }, { 0xF000, 0x9000, 0x02 }, { 0xF000, 0xD000, 0x03 }, { 0xF00F, 0x6003, 0x04 },
    { 0xF00F, 0x2000, 0x05 }, { 0xF00F, 0x2001, 0x06 }, { 0xF00F, 0x2002, 0x07 }, { 0xF00F, 0x6000, 0x08 },
    { 0xF00F, 0x6001, 0x09 }, { 0xF00F, 0x6002, 0x0A }, { 0xF00F, 0x2004, 0x0B }, { 0xF00F, 0x2005, 0x0C },
    { 0xF00F, 0x2006, 0x0D }, { 0xF00F, 0x6004, 0x0E }, { 0xF00F, 0x6005, 0x0F }, { 0xF00F, 0x6006, 0x10 },
    { 0xFF00, 0x8000, 0x11 }, { 0xFF00, 0x8100, 0x12 }, { 0xF000, 0x1000, 0x13 }, { 0xFF00, 0x8400, 0x14 },
    { 0xFF00, 0x8500, 0x15 }, { 0xF000, 0x5000, 0x16 }, { 0xF00F, 0x0004, 0x17 }, { 0xF00F, 0x0005, 0x18 },
    { 0xF00F, 0x0006, 0x19 }, { 0xF00F, 0x000C, 0x1A }, { 0xF00F, 0x000D, 0x1B }, { 0xF00F, 0x000E, 0x1C },
    { 0xFF00, 0xC000, 0x1D }, { 0xFF00, 0xC100, 0x1E }, { 0xFF00, 0xC200, 0x1F }, { 0xFF00, 0xC400, 0x20 },
    { 0xFF00, 0xC500, 0x21 }, { 0xFF00, 0xC600, 0x22 }, { 0xFF00, 0xC700, 0x23 }, { 0xF0FF, 0x0029, 0x24 },
    { 0xF00F, 0x6008, 0x25 }, { 0xF00F, 0x6009, 0x26 }, { 0xF00F, 0x200D, 0x27 }, { 0xF00F, 0x300C, 0x28 },
    { 0xF000, 0x7000, 0x29 }, { 0xF00F, 0x300E, 0x2A }, { 0xF00F, 0x300F, 0x2B }, { 0xFF00, 0x8800, 0x2C },
    { 0xF00F, 0x3000, 0x2D }, { 0xF00F, 0x3002, 0x2E }, { 0xF00F, 0x3003, 0x2F }, { 0xF00F, 0x3006, 0x30 },
    { 0xF00F, 0x3007, 0x31 }, { 0xF0FF, 0x4011, 0x32 }, { 0xF0FF, 0x4015, 0x33 }, { 0xF00F, 0x200C, 0x34 },
    { 0xF00F, 0x3004, 0x35 }, { 0xF00F, 0x2007, 0x36 }, { 0xFFFF, 0x0019, 0x37 }, { 0xF00F, 0x300D, 0x38 },
    { 0xF00F, 0x3005, 0x39 }, { 0xF0FF, 0x4010, 0x3A }, { 0xF00F, 0x600E, 0x3B }, { 0xF00F, 0x600F, 0x3C },
    { 0xF00F, 0x600C, 0x3D }, { 0xF00F, 0x600D, 0x3E }, { 0xF00F, 0x000F, 0x3F }, { 0xF00F, 0x400F, 0x40 },
    { 0xF00F, 0x0007, 0x41 }, { 0xF00F, 0x200F, 0x42 }, { 0xF00F, 0x200E, 0x43 }, { 0xF00F, 0x600B, 0x44 },
    { 0xF00F, 0x600A, 0x45 }, { 0xF00F, 0x3008, 0x46 }, { 0xF00F, 0x300A, 0x47 }, { 0xF00F, 0x300B, 0x48 },
    { 0xF00F, 0x2009, 0x49 }, { 0xFF00, 0xC900, 0x4A }, { 0xFF00, 0xCD00, 0x4B }, { 0xF00F, 0x6007, 0x4C },
    { 0xF00F, 0x200B, 0x4D }, { 0xFF00, 0xCB00, 0x4E }, { 0xFF00, 0xCF00, 0x4F }, { 0xF0FF, 0x401B, 0x50 },
    { 0xF00F, 0x2008, 0x51 }, { 0xFF00, 0xC800, 0x52 }, { 0xFF00, 0xCC00, 0x53 }, { 0xF00F, 0x200A, 0x54 },
    { 0xFF00, 0xCA00, 0x55 }, { 0xFF00, 0xCE00, 0x56 }, { 0xF0FF, 0x4004, 0x57 }, { 0xF0FF, 0x4005, 0x58 },
    { 0xF0FF, 0x4024, 0x59 }, { 0xF0FF, 0x4025, 0x5A }, { 0xF0FF, 0x4021, 0x5B }, { 0xF0DF, 0x4000, 0x5C },
    { 0xF0FF, 0x4001, 0x5D }, { 0xF0FF, 0x4008, 0x5E }, { 0xF0FF, 0x4009, 0x5F }, { 0xF0FF, 0x4018, 0x60 },
    { 0xF0FF, 0x4019, 0x61 }, { 0xF0FF, 0x4028, 0x62 }, { 0xF0FF, 0x4029, 0x63 }, { 0xFF00, 0x8B00, 0x64 },
    { 0xFF00, 0x8F00, 0x65 }, { 0xFF00, 0x8900, 0x66 }, { 0xFF00, 0x8D00, 0x67 }, { 0xF000, 0xA000, 0x68 },
    { 0xF0FF, 0x0023, 0x69 }, { 0xF000, 0xB000, 0x6A }, { 0xF0FF, 0x0003, 0x6B }, { 0xF0FF, 0x402B, 0x6C },
    { 0xF0FF, 0x400B, 0x6D }, { 0xFFFF, 0x000B, 0x6E }, { 0xFFFF, 0x0008, 0x6F }, { 0xFFFF, 0x0028, 0x70 },
    { 0xF0FF, 0x400E, 0x71 }, { 0xF0FF, 0x401E, 0x71 }, { 0xF0FF, 0x402E, 0x71 }, { 0xF0FF, 0x4007, 0x72 },
    { 0xF0FF, 0x4017, 0x72 }, { 0xF0FF, 0x4027, 0x72 }, { 0xF0FF, 0x400A, 0x73 }, { 0xF0FF, 0x401A, 0x73 },
    { 0xF0FF, 0x402A, 0x73 }, { 0xF0FF, 0x4006, 0x74 }, { 0xF0FF, 0x4016, 0x74 }, { 0xF0FF, 0x4026, 0x74 },
    { 0xFFFF, 0x0009, 0x75 }, { 0xFFFF, 0x002B, 0x76 }, { 0xFFFF, 0x0018, 0x77 }, { 0xFFFF, 0x001B, 0x78 },
    { 0xF0FF, 0x0002, 0x79 }, { 0xF0FF, 0x0012, 0x79 }, { 0xF0FF, 0x0022, 0x79 }, { 0xF0FF, 0x4003, 0x7A },
    { 0xF0FF, 0x4013, 0x7A }, { 0xF0FF, 0x4023, 0x7A }, { 0xF0FF, 0x000A, 0x7B }, { 0xF0FF, 0x001A, 0x7B },
    { 0xF0FF, 0x002A, 0x7B }, { 0xF0FF, 0x4002, 0x7C }, { 0xF0FF, 0x4012, 0x7C }, { 0xF0FF, 0x4022, 0x7C },
    { 0xFF00, 0xC300, 0x7D },
};


const OpDef* gOpDef[256];        // by op id
uint8_t gOpOf[65536];            // Mednafen's decode of every word

void BuildTables()
{
    std::memset(gOpDef, 0, sizeof gOpDef);
    for (const OpDef& d : kOps) gOpDef[d.id] = &d;
    for (int w = 0; w < 65536; ++w)
    {
        gOpOf[w] = 0;
        for (const Pat& p : kPats)
            if ((w & p.mask) == p.value) { gOpOf[w] = p.op; break; }
    }
}

// Re-check the transcribed patterns against Mednafen's own flat table, when a checkout is given.
// Returns the number of words that differ, or -1 when the table could not be read.
int VerifyAgainstMednafen(const char* dir)
{
    std::ifstream in(std::string(dir) + "/sh7095_idecodetab.inc", std::ios::binary);
    if (!in) return -1;
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::vector<int> tab;
    for (size_t i = 0; i + 3 < text.size(); ++i)
        if (text[i] == '0' && text[i + 1] == 'x')
        {
            tab.push_back(static_cast<int>(std::strtol(text.c_str() + i + 2, nullptr, 16)));
            i += 3;
        }
    if (tab.size() != 65536) return -1;
    int bad = 0;
    for (int w = 0; w < 65536; ++w)
        if (tab[w] != gOpOf[w]) ++bad;
    return bad;
}

std::string Hex8(uint32_t v)
{
    char b[16];
    std::snprintf(b, sizeof b, "0x%08x", v);
    return b;
}

std::string Dec(long long v) { return std::to_string(v); }

// Mednafen's operand list for a word at 'addr', in the normal form (see Canon).
std::string RenderOps(const char* t, uint16_t w, uint32_t addr)
{
    static const char* kCtrl[] = { "sr", "gbr", "vbr", "?" };
    static const char* kSys[] = { "mach", "macl", "pr", "?" };
    const unsigned n = (w >> 8) & 15, m = (w >> 4) & 15;
    std::string out;
    for (const char* p = t; *p;)
    {
        auto is = [&](const char* tok) { return std::strncmp(p, tok, std::strlen(tok)) == 0; };
        if (is("#S8") || is("#U8")) { out += "#" + Dec(w & 0xFF); p += 3; }
        else if (is("D4B")) { out += Dec(w & 0xF); p += 3; }
        else if (is("D4W")) { out += Dec((w & 0xF) * 2); p += 3; }
        else if (is("D4L")) { out += Dec((w & 0xF) * 4); p += 3; }
        else if (is("D8B")) { out += Dec(w & 0xFF); p += 3; }
        else if (is("D8W")) { out += Dec((w & 0xFF) * 2); p += 3; }
        else if (is("D8L")) { out += Dec((w & 0xFF) * 4); p += 3; }
        else if (is("PCW")) { out += "@[" + Hex8(addr + 4 + (w & 0xFF) * 2) + "]"; p += 3; }
        else if (is("PCL")) { out += "@[" + Hex8(((addr + 4) & ~3u) + (w & 0xFF) * 4) + "]"; p += 3; }
        else if (is("BR12"))
        {
            int32_t d = w & 0xFFF;
            if (d & 0x800) d -= 0x1000;
            out += Hex8(addr + 4 + static_cast<uint32_t>(d * 2));
            p += 4;
        }
        else if (is("BR8")) { out += Hex8(addr + 4 + static_cast<uint32_t>(static_cast<int8_t>(w) * 2)); p += 3; }
        else if (is("CTRL")) { out += kCtrl[(w >> 4) & 3]; p += 4; }
        else if (is("SYS")) { out += kSys[(w >> 4) & 3]; p += 3; }
        else if (is("GBR")) { out += "gbr"; p += 3; }
        else if (is("R0")) { out += "r0"; p += 2; }
        else if (is("Rn")) { out += "r" + Dec(n); p += 2; }
        else if (is("Rm")) { out += "r" + Dec(m); p += 2; }
        else out += *p++;
    }
    return out;
}

// ---- Normalising the two decoders' text ------------------------------------------------------------
//
// The three decoders spell the same operands differently, so each one's text is rewritten into the
// normal form RenderOps produces. Only spelling is normalised away:
//   - case, spaces, and the optional '#';
//   - an 8-bit immediate is compared modulo 256 (Sh2Disasm prints add #-4 as #0xFC, SLEIGH as -0x4;
//     the sign extension is checked by the execution half, not here);
//   - a displacement is a decimal byte count, already scaled by the operand size;
//   - a PC-relative operand is the address it reads, written @[0x........]: SLEIGH prints
//     "@(0xADDR,pc)" and Sh2Disasm "@(0xADDR)";
//   - a branch target is the absolute address;
//   - "shal" is "shll": Mednafen decodes 4n20 and 4n00 as the same op (sh7095_idecodetab.inc maps
//     both to SHLL_REG) because the two instructions are one operation.
// Registers, system register names, the @-forms, the mnemonic and the operand order are compared as
// written.

long long ParseNum(const std::string& s)
{
    size_t i = 0;
    bool neg = false;
    if (i < s.size() && s[i] == '-') { neg = true; ++i; }
    int base = 10;
    if (s.compare(i, 2, "0x") == 0) { base = 16; i += 2; }
    const long long v = std::strtoll(s.c_str() + i, nullptr, base);
    return neg ? -v : v;
}

bool IsNumber(const std::string& s)
{
    if (s.empty()) return false;
    const char c = s[0];
    return std::isdigit(static_cast<unsigned char>(c)) || (c == '-' && s.size() > 1 && std::isdigit(static_cast<unsigned char>(s[1])));
}

std::vector<std::string> SplitTop(const std::string& s)
{
    std::vector<std::string> parts;
    int depth = 0;
    std::string cur;
    for (char c : s)
    {
        if (c == '(') ++depth;
        if (c == ')') --depth;
        if (c == ',' && depth == 0) { parts.push_back(cur); cur.clear(); }
        else cur += c;
    }
    if (!cur.empty() || !parts.empty()) parts.push_back(cur);
    return parts;
}

std::string Lower(std::string s)
{
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string CanonMnemonic(const std::string& mn)
{
    std::string m = Lower(mn);
    if (m == "shal") return "shll";
    return m;
}

std::string CanonOps(const std::string& mnemonic, const std::string& text)
{
    std::string s;
    for (char c : text)
        if (c != ' ') s += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    const std::string mn = Lower(mnemonic);
    const bool branchTarget = mn == "bt" || mn == "bf" || mn == "bt/s" || mn == "bf/s" || mn == "bra" || mn == "bsr";
    std::string out;
    bool first = true;
    for (const std::string& p : SplitTop(s))
    {
        std::string o;
        if (p.compare(0, 2, "@(") == 0 && p.back() == ')')
        {
            const std::vector<std::string> in = SplitTop(p.substr(2, p.size() - 3));
            if (in.size() == 2)
            {
                if (in[0] == "r0") o = "@(r0," + in[1] + ")";
                else if (in[1] == "pc") o = "@[" + Hex8(static_cast<uint32_t>(ParseNum(in[0]))) + "]";
                else o = "@(" + Dec(ParseNum(in[0])) + "," + in[1] + ")";
            }
            else if (in.size() == 1 && IsNumber(in[0])) o = "@[" + Hex8(static_cast<uint32_t>(ParseNum(in[0]))) + "]";
            else o = p;
        }
        else if (!p.empty() && p[0] == '#') o = "#" + Dec(ParseNum(p.substr(1)) & 0xFF);
        else if (IsNumber(p))
            o = branchTarget ? Hex8(static_cast<uint32_t>(ParseNum(p))) : "#" + Dec(ParseNum(p) & 0xFF);
        else o = p;
        if (!first) out += ",";
        out += o;
        first = false;
    }
    return out;
}

// ---- The three decoders -------------------------------------------------------------------------------

// The ops with a delay slot: the branches that go through UCDelayBranch / DelayBranch in sh7095.inc.
bool IsDelayedOp(int op)
{
    return op == 0x65 || op == 0x67 || op == 0x68 || op == 0x69 || op == 0x6A || op == 0x6B || op == 0x6C ||
           op == 0x6D || op == 0x6E || op == 0x76;
}

struct Decoded
{
    bool valid = false;
    std::string mn, ops;   // normal form
    std::string flags;     // control-flow facts (Sh2Disasm's IsBranch and friends), normal form
    std::string raw;       // as printed, for the report
};

// What Sh2Disasm.h defines IsBranch (any control-flow change), IsCall (bsr bsrf jsr trapa), IsReturn
// (rts rte), IsConditional, HasDelaySlot and HasBranchTarget to mean, for a Mednafen op.
std::string MednafenFlags(int op, uint16_t w, uint32_t addr)
{
    std::string f;
    if ((op >= 0x64 && op <= 0x6E) || op == 0x76 || op == 0x7D) f += "branch ";
    if (op == 0x6A || op == 0x6B || op == 0x6D || op == 0x7D) f += "call ";
    if (op == 0x6E || op == 0x76) f += "return ";
    if (op >= 0x64 && op <= 0x67) f += "conditional ";
    if (IsDelayedOp(op)) f += "delay-slot ";
    if ((op >= 0x64 && op <= 0x68) || op == 0x6A) f += "target=" + RenderOps(gOpDef[op]->ops, w, addr);
    return f;
}

Decoded MednafenDecode(uint16_t w, uint32_t addr)
{
    Decoded d;
    const int op = gOpOf[w];
    if (!op) return d;
    const OpDef& def = *gOpDef[op];
    d.valid = true;
    d.mn = def.mnem;
    d.ops = RenderOps(def.ops, w, addr);
    d.flags = MednafenFlags(op, w, addr);
    d.raw = std::string(def.name) + " " + def.mnem + " " + def.ops;
    return d;
}

Decoded DisasmDecode(uint16_t w, uint32_t addr)
{
    Decoded d;
    const sfe::DisassembledInstruction ins = sfe::Sh2Decode(addr, w);
    d.raw = ins.Mnemonic + " " + ins.Operands;
    if (!ins.IsValid) return d;
    d.valid = true;
    d.mn = CanonMnemonic(ins.Mnemonic);
    d.ops = CanonOps(ins.Mnemonic, ins.Operands);
    if (ins.IsBranch) d.flags += "branch ";
    if (ins.IsCall) d.flags += "call ";
    if (ins.IsReturn) d.flags += "return ";
    if (ins.IsConditional) d.flags += "conditional ";
    if (ins.HasDelaySlot) d.flags += "delay-slot ";
    if (ins.HasBranchTarget) d.flags += "target=" + Hex8(ins.BranchTarget);
    return d;
}

struct DisEmit : public ghidra::AssemblyEmit
{
    std::string mn, body;
    bool got = false;
    void dump(const ghidra::Address&, const std::string& m, const std::string& b) override
    {
        mn = m;
        body = b;
        got = true;
    }
};

}  // namespace

namespace
{

// ---- Oracle 1: exhaustive decode -----------------------------------------------------------------------

struct Group
{
    int count = 0;
    int layouts = 0;       // bit 0: word at an address = 0 mod 4, bit 1: = 2 mod 4
    uint16_t first[6] = {};
    int nfirst = 0;
    uint32_t addr = 0;
    std::string expected, got;   // the first example
};

struct SideReport
{
    const char* name;
    std::map<std::string, Group> groups;
    long valid = 0, total = 0, disagree = 0;
    bool flags;   // the decoder reports control-flow flags (Sh2Disasm does; SLEIGH's output is text only)
};

void Note(SideReport& r, const char* kind, const Decoded& e, const Decoded& x, uint16_t w, uint32_t addr, int layout,
          bool flags = false)
{
    const int op = gOpOf[w];
    std::string key = std::string(kind) + " | " + (op ? gOpDef[op]->name : "(illegal in Mednafen)") + " | " +
                      (x.valid ? x.mn : "(undefined)");
    if (!op) key += " | word>>8=" + Hex8(w >> 8).substr(6);
    Group& g = r.groups[key];
    if (!g.count)
    {
        g.addr = addr;
        g.expected = e.valid ? e.mn + " " + e.ops : "(illegal)";
        g.got = x.valid ? x.raw : "(undefined)";
        if (flags)
        {
            g.expected += "   [" + e.flags + "]";
            g.got += "   [" + x.flags + "]";
        }
    }
    if (g.nfirst < 6) g.first[g.nfirst++] = w;
    g.count++;
    g.layouts |= 1 << layout;
    r.disagree++;
}

void CompareDecoders(SideReport& r, const Decoded& e, const Decoded& x, uint16_t w, uint32_t addr, int layout)
{
    r.total++;
    if (x.valid) r.valid++;
    if (e.valid && !x.valid) Note(r, "rejects a word Mednafen defines", e, x, w, addr, layout);
    else if (!e.valid && x.valid) Note(r, "accepts a word Mednafen leaves illegal", e, x, w, addr, layout);
    else if (e.valid && e.mn != x.mn) Note(r, "mnemonic", e, x, w, addr, layout);
    else if (e.valid && e.ops != x.ops) Note(r, "operands", e, x, w, addr, layout);
    else if (r.flags && e.valid && e.flags != x.flags) Note(r, "control-flow flags", e, x, w, addr, layout, true);
}

void PrintReport(const SideReport& r)
{
    std::printf("  %s: %ld words defined of %ld, %ld disagreements with Mednafen in %zu groups\n", r.name, r.valid,
                r.total, r.disagree, r.groups.size());
    for (const auto& kv : r.groups)
    {
        const Group& g = kv.second;
        std::string where;
        static const char* kWhere[] = { "RAM 0 mod 4", "RAM 2 mod 4", "address 0", "top" };
        for (int i = 0; i < 4; ++i)
            if (g.layouts & (1 << i)) where += std::string(where.empty() ? "" : ", ") + kWhere[i];
        std::printf("    [%s] x%d (at: %s) words", kv.first.c_str(), g.count, where.c_str());
        for (int i = 0; i < g.nfirst; ++i) std::printf(" %04X", g.first[i]);
        std::printf("\n        Mednafen: %s\n        %s: %s   (first word %04X at %08X)\n", g.expected.c_str(), r.name,
                    g.got.c_str(), g.first[0], g.addr);
    }
}

void TestDecode()
{
    SideReport sleigh{ "SLEIGH", {}, 0, 0, 0, false }, disasm{ "Sh2Disasm", {}, 0, 0, 0, true };
    const char* dump = std::getenv("SE_DECODE_DUMP");   // space-separated hex words to print
    long mednafenValid = 0;
    for (int w = 0; w < 65536; ++w)
        if (gOpOf[w]) ++mednafenValid;
    std::printf("Mednafen defines %ld of 65536 words (%d ops, %zu patterns)\n", mednafenValid,
                static_cast<int>(sizeof kOps / sizeof kOps[0]), sizeof kPats / sizeof kPats[0]);

    // Where each word is decoded. A PC-relative operand depends on the address, so the words are laid
    // out at both alignments in RAM (each with a nop beside it: after it at 0 mod 4, before it at
    // 2 mod 4; a context left behind by an earlier instruction would show). The third layout is the
    // bottom of the address space, where a backward branch wraps below zero. The fourth is the top,
    // where a forward one wraps past 2^32; the engine cannot fetch code there (on-chip space, not
    // memory), so only Sh2Disasm, a pure function, is checked at it.
    const uint32_t kBase[4] = { 0x06000000u, 0x06000002u, 0x00000000u, 0xFFFC0000u };
    for (int layout = 0; layout < 4; ++layout)
    {
        const bool engine = layout < 3;
        std::unique_ptr<SaturnArchitecture> arch;
        std::ostringstream errors;
        ghidra::AddrSpace* ram = nullptr;
        std::shared_ptr<MemorySnapshot> snap;
        if (engine)
        {
            std::vector<uint8_t> img(65536 * 4, 0);
            for (int w = 0; w < 65536; ++w)
            {
                const size_t at = size_t(w) * 4 + (layout == 1 ? 2 : 0);
                dts::Put16(img, at, static_cast<uint16_t>(w));
                dts::Put16(img, layout == 1 ? at - 2 : at + 2, 0x0009);
            }
            if (layout == 2)
            {
                snap = dts::MakeSnapshot({});
                MemoryRegion bios;
                bios.name = "BIOS";
                bios.base = 0;
                bios.bytes = img;
                snap->regions.push_back(std::move(bios));
                snap->key.regionSetHash = snap->ComputeRegionSetHash();
            }
            else snap = dts::MakeSnapshot(img);
            arch.reset(new SaturnArchitecture(snap, &errors));
            arch->Initialise(20000, nullptr);
            ram = arch->getDefaultCodeSpace();
        }
        for (int w = 0; w < 65536; ++w)
        {
            const uint32_t addr = kBase[layout] + static_cast<uint32_t>(w) * 4;
            const Decoded e = MednafenDecode(static_cast<uint16_t>(w), addr);
            const Decoded d = DisasmDecode(static_cast<uint16_t>(w), addr);
            Decoded s;
            if (engine)
            {
                DisEmit emit;
                try
                {
                    arch->translate->printAssembly(emit, ghidra::Address(ram, addr));
                    if (emit.got)
                    {
                        s.valid = true;
                        s.mn = CanonMnemonic(emit.mn);
                        s.ops = CanonOps(emit.mn, emit.body);
                        s.raw = emit.mn + " " + emit.body;
                    }
                }
                catch (ghidra::LowlevelError&)
                {
                }
            }
            if (dump && layout == 0 && std::strstr(dump, Hex8(w).substr(6).c_str()))
                std::printf("  %04X at %08X: Mednafen '%s %s'  SLEIGH '%s'  Sh2Disasm '%s'\n", w, addr, e.mn.c_str(),
                            e.ops.c_str(), s.valid ? s.raw.c_str() : "(undefined)", d.valid ? d.raw.c_str() : "(undefined)");
            if (engine) CompareDecoders(sleigh, e, s, static_cast<uint16_t>(w), addr, layout);
            CompareDecoders(disasm, e, d, static_cast<uint16_t>(w), addr, layout);
        }
    }
    // The totals count each word once per layout; report words, not word-layouts.
    sleigh.total /= 3;
    sleigh.valid /= 3;
    disasm.total /= 4;
    disasm.valid /= 4;
    std::printf("decode comparison (a word is counted once per address layout it disagrees at: 0 mod 4 and 2 mod 4 in RAM,\n"
                "bottom of the address space, and for Sh2Disasm alone the top):\n");
    PrintReport(sleigh);
    PrintReport(disasm);
    CHECK(sleigh.disagree == 0);
    CHECK(disasm.disagree == 0);
}

}  // namespace

namespace
{

// ---- Oracle 2: differential execution -----------------------------------------------------------------
//
// One case is: an instruction (and, for a delayed branch, its slot) placed in the snapshot's code,
// a random register file and memory, run once on each side.
//
//   emulator   the vendored p-code emulator on the shipped translator. Registers live in the
//              register space; memory is a bank whose unwritten bytes are a hash of (seed, address),
//              so no memory has to be filled in, and which words the instruction wrote is logged.
//   reference  ExecRef below, transcribed from Mednafen: sh7095_ops.inc for what each op does,
//              sh7095.inc (UCDelayBranch, DelayBranch, Branch, Exception) for the PC. Mednafen runs
//              an instruction with z->PC = its address + 4; a taken branch sets z->PC to the target
//              and the post-switch "PC += 2" makes it target + 2 while the delay slot runs.

// xorshift64*, as in Sh2SemanticsTests.
struct Rng
{
    uint64_t s;
    explicit Rng(uint64_t seed) : s(seed * 0x9E3779B97F4A7C15ull + 0x1234567ull)
    {
        for (int i = 0; i < 4; ++i) Next();
    }
    uint32_t Next()
    {
        s ^= s >> 12;
        s ^= s << 25;
        s ^= s >> 27;
        return static_cast<uint32_t>((s * 2685821657736338717ull) >> 32);
    }
    uint32_t Below(uint32_t n) { return Next() % n; }
    // Biased towards the values where carries, saturation and sign changes happen.
    uint32_t Operand()
    {
        static const uint32_t edges[] = { 0u, 1u, 0xFFFFFFFFu, 0x7FFFFFFFu, 0x80000000u, 0x00010001u, 0xFFFF0000u,
                                          0x0000FFFFu, 0x00008000u, 0xFFFF8000u, 0x00007FFFu, 0x80000001u,
                                          0x000000FFu, 0xFFFFFF00u, 0x0000007Fu, 0xFFFFFF80u, 0x00000080u };
        const uint32_t r = Next();
        return (r & 3) == 0 ? edges[Next() % (sizeof edges / sizeof edges[0])] : Next();
    }
};

enum { kSR = 16, kGBR, kVBR, kMACH, kMACL, kPR, kRegs };
const char* const kRegNames[kRegs] = { "r0",  "r1",  "r2",  "r3",  "r4",  "r5",  "r6",  "r7",  "r8",   "r9", "r10",
                                       "r11", "r12", "r13", "r14", "r15", "sr",  "gbr", "vbr", "mach", "macl", "pr" };

struct Cpu
{
    uint32_t v[kRegs];
};

const uint32_t kSrMask = 0x3F3;   // the SR bits that exist: T S I3-I0 Q M (Mednafen masks every SR load)
const uint32_t kT = 1u, kS = 2u, kQ = 0x100u, kM = 0x200u;

// Memory with defined contents everywhere: unwritten bytes are a hash of (seed, address), 'pre'
// holds what a case sets up (a stack frame, a vector), 'wr' the writes.
struct Mem
{
    uint64_t seed = 0;
    std::vector<std::pair<uint32_t, uint8_t>> pre, wr;

    uint8_t Fill(uint32_t a) const
    {
        uint64_t x = seed ^ (uint64_t(a) * 0x9E3779B97F4A7C15ull);
        x ^= x >> 29;
        x *= 0xBF58476D1CE4E5B9ull;
        x ^= x >> 32;
        return static_cast<uint8_t>(x);
    }
    uint8_t Peek8(uint32_t a) const
    {
        for (size_t i = wr.size(); i-- > 0;)
            if (wr[i].first == a) return wr[i].second;
        for (size_t i = pre.size(); i-- > 0;)
            if (pre[i].first == a) return pre[i].second;
        return Fill(a);
    }
    void Wr8(uint32_t a, uint8_t v)
    {
        for (auto& e : wr)
            if (e.first == a) { e.second = v; return; }
        wr.emplace_back(a, v);
    }
    uint32_t Rd(uint32_t a, int n) const
    {
        uint32_t v = 0;
        for (int i = 0; i < n; ++i) v = (v << 8) | Peek8(a + static_cast<uint32_t>(i));
        return v;
    }
    void Wr(uint32_t a, int n, uint32_t v)
    {
        for (int i = 0; i < n; ++i) Wr8(a + static_cast<uint32_t>(i), static_cast<uint8_t>(v >> (8 * (n - 1 - i))));
    }
    void Pre32(uint32_t a, uint32_t v)
    {
        for (uint32_t i = 0; i < 4; ++i) pre.emplace_back(a + i, static_cast<uint8_t>(v >> (24 - 8 * i)));
    }
    uint64_t Word(uint32_t ind) const   // the aligned 8-byte word at 'ind', as a big-endian number
    {
        uint64_t v = 0;
        for (uint32_t i = 0; i < 8; ++i) v = (v << 8) | Peek8(ind + i);
        return v;
    }
    std::vector<uint32_t> WrittenWords() const
    {
        std::vector<uint32_t> w;
        for (const auto& e : wr) w.push_back(e.first & ~7u);
        std::sort(w.begin(), w.end());
        w.erase(std::unique(w.begin(), w.end()), w.end());
        return w;
    }
};

// ---- The reference: Mednafen's instruction semantics --------------------------------------------------

struct Flow
{
    bool delayed = false;   // has a delay slot
    bool taken = false;     // control goes to 'target' after the slot
    uint32_t target = 0;
};

uint32_t Sx8(uint32_t v) { return static_cast<uint32_t>(static_cast<int32_t>(static_cast<int8_t>(v))); }
uint32_t Sx16(uint32_t v) { return static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(v))); }
uint32_t Sx12(uint32_t v) { return static_cast<uint32_t>((static_cast<int32_t>(v & 0xFFF) ^ 0x800) - 0x800); }

// One instruction. 'PC' is Mednafen's z->PC at execution.
Flow ExecRef(Cpu& c, Mem& m, uint16_t w, uint32_t PC)
{
    Flow f;
    uint32_t* R = c.v;
    const unsigned n = (w >> 8) & 15, mm = (w >> 4) & 15;
    const uint32_t d8 = w & 0xFF, d4 = w & 0xF;
    const uint32_t simm = Sx8(w);
    const unsigned cri = (w >> 4) & 3;   // the control / system register of ldc, lds, stc, sts
    auto T = [&]() -> uint32_t { return c.v[kSR] & 1u; };
    auto setT = [&](bool b) { c.v[kSR] = (c.v[kSR] & ~1u) | (b ? 1u : 0u); };
    switch (gOpOf[w])
    {
    case 0x01: R[n] = simm; break;
    case 0x02: R[n] = Sx16(m.Rd(PC + d8 * 2, 2)); break;
    case 0x03: R[n] = m.Rd((PC & ~3u) + d8 * 4, 4); break;
    case 0x04: R[n] = R[mm]; break;
    case 0x05: m.Wr(R[n], 1, R[mm]); break;
    case 0x06: m.Wr(R[n], 2, R[mm]); break;
    case 0x07: m.Wr(R[n], 4, R[mm]); break;
    case 0x08: R[n] = Sx8(m.Rd(R[mm], 1)); break;
    case 0x09: R[n] = Sx16(m.Rd(R[mm], 2)); break;
    case 0x0A: R[n] = m.Rd(R[mm], 4); break;
    case 0x0B: { const uint32_t v = R[mm]; R[n] -= 1; m.Wr(R[n], 1, v); break; }
    case 0x0C: { const uint32_t v = R[mm]; R[n] -= 2; m.Wr(R[n], 2, v); break; }
    case 0x0D: { const uint32_t v = R[mm]; R[n] -= 4; m.Wr(R[n], 4, v); break; }
    case 0x0E: { const uint32_t ea = R[mm]; R[mm] += 1; R[n] = Sx8(m.Rd(ea, 1)); break; }
    case 0x0F: { const uint32_t ea = R[mm]; R[mm] += 2; R[n] = Sx16(m.Rd(ea, 2)); break; }
    case 0x10: { const uint32_t ea = R[mm]; R[mm] += 4; R[n] = m.Rd(ea, 4); break; }
    case 0x11: m.Wr(R[mm] + d4, 1, R[0]); break;
    case 0x12: m.Wr(R[mm] + d4 * 2, 2, R[0]); break;
    case 0x13: m.Wr(R[n] + d4 * 4, 4, R[mm]); break;
    case 0x14: R[0] = Sx8(m.Rd(R[mm] + d4, 1)); break;
    case 0x15: R[0] = Sx16(m.Rd(R[mm] + d4 * 2, 2)); break;
    case 0x16: R[n] = m.Rd(R[mm] + d4 * 4, 4); break;
    case 0x17: m.Wr(R[0] + R[n], 1, R[mm]); break;
    case 0x18: m.Wr(R[0] + R[n], 2, R[mm]); break;
    case 0x19: m.Wr(R[0] + R[n], 4, R[mm]); break;
    case 0x1A: R[n] = Sx8(m.Rd(R[0] + R[mm], 1)); break;
    case 0x1B: R[n] = Sx16(m.Rd(R[0] + R[mm], 2)); break;
    case 0x1C: R[n] = m.Rd(R[0] + R[mm], 4); break;
    case 0x1D: m.Wr(c.v[kGBR] + d8, 1, R[0]); break;
    case 0x1E: m.Wr(c.v[kGBR] + d8 * 2, 2, R[0]); break;
    case 0x1F: m.Wr(c.v[kGBR] + d8 * 4, 4, R[0]); break;
    case 0x20: R[0] = Sx8(m.Rd(c.v[kGBR] + d8, 1)); break;
    case 0x21: R[0] = Sx16(m.Rd(c.v[kGBR] + d8 * 2, 2)); break;
    case 0x22: R[0] = m.Rd(c.v[kGBR] + d8 * 4, 4); break;
    case 0x23: R[0] = (PC & ~3u) + d8 * 4; break;
    case 0x24: R[n] = T(); break;
    case 0x25: { const uint32_t v = R[mm]; R[n] = (v & 0xFFFF0000u) | ((v << 8) & 0xFF00u) | ((v >> 8) & 0xFFu); break; }
    case 0x26: { const uint32_t v = R[mm]; R[n] = (v << 16) | (v >> 16); break; }
    case 0x27: R[n] = (R[n] >> 16) | (R[mm] << 16); break;
    case 0x28: R[n] = R[n] + R[mm]; break;
    case 0x29: R[n] = R[n] + simm; break;
    case 0x2A: { const uint64_t r = uint64_t(R[n]) + R[mm] + T(); R[n] = static_cast<uint32_t>(r); setT((r >> 32) & 1); break; }
    case 0x2B: { const uint32_t r = R[n] + R[mm]; setT(((~(R[n] ^ R[mm])) & (R[n] ^ r)) >> 31); R[n] = r; break; }
    case 0x2C: setT(simm == R[0]); break;
    case 0x2D: setT(R[n] == R[mm]); break;
    case 0x2E: setT(R[n] >= R[mm]); break;
    case 0x2F: setT(int32_t(R[n]) >= int32_t(R[mm])); break;
    case 0x30: setT(R[n] > R[mm]); break;
    case 0x31: setT(int32_t(R[n]) > int32_t(R[mm])); break;
    case 0x32: setT(int32_t(R[n]) >= 0); break;
    case 0x33: setT(int32_t(R[n]) > 0); break;
    case 0x34:
    {
        const uint32_t t = R[n] ^ R[mm];
        setT(!(t & 0xFF) || !(t & 0xFF00) || !(t & 0xFF0000) || !(t & 0xFF000000u));
        break;
    }
    case 0x35:   // DIV1 reads Rm after Rn has been shifted
    {
        bool newQ = R[n] >> 31;
        R[n] <<= 1;
        R[n] |= T();
        const uint32_t tmp = R[n];
        const bool q = c.v[kSR] & kQ, mflag = c.v[kSR] & kM;
        newQ ^= mflag;
        if (!(q ^ mflag)) { R[n] -= R[mm]; newQ ^= (R[n] > tmp); }
        else { R[n] += R[mm]; newQ ^= (R[n] < tmp); }
        c.v[kSR] = (c.v[kSR] & ~kQ) | (newQ ? kQ : 0u);
        setT(newQ == mflag);
        break;
    }
    case 0x36:
    {
        const bool nq = R[n] >> 31, nm = R[mm] >> 31;
        c.v[kSR] = (c.v[kSR] & ~(kQ | kM)) | (nq ? kQ : 0u) | (nm ? kM : 0u);
        setT(nq != nm);
        break;
    }
    case 0x37: c.v[kSR] &= ~(kQ | kM | kT); break;
    case 0x38: { const uint64_t r = uint64_t(int64_t(int32_t(R[n])) * int32_t(R[mm])); c.v[kMACL] = uint32_t(r); c.v[kMACH] = uint32_t(r >> 32); break; }
    case 0x39: { const uint64_t r = uint64_t(R[n]) * R[mm]; c.v[kMACL] = uint32_t(r); c.v[kMACH] = uint32_t(r >> 32); break; }
    case 0x3A: R[n]--; setT(!R[n]); break;
    case 0x3B: R[n] = Sx8(R[mm]); break;
    case 0x3C: R[n] = Sx16(R[mm]); break;
    case 0x3D: R[n] = R[mm] & 0xFF; break;
    case 0x3E: R[n] = R[mm] & 0xFFFF; break;
    case 0x3F:   // MAC.L @Rm+,@Rn+: Rm is read first, so with m == n the second word is the next one
    {
        const int32_t m0 = int32_t(m.Rd(R[mm], 4));
        R[mm] += 4;
        const int32_t m1 = int32_t(m.Rd(R[n], 4));
        R[n] += 4;
        uint64_t sum = ((uint64_t(c.v[kMACH]) << 32) | c.v[kMACL]) + uint64_t(int64_t(m0) * m1);
        if ((c.v[kSR] & kS) && sum > 0x00007FFFFFFFFFFFull && sum < 0xFFFF800000000000ull)
            sum = int32_t(m0 ^ m1) < 0 ? 0xFFFF800000000000ull : 0x00007FFFFFFFFFFFull;
        c.v[kMACH] = uint32_t(sum >> 32);
        c.v[kMACL] = uint32_t(sum);
        break;
    }
    case 0x40:
    {
        const int16_t m0 = int16_t(m.Rd(R[mm], 2));
        R[mm] += 2;
        const int16_t m1 = int16_t(m.Rd(R[n], 2));
        R[n] += 2;
        if (c.v[kSR] & kS)
        {
            const int32_t b = int32_t(m0) * m1;
            uint64_t sum = uint64_t(int64_t(int32_t(c.v[kMACL])) + b);
            if (sum > 0x000000007FFFFFFFull && sum < 0xFFFFFFFF80000000ull)
            {
                c.v[kMACH] |= 1;
                sum = b < 0 ? 0x80000000ull : 0x7FFFFFFFull;
            }
            c.v[kMACL] = uint32_t(sum);
        }
        else
        {
            const uint64_t sum = ((uint64_t(c.v[kMACH]) << 32) | c.v[kMACL]) + uint64_t(int64_t(m0) * m1);
            c.v[kMACH] = uint32_t(sum >> 32);
            c.v[kMACL] = uint32_t(sum);
        }
        break;
    }
    case 0x41: c.v[kMACL] = R[n] * R[mm]; break;
    case 0x42: c.v[kMACL] = uint32_t(int32_t(int16_t(R[n])) * int32_t(int16_t(R[mm]))); break;
    case 0x43: c.v[kMACL] = uint32_t(uint16_t(R[n])) * uint32_t(uint16_t(R[mm])); break;
    case 0x44: R[n] = 0u - R[mm]; break;
    case 0x45: { const uint64_t r = uint64_t(0) - R[mm] - T(); R[n] = uint32_t(r); setT((r >> 32) & 1); break; }
    case 0x46: R[n] = R[n] - R[mm]; break;
    case 0x47: { const uint64_t r = uint64_t(R[n]) - R[mm] - T(); R[n] = uint32_t(r); setT((r >> 32) & 1); break; }
    case 0x48: { const uint32_t r = R[n] - R[mm]; setT(((R[n] ^ R[mm]) & (R[n] ^ r)) >> 31); R[n] = r; break; }
    case 0x49: R[n] &= R[mm]; break;
    case 0x4A: R[0] &= d8; break;
    case 0x4B: { const uint32_t ea = R[0] + c.v[kGBR]; m.Wr(ea, 1, m.Rd(ea, 1) & d8); break; }
    case 0x4C: R[n] = ~R[mm]; break;
    case 0x4D: R[n] |= R[mm]; break;
    case 0x4E: R[0] |= d8; break;
    case 0x4F: { const uint32_t ea = R[0] + c.v[kGBR]; m.Wr(ea, 1, m.Rd(ea, 1) | d8); break; }
    case 0x50: { const uint32_t tmp = m.Rd(R[n], 1); setT(!tmp); m.Wr(R[n], 1, tmp | 0x80); break; }
    case 0x51: setT(!(R[n] & R[mm])); break;
    case 0x52: setT(!(R[0] & d8)); break;
    case 0x53: setT(!(m.Rd(R[0] + c.v[kGBR], 1) & d8)); break;
    case 0x54: R[n] ^= R[mm]; break;
    case 0x55: R[0] ^= d8; break;
    case 0x56: { const uint32_t ea = R[0] + c.v[kGBR]; m.Wr(ea, 1, m.Rd(ea, 1) ^ d8); break; }
    case 0x57: { const uint32_t b = R[n] >> 31; R[n] = (R[n] << 1) | b; setT(b); break; }
    case 0x58: { const uint32_t b = R[n] & 1; R[n] = (R[n] >> 1) | (b << 31); setT(b); break; }
    case 0x59: { const uint32_t b = R[n] >> 31; R[n] = (R[n] << 1) | T(); setT(b); break; }
    case 0x5A: { const uint32_t b = R[n] & 1; R[n] = (R[n] >> 1) | (T() << 31); setT(b); break; }
    case 0x5B: { const uint32_t b = R[n] & 1; R[n] = uint32_t(int32_t(R[n]) >> 1); setT(b); break; }
    case 0x5C: { const uint32_t b = R[n] >> 31; R[n] <<= 1; setT(b); break; }
    case 0x5D: { const uint32_t b = R[n] & 1; R[n] >>= 1; setT(b); break; }
    case 0x5E: R[n] <<= 2; break;
    case 0x5F: R[n] >>= 2; break;
    case 0x60: R[n] <<= 8; break;
    case 0x61: R[n] >>= 8; break;
    case 0x62: R[n] <<= 16; break;
    case 0x63: R[n] >>= 16; break;
    case 0x64: if (!T()) { f.taken = true; f.target = PC + (Sx8(w) << 1); } break;
    case 0x65: f.delayed = true; if (!T()) { f.taken = true; f.target = PC + (Sx8(w) << 1); } break;
    case 0x66: if (T()) { f.taken = true; f.target = PC + (Sx8(w) << 1); } break;
    case 0x67: f.delayed = true; if (T()) { f.taken = true; f.target = PC + (Sx8(w) << 1); } break;
    case 0x68: f.delayed = f.taken = true; f.target = PC + (Sx12(w) << 1); break;
    case 0x69: f.delayed = f.taken = true; f.target = PC + R[n]; break;
    case 0x6A: c.v[kPR] = PC; f.delayed = f.taken = true; f.target = PC + (Sx12(w) << 1); break;
    case 0x6B: c.v[kPR] = PC; f.delayed = f.taken = true; f.target = PC + R[n]; break;
    case 0x6C: f.delayed = f.taken = true; f.target = R[n]; break;
    case 0x6D: c.v[kPR] = PC; f.delayed = f.taken = true; f.target = R[n]; break;
    case 0x6E: f.delayed = f.taken = true; f.target = c.v[kPR]; break;
    case 0x6F: setT(false); break;
    case 0x70: c.v[kMACH] = c.v[kMACL] = 0; break;
    case 0x71: c.v[kSR + cri] = R[n]; if (cri == 0) c.v[kSR] &= kSrMask; break;
    case 0x72:
    {
        const uint32_t ea = R[n];
        R[n] += 4;
        c.v[kSR + cri] = m.Rd(ea, 4);
        if (cri == 0) c.v[kSR] &= kSrMask;
        break;
    }
    case 0x73: c.v[kMACH + cri] = R[n]; break;
    case 0x74: { const uint32_t ea = R[n]; R[n] += 4; c.v[kMACH + cri] = m.Rd(ea, 4); break; }
    case 0x75: break;
    case 0x76:   // RTE: PC then SR come off the stack, and SR is in force before the delay slot runs
    {
        const uint32_t npc = m.Rd(R[15], 4);
        R[15] += 4;
        const uint32_t nsr = m.Rd(R[15], 4);
        R[15] += 4;
        c.v[kSR] = nsr & kSrMask;
        f.delayed = f.taken = true;
        f.target = npc;
        break;
    }
    case 0x77: setT(true); break;
    case 0x79: R[n] = c.v[kSR + cri]; break;
    case 0x7A: R[n] -= 4; m.Wr(R[n], 4, c.v[kSR + cri]); break;
    case 0x7B: R[n] = c.v[kMACH + cri]; break;
    case 0x7C: R[n] -= 4; m.Wr(R[n], 4, c.v[kMACH + cri]); break;
    case 0x7D:   // TRAPA: SR, then the address of the next instruction, are pushed; PC = vector
    {
        R[15] -= 4;
        m.Wr(R[15], 4, c.v[kSR]);
        R[15] -= 4;
        m.Wr(R[15], 4, PC - 2);
        f.taken = true;
        f.target = m.Rd(c.v[kVBR] + (d8 << 2), 4);
        break;
    }
    default: break;   // illegal, SLEEP: never generated
    }
    return f;
}

// An instruction at 'A' (and its slot) from the register file 'c'. Returns the next address.
//
// 'specSlotPc' selects the one place the spec is known to differ from the hardware (A2-4): after a
// taken branch other than bra and bsr the spec computes a PC-relative slot's address from the
// slot's own address + 4, where the hardware uses the branch target + 2. The decompiler marks a
// function with such a slot unsupported (ScanRawPcode); the oracle accepts a difference only if it
// is exactly this one, and checks that the marking is there.
uint32_t RunRef(Cpu& c, Mem& m, uint32_t A, uint16_t w0, uint16_t w1, bool specSlotPc = false)
{
    const uint32_t PC = A + 4;
    const Flow f = ExecRef(c, m, w0, PC);
    if (!f.delayed) return f.taken ? f.target : A + 2;
    // The slot runs with z->PC = target + 2 after a taken branch (UCDelayBranch / DelayBranch set
    // PC to the target and the post-switch PC += 2 follows), and its own address + 4 otherwise.
    const int op = gOpOf[w0];
    const bool specFixed = op == 0x68 || op == 0x6A;   // bra, bsr: the target is in the instruction (patch 0005)
    ExecRef(c, m, w1, f.taken && !(specSlotPc && !specFixed) ? f.target + 2 : PC + 2);
    return f.taken ? f.target : A + 4;
}

// ---- The emulator side ---------------------------------------------------------------------------------

// RAM for the emulator: aligned 8-byte words over a Mem, with the words written logged.
class LogBank : public ghidra::MemoryBank
{
public:
    explicit LogBank(ghidra::AddrSpace* spc) : ghidra::MemoryBank(spc, 8, 4096) {}
    Mem* mem = nullptr;
    std::vector<uint32_t> inserted;   // aligned word addresses written
    std::vector<uint32_t> InsertedWords() const
    {
        std::vector<uint32_t> w = inserted;
        std::sort(w.begin(), w.end());
        w.erase(std::unique(w.begin(), w.end()), w.end());
        return w;
    }

protected:
    void insert(ghidra::uintb addr, ghidra::uintb val) override
    {
        const uint32_t a = static_cast<uint32_t>(addr);
        inserted.push_back(a);
        for (uint32_t i = 0; i < 8; ++i) mem->Wr8(a + i, static_cast<uint8_t>(val >> (56 - 8 * i)));
    }
    ghidra::uintb find(ghidra::uintb addr) const override { return mem->Word(static_cast<uint32_t>(addr)); }
};


class EmuMachine
{
public:
    explicit EmuMachine(const std::shared_ptr<const MemorySnapshot>& snap) : mSnap(snap)
    {
        mArch.reset(new SaturnArchitecture(mSnap, &mErrors));
        mArch->Initialise(20000, nullptr);
        // The emulator API takes a mutable translator; it only decodes through it.
        ghidra::Translate* t = const_cast<ghidra::Translate*>(mArch->translate);
        mRam = t->getDefaultCodeSpace();
        mBank.reset(new LogBank(mRam));
        mRegs.reset(new ghidra::MemoryHashOverlay(t->getSpaceByName("register"), 8, 4096, 4096, nullptr));
        mTemps.reset(new ghidra::MemoryHashOverlay(t->getUniqueSpace(), 8, 4096, 4096, nullptr));
        mState.reset(new ghidra::MemoryState(t));
        mState->setMemoryBank(mBank.get());
        mState->setMemoryBank(mRegs.get());
        mState->setMemoryBank(mTemps.get());
        mBreaks.reset(new ghidra::BreakTableCallBack(t));
        dts::HookSh2Userops(*mBreaks);
        mEmu.reset(new ghidra::EmulatePcodeCache(t, mState.get(), mBreaks.get()));
        for (int i = 0; i < kRegs; ++i) mVn[i] = t->getRegister(kRegNames[i]);
    }
    ~EmuMachine()
    {
        // The emulator and state point into the translator: gone before the Architecture.
        mEmu.reset();
        mBreaks.reset();
        mState.reset();
    }

    LogBank& Bank() { return *mBank; }

    // What translating the instruction at 'addr' (its delay slot with it) produces: a hash of every
    // p-code op, and their number. Zeroes when the address cannot be translated.
    struct Translation
    {
        uint64_t hash = 0;
        int ops = 0;
        bool operator==(const Translation& o) const { return hash == o.hash && ops == o.ops; }
    };
    Translation Translate(uint32_t addr)
    {
        struct Hasher : public ghidra::PcodeEmit
        {
            Translation t;
            void Mix(uint64_t v) { t.hash = (t.hash ^ v) * 0x100000001B3ull + 0x9E3779B97F4A7C15ull; }
            void Vn(const ghidra::VarnodeData& v)
            {
                Mix(v.space->getIndex());
                Mix(v.offset);
                Mix(static_cast<uint64_t>(v.size));
            }
            void dump(const ghidra::Address&, ghidra::OpCode opc, ghidra::VarnodeData* out, ghidra::VarnodeData* in,
                      ghidra::int4 n) override
            {
                ++t.ops;
                Mix(static_cast<uint64_t>(opc));
                if (out) Vn(*out);
                for (ghidra::int4 i = 0; i < n; ++i) Vn(in[i]);
            }
        } h;
        try
        {
            const_cast<ghidra::Translate*>(mArch->translate)->oneInstruction(h, ghidra::Address(mRam, addr));
        }
        catch (ghidra::LowlevelError&)
        {
            return Translation();
        }
        return h.t;
    }

    // The p-code of the instruction at 'addr' (its delay slot with it), one op per line.
    void DumpPcode(uint32_t addr)
    {
        struct Printer : public ghidra::PcodeEmit
        {
            static std::string Vn(const ghidra::VarnodeData& v)
            {
                char b[96];
                std::snprintf(b, sizeof b, "%s[%llx]:%d", v.space->getName().c_str(),
                              static_cast<unsigned long long>(v.offset), v.size);
                return b;
            }
            void dump(const ghidra::Address&, ghidra::OpCode opc, ghidra::VarnodeData* out, ghidra::VarnodeData* in,
                      ghidra::int4 n) override
            {
                std::string line = "    ";
                if (out) line += Vn(*out) + " = ";
                line += ghidra::get_opname(opc);
                for (ghidra::int4 i = 0; i < n; ++i) line += (i ? ", " : " ") + Vn(in[i]);
                std::printf("%s\n", line.c_str());
            }
        } printer;
        const_cast<ghidra::Translate*>(mArch->translate)->oneInstruction(printer, ghidra::Address(mRam, addr));
    }
    void Load(const Cpu& c)
    {
        for (int i = 0; i < kRegs; ++i) mState->setValue(&mVn[i], c.v[i]);
    }
    void Store(Cpu& c) const
    {
        for (int i = 0; i < kRegs; ++i) c.v[i] = static_cast<uint32_t>(mState->getValue(&mVn[i]));
    }

    // Executes the instruction at 'addr' once, its delay slot with it, and returns where control
    // goes. A target or a fall-through the snapshot does not hold (a branch to a random address)
    // cannot be translated, which is fine: the emulator has by then done everything the
    // instruction does and has already moved its execute address. 'ok' is false, with 'why', when
    // the instruction itself cannot be translated or run.
    uint32_t Step(uint32_t addr, bool& ok, std::string& why)
    {
        ok = true;
        try
        {
            mEmu->setExecuteAddress(ghidra::Address(mRam, addr));
        }
        catch (ghidra::LowlevelError& e)
        {
            ok = false;
            why = "cannot translate: " + e.explain;
            return addr;
        }
        mEmu->setHalt(false);
        try
        {
            mEmu->executeInstruction();
        }
        catch (ghidra::BadDataError&)
        {
        }
        catch (ghidra::LowlevelError& e)
        {
            ok = false;
            why = "emulator: " + e.explain;
        }
        return static_cast<uint32_t>(mEmu->getExecuteAddress().getOffset());
    }

private:
    std::shared_ptr<const MemorySnapshot> mSnap;
    std::ostringstream mErrors;
    std::unique_ptr<SaturnArchitecture> mArch;
    ghidra::AddrSpace* mRam = nullptr;
    std::unique_ptr<LogBank> mBank;
    std::unique_ptr<ghidra::MemoryHashOverlay> mRegs, mTemps;
    std::unique_ptr<ghidra::MemoryState> mState;
    std::unique_ptr<ghidra::BreakTableCallBack> mBreaks;
    std::unique_ptr<ghidra::EmulatePcodeCache> mEmu;
    ghidra::VarnodeData mVn[kRegs];
};

// ---- Forms, code placement, states --------------------------------------------------------------------

// Not legal in a delay slot (or not runnable): branches, RTE, TRAPA, SLEEP, illegal.
bool NoSlot(int op) { return op == 0 || (op >= 0x64 && op <= 0x6E) || op == 0x76 || op == 0x78 || op == 0x7D; }

struct Cell
{
    uint32_t addr;     // of the first word
    uint16_t w[2];     // the instruction and, for a delayed branch, its slot
};

struct Form
{
    std::string name;
    int op = 0;
    bool delayed = false;
    std::vector<Cell> cells;
};

// Where code goes: HWRAM from 0x06004000, nop-filled so that whatever follows a case decodes, and a
// small region at address 0 for branches that wrap below zero.
struct Layout
{
    std::vector<uint8_t> hw, low;
    uint32_t hwNext = 0x4000, lowNext = 0;
    bool overflow = false;
    Layout() : hw(0x100000, 0), low(0x4000, 0)
    {
        for (size_t i = 0; i < hw.size(); i += 2) dts::Put16(hw, i, 0x0009);
        for (size_t i = 0; i < low.size(); i += 2) dts::Put16(low, i, 0x0009);
    }
    // 'a' (0 or 2) is the cell's alignment: the first word sits at the cell's start or two bytes in.
    uint32_t Place(const uint16_t* w, int n, int a, bool atZero)
    {
        std::vector<uint8_t>& img = atZero ? low : hw;
        uint32_t& next = atZero ? lowNext : hwNext;
        const uint32_t bytes = n == 1 ? 4 : 8;
        if (next + bytes > img.size())
        {
            overflow = true;
            return 0;
        }
        const uint32_t off = next;
        next += bytes;
        for (int i = 0; i < n; ++i) dts::Put16(img, off + a + 2 * i, w[i]);
        return (atZero ? 0u : 0x06000000u) + off + static_cast<uint32_t>(a);
    }
};

// The words a form is tried on: all of them when there are few, otherwise the extremes of every
// field (all zero, all one, the sign bit, the bit below it, ...) and a random fill.
std::vector<uint16_t> PoolWords(int op, Rng& rng, size_t maxN)
{
    std::vector<const Pat*> ps;
    for (const Pat& p : kPats)
        if (p.op == op) ps.push_back(&p);
    size_t total = 0;
    auto popCount16 = [](unsigned v) {   // MSVC has no __builtin_popcount (Windows run 1, A12-1)
        int n = 0;
        for (v &= 0xFFFFu; v; v &= v - 1) ++n;
        return n;
    };
    for (const Pat* p : ps) total += size_t(1) << popCount16(~p->mask & 0xFFFF);
    std::vector<uint16_t> out;
    if (total <= maxN)
    {
        for (const Pat* p : ps)
        {
            const uint32_t free = ~p->mask & 0xFFFF;
            uint32_t v = 0;
            do
            {
                out.push_back(static_cast<uint16_t>(p->value | v));
                v = (v - free) & free;
            } while (v);
        }
        return out;
    }
    static const uint16_t edges[] = { 0x0000, 0xFFFF, 0x00FF, 0xFF00, 0x0F0F, 0xF0F0, 0x5555, 0xAAAA, 0x0080, 0x007F,
                                      0x0001, 0x0800, 0x07FF, 0x0008, 0x0007, 0x0F00, 0x00F0, 0x000F, 0x0100, 0x0FFE,
                                      0x0801, 0x0FF0, 0x00FE, 0x0002 };
    const size_t share = maxN / ps.size();
    for (const Pat* p : ps)
    {
        const uint32_t free = ~p->mask & 0xFFFF;
        std::set<uint16_t> seen;
        for (uint16_t e : edges) seen.insert(static_cast<uint16_t>(p->value | (e & free)));
        while (seen.size() < share) seen.insert(static_cast<uint16_t>(p->value | (rng.Next() & free)));
        out.insert(out.end(), seen.begin(), seen.end());
    }
    return out;
}

// A word that may sit in a delay slot: half from a list of instructions that make the slot's
// effect visible (they change registers, flags or memory, read PR or PC), half any non-branch op.
uint16_t RandSlot(Rng& rng)
{
    const unsigned k = rng.Below(16), j = rng.Below(16);
    if (rng.Below(2))
    {
        switch (rng.Below(14))
        {
        case 0: return static_cast<uint16_t>(0x7000 | k << 8 | (1 + rng.Below(255)));   // add #imm,Rk
        case 1: return 0x0018;                                                          // sett
        case 2: return 0x0008;                                                          // clrt
        case 3: return static_cast<uint16_t>(0x0029 | k << 8);                          // movt Rk
        case 4: return static_cast<uint16_t>(0x002A | k << 8);                          // sts pr,Rk
        case 5: return static_cast<uint16_t>(0x402A | k << 8);                          // lds Rk,pr
        case 6: return static_cast<uint16_t>(0x2F06 | j << 4);                          // mov.l Rj,@-r15
        case 7: return static_cast<uint16_t>(0x9000 | k << 8 | rng.Below(256));         // mov.w @(d,pc),Rk
        case 8: return static_cast<uint16_t>(0xD000 | k << 8 | rng.Below(256));         // mov.l @(d,pc),Rk
        case 9: return static_cast<uint16_t>(0xC700 | rng.Below(256));                  // mova @(d,pc),r0
        case 10: return static_cast<uint16_t>(0x3000 | k << 8 | j << 4);                // cmp/eq Rj,Rk
        case 11: return static_cast<uint16_t>(0x400E | k << 8);                         // ldc Rk,sr
        case 12: return static_cast<uint16_t>(0x6F06 | k << 8);                         // mov.l @r15+,Rk
        default: return 0x0009;
        }
    }
    for (;;)
    {
        const Pat& p = kPats[rng.Below(sizeof kPats / sizeof kPats[0])];
        if (NoSlot(p.op)) continue;
        return static_cast<uint16_t>(p.value | (rng.Next() & ~p.mask & 0xFFFF));
    }
}

void BuildForms(Rng& rng, Layout& L, std::vector<Form>& forms)
{
    for (const OpDef& d : kOps)
    {
        if (d.id == 0x78) continue;   // SLEEP is a user op the emulator cannot run
        Form f;
        f.name = std::string(d.mnem) + (d.ops[0] ? " " : "") + d.ops;
        f.op = d.id;
        f.delayed = IsDelayedOp(d.id);
        const bool pcRel = std::strstr(d.ops, "PC") != nullptr;
        const std::vector<uint16_t> words = PoolWords(d.id, rng, 512);
        // A delayed branch gets 512 cells even when it has few encodings (rts has one), so that it
        // meets many different delay slots.
        const size_t nCells = f.delayed ? 512 : words.size();
        for (size_t i = 0; i < nCells; ++i)
            for (int copy = 0; copy < (pcRel ? 2 : 1); ++copy)
            {
                Cell c{};
                c.w[0] = words[i % words.size()];
                c.w[1] = f.delayed ? RandSlot(rng) : 0;
                c.addr = L.Place(c.w, f.delayed ? 2 : 1, pcRel ? copy * 2 : static_cast<int>(i & 1) * 2, false);
                f.cells.push_back(c);
            }
        forms.push_back(std::move(f));
        // Relative branches at the bottom of the address space, where a backward target wraps.
        if (d.id == 0x64 || d.id == 0x65 || d.id == 0x66 || d.id == 0x67 || d.id == 0x68 || d.id == 0x6A)
        {
            Form g;
            g.name = forms.back().name + " (code at address 0)";
            g.op = d.id;
            g.delayed = forms.back().delayed;
            const std::vector<uint16_t> lowWords = PoolWords(d.id, rng, 64);
            for (size_t i = 0; i < (g.delayed ? 128 : lowWords.size()); ++i)
            {
                Cell c{};
                c.w[0] = lowWords[i % lowWords.size()];
                c.w[1] = g.delayed ? RandSlot(rng) : 0;
                c.addr = L.Place(c.w, g.delayed ? 2 : 1, static_cast<int>(g.cells.size() & 1) * 2, true);
                g.cells.push_back(c);
            }
            forms.push_back(std::move(g));
        }
    }
}

// An aligned address for a memory operand: mostly RAM, often one that wraps (low, so that a
// pre-decrement goes below zero; high, so that an increment or a displacement goes past 2^32).
uint32_t PickAddr(Rng& rng, uint32_t align)
{
    uint32_t a;
    switch (rng.Below(20))
    {
    case 0: case 1: case 2: case 3: case 4: case 5: case 6: case 7: case 8: case 9: case 10:
        a = 0x06100000u + (rng.Next() & 0x7FFF8u);
        break;
    case 11: case 12: a = rng.Next() & 0x3F; break;
    case 13: case 14: a = 0xFFFFFFC0u + (rng.Next() & 0x3F); break;
    case 15: { static const uint32_t e[] = { 0x7FFFFFFCu, 0x80000000u, 0xFFFF0000u, 0x00010000u, 0x20000000u };
               a = e[rng.Below(5)]; break; }
    default: a = rng.Next(); break;
    }
    return a & ~(align - 1);
}

// An even address for a register-target branch: near the code, at the bottom or top of the address
// space, or anywhere.
uint32_t PickTarget(Rng& rng, uint32_t from)
{
    switch (rng.Below(10))
    {
    case 0: case 1: case 2: return (from + 2 * rng.Below(0x400)) & ~1u;
    case 3: case 4: return (from - 2 * rng.Below(0x400)) & ~1u;
    case 5: return 2 * rng.Below(64);
    case 6: return 0xFFFFFFFEu - 2 * rng.Below(64);
    case 7: return 0x7FFFFFFEu + 2 * rng.Below(4);
    default: return rng.Operand() & ~1u;
    }
}

// Point the registers an instruction addresses memory through at aligned addresses.
void PrepMem(Cpu& c, uint16_t w, Rng& rng)
{
    const int op = gOpOf[w];
    if (!op) return;
    const OpDef& d = *gOpDef[op];
    const unsigned n = (w >> 8) & 15, m = (w >> 4) & 15;
    const uint32_t al = d.mem ? static_cast<uint32_t>(d.mem) : 1;
    if (op == 0x76) { c.v[15] = PickAddr(rng, 4); return; }
    if (op == 0x7D) { c.v[15] = PickAddr(rng, 4); c.v[kVBR] = PickAddr(rng, 4); return; }
    for (const char* p = d.ops; *p; ++p)
    {
        if (*p != '@') continue;
        const char* q = p + 1;
        if (*q == '(')
        {
            ++q;
            const char* comma = std::strchr(q, ',');
            const char* close = std::strchr(q, ')');
            const std::string a(q, comma), b(comma + 1, close);
            if (a == "R0")
            {
                // @(R0,Rx) and @(R0,GBR): R0 an aligned offset, the other register the rest.
                const uint32_t ea = PickAddr(rng, al);
                const uint32_t off = (rng.Next() & 0xFFFF8u) & ~(al - 1);
                c.v[0] = off;
                if (b == "GBR") c.v[kGBR] = ea - off;
                else if (b == "Rn" && n != 0) c.v[n] = ea - off;
                else if (b == "Rm" && m != 0) c.v[m] = ea - off;
            }
            else if (b == "GBR") c.v[kGBR] = PickAddr(rng, al);
            else if (b == "Rn") c.v[n] = PickAddr(rng, al);
            else if (b == "Rm") c.v[m] = PickAddr(rng, al);
        }
        else
        {
            if (*q == '-') ++q;
            c.v[q[1] == 'n' ? n : m] = PickAddr(rng, al);
        }
    }
}

struct Case
{
    Cpu cpu;
    Mem mem;
};

Case GenCase(const Form& f, const Cell& cell, Rng& rng)
{
    Case k;
    for (int i = 0; i < 16; ++i) k.cpu.v[i] = rng.Operand();
    k.cpu.v[kSR] = rng.Next() & kSrMask;
    for (int i = kGBR; i < kRegs; ++i)
        if (i != kSR) k.cpu.v[i] = rng.Operand();
    const int op = gOpOf[cell.w[0]];
    const unsigned n = (cell.w[0] >> 8) & 15;
    const uint32_t from = cell.addr + 4;
    switch (op)
    {
    case 0x69: case 0x6B: k.cpu.v[n] = PickTarget(rng, from) - from; break;   // braf, bsrf: target = PC + Rn
    case 0x6C: case 0x6D: k.cpu.v[n] = PickTarget(rng, from); break;
    case 0x6E: k.cpu.v[kPR] = PickTarget(rng, from); break;
    default: break;
    }
    PrepMem(k.cpu, cell.w[0], rng);
    if (f.delayed) PrepMem(k.cpu, cell.w[1], rng);
    k.mem.seed = (uint64_t(rng.Next()) << 32) | rng.Next();
    if (op == 0x76)   // the frame RTE pops: an even PC and a whole SR word
    {
        k.mem.Pre32(k.cpu.v[15], PickTarget(rng, from));
        // The SR word: half the time only bits that exist, so that the mask is not what differs.
        k.mem.Pre32(k.cpu.v[15] + 4, rng.Below(2) ? (rng.Next() & kSrMask) : rng.Next());
    }
    if (op == 0x7D) k.mem.Pre32(k.cpu.v[kVBR] + ((cell.w[0] & 0xFF) << 2), PickTarget(rng, from));
    return k;
}

// ---- Comparison and reporting --------------------------------------------------------------------------

std::string Bytes8(uint64_t v)
{
    char b[24];
    std::snprintf(b, sizeof b, "%016llX", static_cast<unsigned long long>(v));
    return b;
}

struct Outcome
{
    bool ran = true;      // false: the emulator could not run the instruction ('why')
    std::string why;
    std::string sig;      // which parts of the result differ ("" if none)
    std::string detail;   // and how
    bool known = false;   // differs from the hardware only as A2-4 does (see RunRef); sig is then empty
};

// One case on both sides, compared: r0-r15, SR, GBR, VBR, MACH, MACL, PR, the next PC and every
// word of memory either side wrote.
Outcome RunCase(EmuMachine& mach, const Cell& cell, const Case& k)
{
    Outcome o;
    Mem emuMem = k.mem;
    mach.Bank().mem = &emuMem;
    mach.Bank().inserted.clear();
    mach.Load(k.cpu);
    const uint32_t emuNext = mach.Step(cell.addr, o.ran, o.why);
    if (!o.ran) return o;
    Cpu emu;
    mach.Store(emu);
    const std::vector<uint32_t> ew = mach.Bank().InsertedWords();

    // The emulator's result against the reference; the reference runs on a copy of the case.
    auto compare = [&](bool specSlotPc, std::string& sig, std::string& detail) {
        Cpu ref = k.cpu;
        Mem refMem = k.mem;
        const uint32_t refNext = RunRef(ref, refMem, cell.addr, cell.w[0], cell.w[1], specSlotPc);
        char b[160];
        bool gpr = false;
        for (int r = 0; r < kRegs; ++r)
            if (ref.v[r] != emu.v[r])
            {
                if (r < 16) gpr = true;
                else sig += std::string(sig.empty() ? "" : ",") + kRegNames[r];
                std::snprintf(b, sizeof b, "%s want %08X got %08X; ", kRegNames[r], ref.v[r], emu.v[r]);
                detail += b;
            }
        if (gpr) sig = std::string("r0-r15") + (sig.empty() ? "" : ",") + sig;
        if (refNext != emuNext)
        {
            sig += std::string(sig.empty() ? "" : ",") + "next";
            std::snprintf(b, sizeof b, "next want %08X got %08X; ", refNext, emuNext);
            detail += b;
        }
        const std::vector<uint32_t> rw = refMem.WrittenWords();
        bool memDiff = rw != ew;
        if (!memDiff)
            for (uint32_t a : rw)
                if (refMem.Word(a) != emuMem.Word(a)) memDiff = true;
        if (memDiff)
        {
            sig += std::string(sig.empty() ? "" : ",") + "mem";
            detail += "memory written: want";
            for (uint32_t a : rw) detail += " " + Hex8(a) + "=" + Bytes8(refMem.Word(a));
            detail += "; got";
            for (uint32_t a : ew) detail += " " + Hex8(a) + "=" + Bytes8(emuMem.Word(a));
            detail += "; ";
        }
    };
    compare(false, o.sig, o.detail);
    if (!o.sig.empty())
    {
        // A delayed branch that differs: is it exactly the spec's known slot PC (A2-4)? Only then is it
        // not a finding here; anything else in the same case, an SR bit say, still fails.
        std::string sig, detail;
        compare(true, sig, detail);
        if (sig.empty())
        {
            o.known = true;
            o.sig.clear();
            o.detail.clear();
        }
    }
    return o;
}

void DescribeInit(std::string& out, const Cpu& c)
{
    char b[48];
    for (int i = 0; i < kRegs; ++i)
    {
        std::snprintf(b, sizeof b, "%s=%08X ", kRegNames[i], c.v[i]);
        out += b;
    }
}

// ---- Named shapes: the findings of the random run, as small fixed cases --------------------------------
//
// Each is one case of the same comparison with its registers and stack written out, so that a fix
// can be checked against it by hand and a finding reproduced without the generator. Controls are
// shapes next to a finding that must (and do) agree.

struct Shape
{
    std::string name;
    Cell cell{};
    bool delayed = false;
    Case k{};
};

Shape MakeShape(Layout& L, const char* name, uint16_t w0, uint16_t w1, bool delayed,
                std::initializer_list<std::pair<int, uint32_t>> regs,
                std::initializer_list<std::pair<uint32_t, uint32_t>> words = {})
{
    Shape s;
    s.name = name;
    s.delayed = delayed;
    s.cell.w[0] = w0;
    s.cell.w[1] = w1;
    s.cell.addr = L.Place(s.cell.w, delayed ? 2 : 1, 0, false);
    std::memset(&s.k.cpu, 0, sizeof s.k.cpu);
    for (const auto& r : regs) s.k.cpu.v[r.first] = r.second;
    s.k.mem.seed = 0x5EEDull;
    for (const auto& w : words) s.k.mem.Pre32(w.first, w.second);
    return s;
}

void BuildShapes(Layout& L, std::vector<Shape>& out)
{
    const uint32_t kStack = 0x06100000u, kTarget = 0x06008000u, kPool = 0x06100100u;
    // SR keeps only the bits that exist (T S I3-I0 Q M); ldc and ldc.l mask what they load.
    out.push_back(MakeShape(L, "ldc r0,sr; r0 = FFFFFFFF", 0x400E, 0, false, { { 0, 0xFFFFFFFFu } }));
    out.push_back(MakeShape(L, "ldc.l @r1+,sr; word = FFFFFFFF", 0x4107, 0, false, { { 1, kPool } }, { { kPool, 0xFFFFFFFFu } }));
    out.push_back(MakeShape(L, "ldc r0,gbr; r0 = FFFFFFFF (control: GBR is not masked)", 0x401E, 0, false, { { 0, 0xFFFFFFFFu } }));
    // RTE: PC then SR come off the stack, SR before the slot runs, masked to the bits that exist.
    out.push_back(MakeShape(L, "rte; nop; stacked SR = FFFFFFFF", 0x002B, 0x0009, true, { { 15, kStack } },
                            { { kStack, kTarget }, { kStack + 4, 0xFFFFFFFFu } }));
    out.push_back(MakeShape(L, "rte; sett; stacked SR = 0 (T must be 1 after the slot)", 0x002B, 0x0018, true,
                            { { 15, kStack } }, { { kStack, kTarget }, { kStack + 4, 0 } }));
    out.push_back(MakeShape(L, "rte; clrt; stacked SR = 1 (T must be 0 after the slot)", 0x002B, 0x0008, true,
                            { { 15, kStack } }, { { kStack, kTarget }, { kStack + 4, 1 } }));
    out.push_back(MakeShape(L, "rte; movt r2; stacked SR = 1 (the slot reads the restored T)", 0x002B, 0x0229, true,
                            { { 15, kStack } }, { { kStack, kTarget }, { kStack + 4, 1 } }));
    // A PC-relative load in a delay slot reads relative to the branch target + 2 when the branch is
    // taken. bra and bsr are fixed (patch 0005); the other delayed branches are not.
    const uint16_t slotL = 0xD200, slotW = 0x9200, slotA = 0xC700;   // mov.l/mov.w @(0,pc),r2; mova @(0,pc),r0
    out.push_back(MakeShape(L, "bra +; mov.l @(0,pc),r2 (control)", 0xA010, slotL, true, {}));
    out.push_back(MakeShape(L, "bsr +; mov.w @(0,pc),r2 (control)", 0xB010, slotW, true, {}));
    out.push_back(MakeShape(L, "bt/s taken; mov.l @(0,pc),r2", 0x8D10, slotL, true, { { kSR, 1 } }));
    out.push_back(MakeShape(L, "bf/s taken; mov.w @(0,pc),r2", 0x8F10, slotW, true, { { kSR, 0 } }));
    out.push_back(MakeShape(L, "bt/s not taken; mov.l @(0,pc),r2 (control)", 0x8D10, slotL, true, { { kSR, 0 } }));
    out.push_back(MakeShape(L, "jmp @r1; mov.l @(0,pc),r2", 0x412B, slotL, true, { { 1, kTarget } }));
    out.push_back(MakeShape(L, "jsr @r1; mova @(0,pc),r0", 0x410B, slotA, true, { { 1, kTarget } }));
    out.push_back(MakeShape(L, "rts; mov.l @(0,pc),r2", 0x000B, slotL, true, { { kPR, kTarget } }));
    out.push_back(MakeShape(L, "braf r1; mov.l @(0,pc),r2", 0x0123, slotL, true, { { 1, 0x7000 } }));
    out.push_back(MakeShape(L, "bsrf r1; mov.w @(0,pc),r2", 0x0103, slotW, true, { { 1, 0x7000 } }));
    out.push_back(MakeShape(L, "rte; mov.l @(0,pc),r2", 0x002B, slotL, true, { { 15, kStack } },
                            { { kStack, kTarget }, { kStack + 4, 0 } }));
}

// ---- Translation must not depend on what was translated before -------------------------------------------
//
// The translator caches parse trees in a small ring (DisassemblyCache: 2 entries for a language whose
// delay slots are one instruction, as SH-2's are). Translating an instruction again can hit its
// cached tree while the lookup for its delay slot misses and takes the ring's next entry; when the
// ring evicted in order of allocation that was the instruction's own tree, and the branch was built
// from its slot's parse (A2-5, fixed by patch 0020: a hit makes the entry the newest). The test: for
// every delayed cell, translate [slot, branch, one unrelated instruction, branch] and compare the two
// translations of the branch with each other and with a cold one.
long TestTranslationHistory(EmuMachine& mach, const std::vector<Form>& forms, const char* filter, int show)
{
    long cells = 0, bad = 0;
    for (const Form& f : forms)
    {
        if (!f.delayed || (filter && f.name.find(filter) == std::string::npos)) continue;
        long formBad = 0;
        std::string example;
        for (const Cell& c : f.cells)
        {
            ++cells;
            const uint32_t other = c.addr + 4;   // any other instruction, not in the same hash slot
            mach.Translate(c.addr + 0x100);      // move the ring on, so the next lookup is cold
            mach.Translate(c.addr + 0x102);
            const EmuMachine::Translation cold = mach.Translate(c.addr);
            mach.Translate(c.addr + 0x100);
            mach.Translate(c.addr + 0x102);
            mach.Translate(c.addr + 2);          // the slot, as an instruction of its own (a branch target)
            mach.Translate(c.addr);
            mach.Translate(other);
            const EmuMachine::Translation again = mach.Translate(c.addr);
            if (cold == again) continue;
            ++formBad;
            if (example.empty())
            {
                char b[160];
                std::snprintf(b, sizeof b, "  %s %s at %s: %d ops cold, %d ops after [%s, %s, %s, %s]", Hex8(c.w[0]).substr(6).c_str(),
                              Hex8(c.w[1]).substr(6).c_str(), Hex8(c.addr).c_str(), cold.ops, again.ops,
                              Hex8(c.addr + 2).c_str(), Hex8(c.addr).c_str(), Hex8(other).c_str(), Hex8(c.addr).c_str());
                example = b;
            }
        }
        bad += formBad;
        if (formBad) std::printf("TRANSLATION HISTORY %-34s %ld of %zu cells translate differently\n%s\n", f.name.c_str(),
                                 formBad, f.cells.size(), example.c_str());
    }
    (void)show;
    std::printf("translation history: %ld delayed-branch cells, %ld translate differently when their slot was translated first\n",
                cells, bad);
    return bad;
}

// A2-4 is accepted because the host flags it: a function that reaches a PC-relative load or mova in the
// delay slot of a branch other than bra and bsr is marked unsupported, and its literal is not folded.
// Pinned for the shapes the differential run found: decompiled from the branch, the result says so.
void TestKnownLimitIsFlagged(const std::shared_ptr<MemorySnapshot>& snap,
                             const std::vector<std::pair<std::string, uint32_t>>& shapes)
{
    Sh2Decompiler d(gConfig);
    int flagged = 0;
    for (const auto& s : shapes)
    {
        DecompileRequest q;
        q.entry = s.second;
        const DecompileResult r = d.Decompile(snap, q);
        std::printf("A2-4 shape %-44s decompiled from %08X: %s\n", s.first.c_str(), s.second,
                    r.unsupported ? "marked unsupported" : "NOT MARKED");
        CHECK(r.status == DecompileStatus::Ok);
        CHECK(r.unsupported);
        flagged += r.unsupported ? 1 : 0;
    }
    std::printf("known limit A2-4: %d of %zu shapes marked unsupported by the decompiler\n", flagged, shapes.size());
    CHECK(!shapes.empty());
}

// A2-5 as the decompiler reaches it. The signature check (ReturnCheck) walks the function's
// instructions again, after the flow has been through them, in its own order, and so can ask for a
// delayed branch whose cached tree is the oldest while its slot's is gone. The function: a delayed
// branch at its entry, whose slot is also a jump target, and a loop back to the branch.
//
//   A  0x00  bra Z          entry; the slot is the instruction at 0x02
//   S  0x02  add #1,r2      the slot, also reached by 'bt S'
//   P  0x04  jmp @r1        a return (r1 is PR here on every path that reaches it)
//      0x06  nop
//   Z  0x08  bf A           loop back to the branch
//      0x0A  sts pr,r1
//      0x0C  bt S           (the slot as an instruction of its own: the flow ends on it)
//      0x0E  bt P
//      0x10  rts
//      0x12  nop
//
// The flow follows 0x00, 0x08, 0x0A..0x10, 0x04, then 0x02 last. ReturnCheck then steps 0x00 (the slot's
// tree is the newest), 0x08 (a plain instruction: the oldest, the slot's, goes), and 0x00 again: the
// branch is a hit, its slot a miss, and the miss took the branch's own tree, so the branch was built
// from its slot's parse and flowed to 0x04 with r1 not holding PR, which "does not resolve" and withheld
// a signature the function is entitled to. Without patch 0020 this fixture withholds it.
void TestRecycledTreeInDecompiler()
{
    const uint32_t base = 0x06009000u;
    const uint16_t code[] = { 0xA002, 0x7201, 0x412B, 0x0009, 0x8BFA, 0x012A, 0x89F9, 0x89F9, 0x000B, 0x0009 };
    std::vector<uint8_t> hw;
    for (size_t i = 0; i < sizeof code / sizeof code[0]; ++i) dts::Put16(hw, base - 0x06000000u + 2 * i, code[i]);
    auto snap = dts::MakeSnapshot(hw);
    Sh2Decompiler d(gConfig);
    DecompileRequest q;
    q.entry = base;
    const DecompileResult r = d.Decompile(snap, q);
    std::printf("A2-5 in the decompiler: status %d, signature withheld: [%s]\n", int(r.status), r.prototypeWithheld.c_str());
    CHECK(r.status == DecompileStatus::Ok);
    CHECK(r.prototypeWithheld.empty());
}

void TestExecution(long casesPerForm, uint64_t seed, const char* filter, int show)
{
    const auto t0 = std::chrono::steady_clock::now();
    Rng rng(seed);
    Layout layout;
    std::vector<Form> forms;
    BuildForms(rng, layout, forms);
    std::vector<Shape> shapes;
    BuildShapes(layout, shapes);
    CHECK(!layout.overflow);
    auto snap = dts::MakeSnapshot(layout.hw);
    MemoryRegion low;
    low.name = "BIOS";
    low.base = 0;
    low.bytes = layout.low;
    snap->regions.push_back(std::move(low));
    snap->key.regionSetHash = snap->ComputeRegionSetHash();
    std::unique_ptr<EmuMachine> machPtr(new EmuMachine(snap));
    EmuMachine& mach = *machPtr;
    std::printf("%zu forms, %ld cases each, seed %llu; code placed in %u bytes of HWRAM and %u at address 0 (%.0f ms)\n",
                forms.size(), casesPerForm, static_cast<unsigned long long>(seed), layout.hwNext - 0x4000,
                layout.lowNext, dts::MsSince(t0));

    const long historyBad = TestTranslationHistory(mach, forms, filter, show);
    long shapeFails = 0;
    std::vector<std::pair<std::string, uint32_t>> knownShapes;   // A2-4 shapes: the decompiler must flag them
    const char* pcode = std::getenv("SE_DECODE_PCODE");   // print the p-code of the shapes selected by SE_DECODE_FORM
    for (const Shape& s : shapes)
    {
        if (filter && s.name.find(filter) == std::string::npos) continue;
        if (pcode)
        {
            std::printf("p-code of %s (%08X):\n", s.name.c_str(), s.cell.addr);
            mach.DumpPcode(s.cell.addr);
        }
        const Outcome o = RunCase(mach, s.cell, s.k);
        if (!o.ran) std::printf("SHAPE %s: could not run (%s)\n", s.name.c_str(), o.why.c_str());
        else if (o.known)
        {
            std::printf("SHAPE %s: known limit (A2-4: the slot's PC)\n", s.name.c_str());
            knownShapes.emplace_back(s.name, s.cell.addr);
        }
        else if (o.sig.empty()) std::printf("SHAPE %s: agrees\n", s.name.c_str());
        else std::printf("SHAPE %s: DIFFERS %s\n         %s\n", s.name.c_str(), o.sig.c_str(), o.detail.c_str());
        if (!o.ran || !o.sig.empty()) ++shapeFails;
    }

    long totalCases = 0, totalFails = 0, failingForms = 0, unrunnable = 0, pcodeDumps = 0, totalKnown = 0;
    for (const Form& f : forms)
    {
        if (filter && f.name.find(filter) == std::string::npos) continue;
        long cases = 0, fails = 0, notRun = 0, known = 0;
        std::map<std::string, long> signatures;   // which parts of the result differed -> how often
        std::vector<std::string> examples;
        Rng frng(seed ^ (uint64_t(f.op) << 40) ^ std::hash<std::string>()(f.name));
        for (long i = 0; i < casesPerForm; ++i)
        {
            const Cell& cell = f.cells[frng.Below(static_cast<uint32_t>(f.cells.size()))];
            const Case k = GenCase(f, cell, frng);
            Outcome o = RunCase(mach, cell, k);
            ++cases;
            if (!o.ran)
            {
                ++notRun;
                if (examples.size() < size_t(show))
                    examples.push_back("  " + o.why + " (word " + Hex8(cell.w[0]).substr(6) + " at " + Hex8(cell.addr) + ")");
                continue;
            }
            if (o.known) ++known;
            if (o.sig.empty()) continue;
            if (pcode && o.sig.find("next") != std::string::npos && ++pcodeDumps <= 3)
            {
                std::printf("p-code of %s at %08X:\n", Hex8(cell.w[0]).substr(6).c_str(), cell.addr);
                mach.DumpPcode(cell.addr);
            }
            if (f.delayed)   // what the slot was: the PC-relative and SR-writing slots have known causes
            {
                const int so = gOpOf[cell.w[1]];
                o.sig += so == 0x02 || so == 0x03 || so == 0x23 ? " | slot PC-relative"
                         : so == 0x71 || so == 0x72                ? " | slot ldc to SR"
                                                                   : " | slot other";
            }
            ++fails;
            ++signatures[o.sig];
            if (examples.size() < size_t(show))
            {
                std::string ex = "  " + Hex8(cell.w[0]).substr(6);
                if (f.delayed) ex += " " + Hex8(cell.w[1]).substr(6);
                ex += " at " + Hex8(cell.addr) + ": " + o.detail + "\n      init: ";
                DescribeInit(ex, k.cpu);
                examples.push_back(ex);
            }
        }
        totalCases += cases;
        totalFails += fails;
        totalKnown += known;
        unrunnable += notRun;
        if (known) std::printf("form %-34s %ld of %ld cases differ only as A2-4 does (a PC-relative slot after a taken branch)\n",
                               f.name.c_str(), known, cases);
        if (fails || notRun)
        {
            ++failingForms;
            std::printf("FORM %-34s %ld of %ld cases differ", f.name.c_str(), fails, cases);
            if (notRun) std::printf(", %ld could not run", notRun);
            std::printf("\n");
            for (const auto& kv : signatures) std::printf("    differing: %-36s x%ld\n", kv.first.c_str(), kv.second);
            for (const std::string& e : examples) std::printf("%s\n", e.c_str());
        }
    }
    std::printf("differential execution: %ld cases, %ld differ, %ld more differ only as the known limit A2-4, %ld could not run, "
                "%ld of %zu forms fail; %zu named shapes, %ld differ (%.1f s)\n",
                totalCases, totalFails, totalKnown, unrunnable, failingForms, forms.size(), shapes.size(), shapeFails,
                dts::MsSince(t0) / 1000.0);
    CHECK(totalFails == 0);
    CHECK(unrunnable == 0);
    CHECK(shapeFails == 0);
    CHECK(historyBad == 0);

    // The known limit stays safe only while the decompiler says so: each shape, decompiled from its
    // branch, must come back unsupported (ScanRawPcode: a PC-relative slot after a delayed branch the
    // spec cannot place). The machine goes first -- the decompiler resets the process-wide translator.
    machPtr.reset();
    if (!filter) TestKnownLimitIsFlagged(snap, knownShapes);
}

}  // namespace

int main(int argc, char** argv)
{
    if (argc != 2)
    {
        std::printf("usage: %s <scratch dir>\n", argv[0]);
        return 2;
    }
    dts::PrepareProcess();
    const auto start = std::chrono::steady_clock::now();
    const std::string scratch = argv[1];
    dts::RemoveTree(scratch);
    CHECK(sfe::MakeDirectory(scratch));
    gConfig = dts::Join(scratch, "config");
    CHECK(sfe::MakeDirectory(gConfig));
    std::string error;
    if (!SaturnArchitecture::StartLibrary(gConfig, error))
    {
        std::printf("FATAL: %s\n", error.c_str());
        return 3;
    }
    BuildTables();
    if (const char* ss = std::getenv("SE_MEDNAFEN_SS"))
    {
        const int bad = VerifyAgainstMednafen(ss);
        std::printf("transcribed word-to-op table vs %s/sh7095_idecodetab.inc: %s\n", ss,
                    bad < 0 ? "could not read" : (bad == 0 ? "all 65536 words equal" : "DIFFERENT"));
        CHECK(bad == 0);
    }
    try
    {
        const auto t0 = std::chrono::steady_clock::now();
        RUN(TestDecode());
        std::printf("decode: %.1f ms\n", dts::MsSince(t0));
        const char* env = std::getenv("SE_DECODE_CASES");
        const long cases = env ? std::strtol(env, nullptr, 10) : 2000;
        env = std::getenv("SE_DECODE_SEED");
        const uint64_t seed = env ? std::strtoull(env, nullptr, 10) : 20260101ull;
        env = std::getenv("SE_DECODE_SHOW");
        const int show = env ? static_cast<int>(std::strtol(env, nullptr, 10)) : 2;
        RUN(TestRecycledTreeInDecompiler());
        RUN(TestExecution(cases, seed, std::getenv("SE_DECODE_FORM"), show));

    }
    catch (ghidra::LowlevelError& e)
    {
        std::printf("FATAL: engine error: %s\n", e.explain.c_str());
        return 3;
    }
    catch (std::exception& e)
    {
        std::printf("FATAL: uncaught std::exception: %s\n", e.what());
        return 3;
    }
    dts::RemoveTree(scratch);
    return dts::Finish("Sh2DecodeTests", start);
}
