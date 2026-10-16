// The decompiler against the emulator: random SH-2 functions from a small grammar, run on the
// vendored p-code emulator for the memory accesses the code really makes (the ground truth),
// then decompiled; every access the emulator saw must be in the decompiler's access ledger
// (DecompileResult::accesses), unless it is one the decompiler is allowed to fold or has said it
// removed. This is the review program's instrument for finding failure classes without a human
// reviewer (Docs/Decompiler/REVIEW_PROGRAM.md, Phase 0): a shape the hand-written fixtures never
// thought of shows up as a seed that fails.
//
// The grammar (generation is a pure function of the seed). Version 1 is one function:
//   * ALU: mov #imm, mov, add, sub, and, or, xor, shll2, extu.b, extu.w -- into r0-r3 only.
//   * loads and stores of every width through r4/r5 (pointer parameters into scratch RAM) and
//     r6/r7 (pointers loaded from the literal pool: a hardware register, an ordinary RAM address
//     or a mailbox the request marks volatile), as @Rn, @(disp,Rn), @(r0,Rn), @Rm+ / @-Rn (r4/r5
//     only) and tas.b; and mac.l / mac.w @Rm+,@Rn+, two reads in one instruction (one program in eight,
//     from a stream of its own so the other programs of a seed are the same as before it was in the
//     grammar). r15 is never touched.
//   * a forward conditional branch (tst/cmp, then bt/bf or bt/s/bf/s with a delay-slot
//     instruction) over one to three instructions, which may reload a pointer register.
//   * a counted loop: mov #n,r3 ; body ; dt r3 ; bf.
//   * a mailbox write-then-poll: store a value, then read until another processor changes it.
//   * rts with a nop or an instruction in its delay slot. A delay slot never holds a
//     PC-relative load or a branch (the decompiler declares that unsupported, by policy).
// Version 2 is a module of one to three functions laid out one after the other, each built from the
// same items plus (Areas 3 and 4, REVIEW_PROGRAM.md):
//   * a stack: r15 is a stack top in scratch RAM; a function pushes r8-r14 and PR (sts.l pr,@-r15, or
//     sts pr,Rn into a pushed register), takes a frame (add #-n,r15; optionally r14 as frame pointer),
//     addresses locals as @(disp,r15) / @(disp,r14) / @(r0,r15) / @r15, spills r1-r5 around a block
//     (mov.l Rn,@-r15 ... mov.l @r15+,Rn), and restores everything by rts (the last pop, or the
//     return value's mov, may sit in the delay slot). The harness checks it does.
//   * calls to a function laid out after the caller: bsr, or jsr @Rn after a literal-pool load of the
//     address; arguments in r4-r7 (the last may be set in the delay slot), kept across calls in
//     callee-saved copies; a result in r0 (a function either writes r0 as its result on every path,
//     or leaves r0 alone); a call may be conditional; tail calls (jmp @Rn after the epilogue has
//     restored PR and the registers) and falling off the end into the next function (no rts).
//   * control flow: bra forward (to a loop test at the bottom) and backward (loop with an exit in the
//     middle), data words skipped by a bra (rts and invalid words among them), a jump table through
//     mova + shll2 + mov.l @(r0,Rn) + jmp @Rn with 3 to 6 entries (some shared, some going to the
//     default) and a cmp/hi or cmp/hs bound check or, for a quarter of the tables, a signed one
//     (cmp/gt, cmp/ge: the larger indices go to the default and index -1 reads the word before
//     the table, which the generator fills with an entry that goes to a case), a shared exit reached
//     by two bra, and an exit placed before the entry.
//   * conventions the compiler does not use (a hand-written routine's; one module in three, ledger A3-3 and
//     A3-5), drawn from a stream of their own so no other program of a seed changes: a function that leaves
//     a register changed for good -- r12, r13 or r14 (one per module, no function of it uses that one for a
//     frame pointer, a saved PR or a push), GBR, VBR, MACH or MACL, from r4-r7 or a constant; a function that
//     reads an input no signature passes -- a scratch register the caller left (r1-r3), or the entry value of
//     r8-r14 it does not save, GBR, VBR, MACH or MACL, copied to a scratch register the later items use; and
//     a caller that, right after a call to such a function, copies the register it changed into a scratch
//     register that later items may store or return. (r15 is not moved: a function that leaves it changed
//     takes its callers' frames and saved PR with it, and the harness cannot say what they then mean.)
//
// The domain (ledger A2-8 (a), A11-2): a misaligned word or longword access is an address error on the SH-2, which the
// spec has no model of and the emulator does not raise (it makes the access). The generator keeps the pointer parameters
// r4/r5 aligned for the width they are used at -- including after a post-increment or pre-decrement of a smaller width,
// in a loop body, and in a returned value's load -- so the programs it makes execute far fewer of them (3,000 grammar-2
// seeds from 200001: 232 seeds with one, 934 runs, against 329 and 1,496 untracked). The rest -- a pointer handed to a
// callee by a caller that advanced it, a mac inserted after the items around it were drawn, GBR and literal-pointer
// displacements -- is not prevented: the harness classifies a run whose trace holds a misaligned access (also a perturbed
// run, and SE_GENERATED_WORDS) as excluded. It is left out of the trace-based checks and counted
// ('excluded.address-error.*', with the seeds listed in the summary), not run as a normal completion. Nothing else is
// suppressed: the decompilation of such a program is still checked (its status, the flow, the prototype), and a Failed
// status or an engine error string is a finding as for any other program. TestDomainPins holds both parts in every run.
//
// The ALU alphabet of both grammars is wider than the first eleven ops (area 2, round 3): one ALU item in four
// is drawn, from a stream of its own, from kRegOps -- carry and overflow arithmetic (addc addv subc subv negc), div0u div1,
// the multiplies (mul.l muls.w mulu.w dmuls.l dmulu.l), exts swap xtrct, the rotates and shifts, movt sett clrt
// clrmac, sts/lds of MACH and MACL, stc/ldc of SR, GBR and VBR, the #imm,r0 logic and compare, cmp/ge gt pz str -- and
// one memory item in ten is a GBR access (and.b / or.b / xor.b / tst.b #imm,@(r0,GBR), mov.x @(disp,GBR),r0 and back,
// GBR pointing into scratch RAM). SE_GENERATED_ALPHABET=narrow switches that off. Not emitted: trapa and rte (they need
// a vector table and a handler laid out beside the function, and rte pops the stack), bsrf (a callee at a distance known
// only after layout: the call grammar's), and, in the first two grammars, stc/sts (ledger A2-16 and A3-5; the register-only
// grammar has them).
// Version 3 is not a program of that kind but a register-only function (kRegOps and bt/bf/bt/s/bf/s and braf over
// one to three instructions, r0-r7, returning r0) checked by TestPrintedC, which compiles the text the decompiler
// prints for it with the system C compiler and compares what it returns with the emulator's r0 (r0:r1 for an
// 8-byte return) for 32 inputs, and the control registers the Write_* calls leave with the ones the run ends with.
//
// The check, per function (and again with the mailbox marked volatile when the program has one),
// against the emulator's runs of that function from its entry: 4 inputs for version 2 (3 for version 1),
// and for a jump table one more per index. A decompilation is of the last function first; what it
// learned (DecompileResult::prototype, when known) is lent to the ones before it through
// DecompileRequest::prototypes, as the panel does.
//   Area 5 (unchanged), over the accesses the function itself made (not its callees', which run at
//   another call depth):
//   1. Decompile returns Ok, and the result is not unsupported, unimplemented or truncated.
//   2. Every (instruction, read or write, size) the emulator traced has an entry in
//      DecompileResult::accesses, or is an access to ordinary memory at a constant address (the
//      decompiler folds those into variables: every pointer reaching it came from a literal that
//      is not a hardware address and not memory the request marked), or is a literal-pool load
//      (folded by design), or is a local (an access in the stack area through r15 or r14: a variable
//      by design, ledger A11-1), or sits in code the output says it removed ("here is missing").
//      Everything else is a dropped access. A ledger entry stands for one access of one execution of
//      the instruction, never two (mac reads two operands: a printed one must not excuse a lost
//      second); an entry with a constant address only for an access at that address.
//   3. Of the accesses listed: each is printed exactly once, none is made twice on a path, none
//      is printed only on some of the paths that reach it, and the ledger's ordering invariants
//      (Sh2DecompilerTests.cpp's Ledger) hold. (FoldedIntoSwitch reports only what a run shows: two
//      accesses it joined, one made without the other.)
//   3b. (round 4, A5-19; round 5, A5-28; nightly, A5-46) A read the emulator traced at a register through the cached partition (an address
//      below 0x20000000 that the bus decodes to a register) has a caution on its instruction saying it is a cache read:
//      the emulator has no cache, Mednafen does, so the trace cannot show the difference and the output must say it. That
//      holds for a read through a pointer too, when the output derives the pointer from a register address: a pointer the engine
//      proved to be one of several register literals prints as `&REG` (`puVar2 = &VDP2_TVSTAT;`), an alias of one as a number
//      (`(xunknown4 *)0x25fe0110`), and an offset from one as `puVar1[4]`; the read through any of them is said as a direct one
//      is. The premise is the pointer's printed derivation (PointerPrintsFromHardware: the statement holding the read, or a variable
//      in it, assigned such a base through assignments, call arguments left out), not a name somewhere in the output: a register
//      named only as the argument of a call (`f(p, &VDP2_VRSIZE); x = *extraout_r6;`) is not the pointer, whose value after the
//      call the engine does not know, counted `cache.read.pointer.unrelated`. A pointer that derives from no register address (a
//      parameter, a loaded or computed address) is excused, counted `cache.read.anonymous`; the others are
//      `cache.read.pointer.said` or `...unsaid`.
//   Area 4:
//   4. Every instruction a run executed lies in DecompileResult::body, or in the code of a function
//      this one calls (or that function's body), or a flow warning names it (a warning at a jump taken for
//      a tail call, or an indirect jump treated as a call, names what the run executes after it).
//   5. The entry of another function inside the body has a "flow runs into" warning; no data word skipped
//      by a bra is in the body.
//   6. Each index of a jump table that a run dispatched with reaches a case that holds the code the
//      index runs (its own label, or the default of a switch whose bound check was taken out, or what
//      follows the switch) when that code does something the ledger lists, or a warning says cases may
//      be missing.
//   Area 3, for a prototype that is known (lent):
//   7. It is not lent when the analysis is incomplete: truncated, unsupported, unimplemented, bad data,
//      a case that is missing, an executed instruction outside the body; known and prototypeWithheld
//      agree.
//   8. Every one of r4-r7 the function reads before writing, and whose value changes what the function
//      does to the world (hardware, marked memory, memory through the parameter pointers, r0 if it
//      returns), is a parameter. Found by running again with that register changed (+0x200, then 0).
//      Every other register it reads before writing -- r0-r3, r8-r14, GBR, VBR, MACH, MACL -- is an input
//      no signature passes, so a lent prototype must have none whose value matters (round 3, A3-5); r0 is
//      compared whenever the function writes it on every path, which is what a dropped return is.
//   9. A return is claimed only if r0 is written on every run before the return; a function the
//      generator made to return r0 does not claim to return nothing.
//   10. A register CallEffects::preserved names has the value it started with at every return: r0-r15, SR,
//       GBR, VBR, MACH, MACL and PR (the four after SR started at values of their own).
//   11. A lent preserved set is honoured by the caller's text (patch 0028): when every function this one
//       calls was lent a signature that keeps r2-r7, MACH, MACL or SR, the text never reads that register
//       as a call's output (extraout_<reg>), whatever else the function does or is lent itself.
//   12. The mirror of 11 (round 3, A3-3): when a function this one calls was lent a signature that changes a
//       register the model keeps -- r8-r14, GBR, VBR -- the text never reads that register as the value it had
//       on entry (unaff_<reg>, in_<reg>), unless the function reads it on purpose.
//   And the generator's own contract (not the decompiler's): a version 2 function returns, restores
//   r8-r15 and ends with PR as it began (but what it was generated to change: Func::clobbers).
// The ground truth is emulator runs with different inputs (registers, what hardware reads
// return), united. The "other processor" of a mailbox poll answers a polled hardware register
// always and a polled address the request marks volatile only in that request's run: an unmarked
// work-RAM poll folds in the decompiler by design (DECOMPILER_GUIDE.md), so its run has no one
// answering and stops at the step limit. (Version 2 has no polls.)
//
// usage: SaturnExplorerSh2GeneratedTests <scratch dir>
//   SE_GENERATED_SEEDS=<n>   how many seeds (default 300)
//   SE_GENERATED_FIRST=<k>   the first seed (default 1): a nightly run moves it
//   SE_GENERATED_SEED=<k>    one seed, with everything printed
//   SE_GENERATED_GRAMMAR=1|2 only that grammar (default: the odd seeds are version 1, the even ones version 2)
//   SE_GENERATED_SHRINK=<n>  how many findings to reduce (default 3; 0 none; large: all)
//   SE_GENERATED_VERBOSE=1   print the full report of every failing seed, not just the first few
//   SE_GENERATED_TOKENS=1    print each output line with the addresses its tokens came from
//   SE_GENERATED_MUTATE=1|ledger|second|params|ret|body|cases|warn|preserved|lent|data|honour|kept
//                            damage every result as one defect would (1: delete the first ledger entry) and
//                            pass only if the check reports it: the checks checked
//   SE_GENERATED_COVERAGE=1  print the mnemonics of the generated programs and how often each was emitted
//   SE_GENERATED_ALPHABET=narrow  only the original eleven ALU ops (what the generator emitted before round 3 of area 2);
//                            =specials  also stc / sts (special register reads) in the first two grammars, fenced by default
//   SE_GENERATED_PRINTEDC=1|only  the printed-C differential (see TestPrintedC); SE_C_COMPILER names the compiler
//   SE_GENERATED_FENCE=selfops|folds  keep a known open class out of the grammar (see gFenceFolds)
//   SE_GENERATED_WORDS="D102 6010 ..."  check one hand-written function (hex words from the entry,
//                            literal pool among them) instead of generated ones; with
//                            SE_GENERATED_VOLATILE=<first>-<last> and SE_GENERATED_POLL=<addr>[:until-set].
//                            A failing seed prints the line that replays it, which for a module adds
//                            SE_GENERATED_FUNCS="start/entry/end,..." (hex offsets from the first word),
//                            SE_GENERATED_TABLES="jmp:sel:n:default:entry,...;...", SE_GENERATED_DATA and
//                            SE_GENERATED_RETURNS="1,0,..." (the generator's intent per function)
#include "DecompilerTestSupport.h"

#include "Decompiler/SaturnArchitecture.h"
#include "Decompiler/SaturnHardwareMap.h"
#include "Debug/Sh2Disasm.h"
#include "Decompiler/Sh2Decompiler.h"
#include "emulate.hh"
#include "Sh2EmulatorHooks.h"
#include "memstate.hh"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <map>
#include <memory>
#include <regex>
#include <set>
#include <sstream>
#include <tuple>

using namespace sfe::decomp;

namespace
{

std::string gConfig;
// A read whose value is used only by an expression the engine folds to a constant (x ^ x, x - x,
// x == x, x & 0, a test of bits a shift cleared) is dropped (ledger A5-1), and that one class fills
// a run. SE_GENERATED_FENCE keeps its triggers out of the grammar, so the run can look for others:
//   selfops  no sub, xor or cmp/eq of a register with itself
//   folds    (implies selfops) no two-register arithmetic, no immediate test, and the conditions of
//            a branch read only registers that cannot be constants
// Unset, nothing is fenced: the default run reports the class, as it should until it is fixed.
bool gFenceSelfOps = false, gFenceFolds = false;
bool gMutate = false;

const uint32_t kCode = 0x06009000u;       // where the function goes
const uint32_t kSentinel = 0x06000F00u;   // the return address: a nop the emulator stops at
const uint32_t kScratch4 = 0x06020100u;   // r4/r5 point into scratch RAM, away from the code
const uint32_t kScratch5 = 0x06021100u;
const uint32_t kScratch6 = 0x06022100u;   // r6/r7 before any literal is loaded into them
const uint32_t kScratch7 = 0x06023100u;
const uint32_t kScratchGbr = 0x06024100u;  // GBR on entry: the base of the @(disp,GBR) and @(r0,GBR) accesses
const uint32_t kStackTop = 0x06040000u;   // r15 on entry (version 2 uses the stack)
const uint32_t kStackLo = 0x0603E000u;    // the stack area: every access in [kStackLo, kStackHi) through r15 or the frame
const uint32_t kStackHi = 0x06041000u;    // pointer is a local, folded by design (A11-1)
const int kStepLimit = 2000;              // instructions per run; more is "did not finish"
const int kPollBusyReads = 2;             // reads of a polled address that see the busy value

// xorshift64*, as in the semantics test; one stream per seed.
struct Rng
{
    uint64_t s;
    explicit Rng(uint64_t seed) : s(seed * 0x9E3779B97F4A7C15ull + 0xD1B54A32D192ED03ull)
    {
        if (!s) s = 1;
        Next();
        Next();
    }
    uint32_t Next()
    {
        s ^= s >> 12;
        s ^= s << 25;
        s ^= s >> 27;
        return static_cast<uint32_t>((s * 2685821657736338717ull) >> 32);
    }
    uint32_t Below(uint32_t n) { return Next() % n; }
    bool Chance(uint32_t percent) { return Below(100) < percent; }
};

std::string Hex(uint32_t v, int digits = 8)
{
    char b[16];
    std::snprintf(b, sizeof b, "%0*X", digits, v);
    return b;
}

// ---- The ALU alphabet beyond the original eleven ops ---------------------------------------------
// form: M Rm,Rn   N Rn   Z no operand   I #imm,r0   A #imm,Rn.   flags: what the op reads and writes.
enum : uint8_t { kReadsN = 1, kReadsM = 2, kWritesN = 4, kReadsR0 = 8, kWritesR0 = 16 };
struct OpDef
{
    const char* name;
    uint16_t base;
    char form;
    uint8_t flags;
};

// Every instruction that is not a memory access, a PC-relative or a delayed branch, and does not move PR
// or the stack (trapa, rte and bsrf need a handler or a callee laid out beside the function; mac.l and
// mac.w, the loads and stores and the GBR operations are memory, which the printed-C oracle does not model).
const OpDef kRegOps[] = {
    { "add", 0x300C, 'M', 7 }, { "addc", 0x300E, 'M', 7 }, { "addv", 0x300F, 'M', 7 },
    { "sub", 0x3008, 'M', 7 }, { "subc", 0x300A, 'M', 7 }, { "subv", 0x300B, 'M', 7 },
    { "and", 0x2009, 'M', 7 }, { "or", 0x200B, 'M', 7 }, { "xor", 0x200A, 'M', 7 },
    { "tst", 0x2008, 'M', 3 }, { "not", 0x6007, 'M', 6 }, { "neg", 0x600B, 'M', 6 },
    { "negc", 0x600A, 'M', 6 }, { "cmp/eq", 0x3000, 'M', 3 }, { "cmp/hs", 0x3002, 'M', 3 },
    { "cmp/ge", 0x3003, 'M', 3 }, { "cmp/hi", 0x3006, 'M', 3 }, { "cmp/gt", 0x3007, 'M', 3 },
    { "cmp/str", 0x200C, 'M', 3 }, { "div0s", 0x2007, 'M', 3 }, { "div1", 0x3004, 'M', 7 },
    { "mul.l", 0x0007, 'M', 3 }, { "muls.w", 0x200F, 'M', 3 }, { "mulu.w", 0x200E, 'M', 3 },
    { "exts.b", 0x600E, 'M', 6 }, { "exts.w", 0x600F, 'M', 6 }, { "extu.b", 0x600C, 'M', 6 },
    { "extu.w", 0x600D, 'M', 6 }, { "swap.b", 0x6008, 'M', 6 }, { "swap.w", 0x6009, 'M', 6 },
    { "xtrct", 0x200D, 'M', 7 }, { "dmuls.l", 0x300D, 'M', 3 }, { "dmulu.l", 0x3005, 'M', 3 },
    { "mov", 0x6003, 'M', 6 }, { "mov", 0x6003, 'M', 6 }, { "add", 0x300C, 'M', 7 },
    { "rotl", 0x4004, 'N', 5 }, { "rotr", 0x4005, 'N', 5 }, { "rotcl", 0x4024, 'N', 5 },
    { "rotcr", 0x4025, 'N', 5 }, { "shal", 0x4020, 'N', 5 }, { "shar", 0x4021, 'N', 5 },
    { "shll", 0x4000, 'N', 5 }, { "shlr", 0x4001, 'N', 5 }, { "shll2", 0x4008, 'N', 5 },
    { "shlr2", 0x4009, 'N', 5 }, { "shll8", 0x4018, 'N', 5 }, { "shlr8", 0x4019, 'N', 5 },
    { "shll16", 0x4028, 'N', 5 }, { "shlr16", 0x4029, 'N', 5 }, { "dt", 0x4010, 'N', 5 },
    { "cmp/pz", 0x4011, 'N', 1 }, { "cmp/pl", 0x4015, 'N', 1 }, { "movt", 0x0029, 'N', 4 },
    { "movt", 0x0029, 'N', 4 }, { "sts mach", 0x000A, 'N', 4 }, { "sts macl", 0x001A, 'N', 4 },
    { "lds mach", 0x400A, 'N', 1 }, { "lds macl", 0x401A, 'N', 1 }, { "stc sr", 0x0002, 'N', 4 },
    { "stc gbr", 0x0012, 'N', 4 }, { "stc vbr", 0x0022, 'N', 4 }, { "ldc sr", 0x400E, 'N', 1 },
    { "ldc gbr", 0x401E, 'N', 1 }, { "ldc vbr", 0x402E, 'N', 1 }, { "sett", 0x0018, 'Z', 0 },
    { "clrt", 0x0008, 'Z', 0 }, { "div0u", 0x0019, 'Z', 0 }, { "clrmac", 0x0028, 'Z', 0 },
    { "and", 0xC900, 'I', 24 }, { "or", 0xCB00, 'I', 24 }, { "xor", 0xCA00, 'I', 24 },
    { "tst", 0xC800, 'I', 8 }, { "cmp/eq", 0x8800, 'I', 8 }, { "add", 0x7000, 'A', 5 },
    { "mov", 0xE000, 'A', 4 },
};
const size_t kRegOpCount = sizeof kRegOps / sizeof kRegOps[0];

// The state registers an op of the table changes for good, and reads, as Func::clobbers / Func::reads bits (17 gbr, 18 vbr,
// 19 mach, 20 macl). The multiplies write MACL (the double-length ones MACH too), lds / ldc load MACH, MACL, GBR and VBR from
// a general register, clrmac clears both; stc and sts (in the grammars that let them in) read them. A function that
// contains one of these changes (or reads) the register as surely as one the conventions grammar made to: the self-checks
// and the checks that ask what a callee changes must be told (the merge of area 2's alphabet and area 3's conventions).
uint32_t StateWritten(const OpDef& op)
{
    static const struct { const char* name; uint32_t bits; } kWrites[] = {
        { "mul.l", 1u << 20 },  { "muls.w", 1u << 20 }, { "mulu.w", 1u << 20 }, { "dmuls.l", 3u << 19 }, { "dmulu.l", 3u << 19 },
        { "lds mach", 1u << 19 }, { "lds macl", 1u << 20 }, { "clrmac", 3u << 19 }, { "ldc gbr", 1u << 17 }, { "ldc vbr", 1u << 18 },
    };
    for (const auto& w : kWrites)
        if (std::strcmp(op.name, w.name) == 0) return w.bits;
    return 0;
}
uint32_t StateRead(const OpDef& op)
{
    static const struct { const char* name; uint32_t bits; } kReads[] = {
        { "stc gbr", 1u << 17 }, { "stc vbr", 1u << 18 }, { "sts mach", 1u << 19 }, { "sts macl", 1u << 20 },
    };
    for (const auto& r : kReads)
        if (std::strcmp(op.name, r.name) == 0) return r.bits;
    return 0;
}
bool gTrackAlignment = true;    // SE_GENERATED_ALIGN=off: r4/r5 alignment is not tracked (the generator as it was before A11-2; for A/B counts and the mutation check)
bool gNarrowAlphabet = false;   // SE_GENERATED_ALPHABET=narrow: only the original eleven ALU ops
bool gSpecialReads = false;     // SE_GENERATED_ALPHABET=specials: stc / sts in the first two grammars too (ledger A2-16, A3-5)

// ---- The program model ----------------------------------------------------------------------

enum class LitKind : uint8_t { Hw, Ram, Mbox, Fn };
const char* KindName(LitKind k)
{
    return k == LitKind::Hw ? "hardware" : k == LitKind::Ram ? "RAM" : k == LitKind::Mbox ? "mailbox" : "function";
}

struct Lit
{
    uint32_t value = 0;
    LitKind kind = LitKind::Ram;
    int width = 4;   // the width accesses through it prefer (a register's documented width)
    int fn = -1;     // Fn: the function whose entry it is (value is filled in when the program is laid out)
};

struct Item
{
    // Poll is version 1 only; the types after it are version 2.
    enum Type : uint8_t { Alu, Mem, LitPtr, Skip, Loop, Poll, Spill, Call, Table, BraLoop, BraBack, DataSkip } type = Alu;
    const char* shape = "";
    std::vector<uint16_t> words;   // Alu, Mem: the instruction words (an indexed access: mov #k,r0 first)
    int base = -1;                 // Mem: the pointer register
    int reg = 6, lit = 0;          // LitPtr, Poll: the pointer register and its literal
    uint16_t cond = 0;             // Skip: the tst or cmp that sets T
    bool onTrue = true;            // Skip: bt (else bf)
    bool delayed = false;          // Skip: bt/s, bf/s with 'slot'
    int counter = 3, count = 2;    // Loop, BraLoop, BraBack
    int width = 2;                 // Poll
    bool pollWrite = true;         // Poll: stores 'pollValue' first
    int pollValue = 1;
    bool untilSet = false;         // Poll: waits for bit 0 to become set (else clear)
    std::vector<Item> slot;        // Skip with delayed: its one single-word instruction
    std::vector<Item> kids;        // Skip: the skipped instructions; Loop: the body; Spill: the block; Table: the default case
    // Version 2.
    bool stack = false;            // Mem: a local, through r15 or the frame pointer
    int spillReg = 1;              // Spill: the register pushed and popped around 'kids'
    int callee = -1;               // Call: the function called
    bool viaJsr = false;           // Call: jsr @Rn after a literal-pool load (else bsr)
    int jsrReg = 1, fnLit = 0;     // Call via jsr: the register and the literal (the callee's address)
    std::vector<uint16_t> setup;   // Call: argument set-up before the call
    std::vector<uint16_t> slotWords;   // Call: the delay slot's instruction (empty: nop)
    std::vector<uint16_t> after;   // Call: the registers restored from their callee-saved copies, then a read of a register the callee changed
    bool observes = false;         // Call: 'after' ends with that read
    int sel = 4;                   // Table: the register that indexes
    bool useHs = false;            // Table: cmp/hs against the case count (else cmp/hi against the last case)
    bool signedGuard = false;      // Table: cmp/ge (useHs) or cmp/gt instead: negative indices pass the bound check (ledger A4-7)
    int preCase = 0;               // Table, signed guard: the case the entry for index -1 (the word before the table) goes to
    std::vector<int> target;       // Table: for each entry, the index of the case it goes to, or -1: the default
    std::vector<std::vector<Item>> cases;   // Table: the bodies of the cases
    std::vector<uint16_t> data;    // DataSkip: the words skipped over
};

// One generated function (version 1 programs have exactly one, with the v1 prologue-free shape).
struct Func
{
    std::vector<Item> items;
    std::vector<Item> tail;        // version 1: the rts delay slot: empty (nop) or one instruction
    bool v2 = false;
    // How the function ends: rts, a tail call (jmp @Rn to another function after the epilogue), or no
    // end at all: the code runs on into the next function.
    enum End : uint8_t { Rts, TailJump, Fall } end = Rts;
    int tailTo = -1, tailLit = 0, tailReg = 1;
    std::vector<int> pushed;       // callee-saved registers pushed at entry (r8-r14), in push order
    bool savePr = false;           // sts.l pr,@-r15 after the pushes (lds.l @r15+,pr before them in the epilogue)
    int prReg = -1;                // else PR is kept in this pushed register (sts pr,Rn / lds Rn,pr)
    int frame = 0;                 // bytes of locals: add #-frame,r15
    bool fp = false;               // r14 is a frame pointer: locals are addressed through it, mov r14,r15 releases the frame
    std::vector<uint16_t> argSave; // mov r4,r8 ... at entry: arguments kept across calls
    bool returns = false;          // r0 is written on every path just before the return
    uint16_t retWord = 0;          // ...by this instruction
    bool retInSlot = false;        // ...which is rts's delay slot
    bool noR0 = false;             // never writes r0 itself (a function that returns nothing)
    bool slotPop = false;          // the last stack instruction of the epilogue is rts's delay slot
    int sel = -1;                  // the register a jump table indexes with: an integer, not a pointer
    // A shared epilogue: two paths that both end with bra to the exit.
    bool shared = false;
    uint16_t sharedCond = 0;
    bool sharedOnTrue = true;
    std::vector<Item> pathA, pathB;
    std::vector<uint16_t> slotA, slotB;   // each path's bra delay slot (empty: nop)
    bool epilogueBelow = false;    // the exit sits before the entry
    // Conventions the compiler does not use (a hand-written routine's: ledger A3-3 and A3-5). Bit n is Rn for
    // r8-r14, and 17 gbr, 18 vbr, 19 mach, 20 macl as in CallEffects::preserved.
    uint32_t clobbers = 0;         // registers this function changes for good (never saved)
    uint32_t effClobbers = 0;      // ...and what its callees, a function it jumps to or runs on into leave changed too
    uint32_t reads = 0;            // registers whose entry value it reads on purpose (r1-r3, r8-r14, gbr, vbr, mach, macl)
};

struct Program
{
    int version = 1;
    std::vector<Lit> lits;
    uint32_t mbox = 0x26001000u;   // the mailbox the request marks volatile, 16 bytes
    std::vector<Func> fns;         // fns[0] is the function a version 1 program is about; later ones follow in memory
};

// ---- Generation ----------------------------------------------------------------------------

struct Gen
{
    Rng rng;
    // A stream of its own for mac: the other items' draws, and so every program a seed made before mac
    // was in the grammar, are unchanged; mac only adds an item.
    Rng macRng;
    Rng guardRng;   // a stream of its own, so that the signed guards do not move the programs of the seeds found before them
    // A stream of its own for the conventions below, for the same reason: the programs of a seed that
    // does not use them are the ones it always made.
    Rng convRng;
    bool convModule = false;  // this module has functions with conventions of their own
    int clobberReg = 12;      // the register (r12-r14) its functions change for good: no function of it uses that one for anything else
    // ...and one for the wide ALU alphabet (addc .. swap, the rotates and shifts, movt, ldc/stc ...): which
    // ALU items become one of those is drawn here, so the shapes of the narrow alphabet keep their draws.
    Rng wideRng;
    // ...and one for the frame walk (a loop that moves a stack address, round 5 A3-16): whether a function has one and where.
    Rng frameRng;
    // What the function being made changes for good / reads on entry through the wide alphabet and the GBR accesses
    // (an over-approximation: an item the grammar drops again is counted, which the checks that use it allow).
    uint32_t wideClobbers = 0;
    uint32_t wideReads = 0;
    Program p;
    int align[8];      // the natural alignment of every literal that may be in a register
    int pref[8];       // ...and the width of the latest
    bool everConst[8] = {};   // a register that has been given a constant (folds fence)
    // Version 2 only: a function body keeps to registers that hold a value on every path to it, so
    // that what the decompiler sees is a function with a prototype and not one reading garbage.
    bool disc = false;
    uint32_t defined = 0;     // bit n: Rn holds a value here, on every path
    bool noR0 = false;        // the function never writes r0 itself
    bool codeInRegs = false;  // the function has a jump table: r0 and r1 may hold a code address on the way out of it
    uint32_t leftScratch = 0; // bit n: Rn (r1-r3) is read for the value the caller left in it, which may be the caller's jsr target (A4-15)
    int selReg = -1;          // a register holding an integer (a jump table's index), never a pointer
    int frame = 0;            // bytes of locals the function has
    bool fp = false;          // locals are addressed through r14
    bool inSpill = false;     // r15 is moved: no local is addressed through it here
    int loopDepth = 0;        // inside a loop body: a pointer advanced here is advanced again by the next iteration
    std::vector<Func> built;  // the functions of this module generated so far (the later ones)
    Gen(uint64_t seed) : rng(seed), macRng(seed ^ 0x6D61636D61636D61ull), guardRng(seed ^ 0x7367756172643A5Aull), convRng(seed ^ 0x636F6E7663727667ull), wideRng(seed ^ 0x77696465414C5521ull), frameRng(seed ^ 0x6672616D6557616Cull)
    {
        for (int i = 0; i < 8; ++i) align[i] = 4, pref[i] = 4;
        p.mbox = 0x26001000u + 0x10u * rng.Below(4);
    }

    bool Def(int r) const { return (defined >> r) & 1u; }
    // 'r' if it holds a value, else a random register up to 'hi' that does, else -1.
    int PickSrc(int r, int hi)
    {
        if (Def(r)) return r;
        int pool[16], n = 0;
        for (int i = 0; i <= hi; ++i)
            if (Def(i)) pool[n++] = i;
        return n ? pool[rng.Below(static_cast<uint32_t>(n))] : -1;
    }
    // As PickSrc, never r0: a copy of r0 into r0 is not a write.
    int PickSrcNotR0(int r)
    {
        if (r >= 1 && Def(r)) return r;
        int pool[16], n = 0;
        for (int i = 1; i <= 7; ++i)
            if (Def(i)) pool[n++] = i;
        return n ? pool[rng.Below(static_cast<uint32_t>(n))] : -1;
    }
    // r1-r3 other than 'avoid': a destination for a function that leaves r0 alone.
    int NonR0(int avoid)
    {
        int d = 1 + static_cast<int>(rng.Below(3));
        if (d == avoid) d = d == 3 ? 2 : d + 1;
        return d;
    }

    int Alignment(uint32_t v) const { return (v & 3) == 0 ? 4 : (v & 1) == 0 ? 2 : 1; }

    Lit NewLit(LitKind kind)
    {
        Lit l;
        l.kind = kind;
        if (kind == LitKind::Hw)
        {
            size_t n = 0;
            const HardwareRegister* regs = SaturnHardwareRegisters(n);
            if (rng.Chance(75))
            {
                const HardwareRegister& r = regs[rng.Below(static_cast<uint32_t>(n))];
                l.value = r.address;
                if (l.value < 0x20000000u && rng.Chance(50)) l.value |= 0x20000000u;   // cache-through
                l.width = r.size;
            }
            else
            {
                const std::vector<HardwareRange>& ranges = HardwareRanges();
                const HardwareRange& r = ranges[rng.Below(static_cast<uint32_t>(ranges.size()))];
                const uint32_t span = r.last - r.first;
                l.value = (r.first + rng.Below(std::min<uint32_t>(span, 0x100u) + 1)) & ~3u;
                if (l.value < r.first) l.value = r.first;
                l.width = 4;
            }
        }
        else if (kind == LitKind::Ram)
        {
            l.value = (rng.Chance(75) ? 0x26000000u : 0x06000000u) + 0x2000u + 0x80u * rng.Below(8) + 4u * rng.Below(4);
            l.width = rng.Chance(50) ? 4 : rng.Chance(50) ? 2 : 1;
        }
        else
        {
            l.value = (rng.Chance(75) ? p.mbox : (p.mbox & ~0x20000000u)) + 4u * rng.Below(3);
            l.width = rng.Chance(50) ? 2 : rng.Chance(50) ? 4 : 1;
        }
        l.width = std::min(l.width, Alignment(l.value));
        return l;
    }

    LitKind PickKind()
    {
        const uint32_t r = rng.Below(100);
        return r < 45 ? LitKind::Hw : r < 75 ? LitKind::Ram : LitKind::Mbox;
    }

    Item MakeLitPtr(int reg, LitKind kind)
    {
        if (disc && reg == selReg) reg = 13 - reg;   // the jump table's integer stays one
        Item it;
        it.type = Item::LitPtr;
        it.reg = reg;
        it.lit = static_cast<int>(p.lits.size());
        it.shape = kind == LitKind::Hw ? "litptr.hw" : kind == LitKind::Ram ? "litptr.ram" : "litptr.mbox";
        p.lits.push_back(NewLit(kind));
        align[reg] = std::min(align[reg], Alignment(p.lits.back().value));
        pref[reg] = p.lits.back().width;
        if (disc) defined |= 1u << reg;
        return it;
    }

    int PickWidth(int base)
    {
        int w = 1 << rng.Below(3);
        if (base >= 6)
        {
            if (rng.Chance(70)) w = pref[base];
            w = std::min(w, align[base]);
        }
        else if (gTrackAlignment)
            w = std::min(w, align[base]);   // r4/r5 start aligned; a post-increment or pre-decrement of a smaller width lowers it (A11-2)
        return w;
    }

    // The ops the narrow alphabet never had (carry and overflow arithmetic, DIV, the multiplies, extensions,
    // swaps, rotates and shifts, T and MAC moves, the #imm,r0 logic, ldc/stc of SR, GBR and VBR), with the
    // destination 'n' and the source 'm' the caller drew. Reads only registers that hold a value (version
    // 2), writes r0 only if the function may, and never the loop counter (n was chosen to avoid it).
    // False: nothing of the alphabet fits here, and the narrow op is made.
    bool MakeWideAlu(int n, int m, uint32_t imm, Item& it)
    {
        it.type = Item::Alu;
        const OpDef* op = nullptr;
        for (int tries = 0; tries < 8 && !op; ++tries)
        {
            const OpDef* cand = &kRegOps[wideRng.Below(static_cast<uint32_t>(kRegOpCount))];
            // stc / sts copy a special register into a general one. In the first two grammars they are fenced (the
            // third has them): after a jump table's case one makes the recovered switch print on the loaded address
            // (ledger A2-16), and one returned in r0 is a copy of an input register that prints void (A3-5).
            // SE_GENERATED_ALPHABET=specials lets them in.
            if (!gSpecialReads && (std::strncmp(cand->name, "stc", 3) == 0 || std::strncmp(cand->name, "sts", 3) == 0)) continue;
            if (disc)
            {
                if ((cand->flags & kReadsN) && !Def(n)) continue;
                if ((cand->flags & kReadsR0) && !Def(0)) continue;
                if ((cand->flags & kWritesR0) && noR0) continue;
                if (cand->flags & kReadsM)
                {
                    const int s = PickSrc(m, 5);
                    if (s < 0) continue;
                    m = s;
                }
            }
            op = cand;
        }
        if (!op) return false;
        wideClobbers |= StateWritten(*op);
        wideReads |= StateRead(*op);
        switch (op->form)
        {
        case 'M': it.words = { uint16_t(op->base | n << 8 | m << 4) }; break;
        case 'N': it.words = { uint16_t(op->base | n << 8) }; break;
        case 'I': it.words = { uint16_t(op->base | imm) }; break;
        case 'A': it.words = { uint16_t(op->base | n << 8 | imm) }; break;
        default: it.words = { op->base }; break;
        }
        it.shape = "alu.wide";
        if (disc)
        {
            if (op->flags & kWritesN) defined |= 1u << n;
            if (op->flags & kWritesR0) defined |= 1u;
        }
        return true;
    }

    Item MakeAlu(int avoid)
    {
        Item it;
        it.type = Item::Alu;
        auto dest = [&] {
            int d = static_cast<int>(rng.Below(4));
            return d == avoid ? (d + 1) & 3 : d;
        };
        int n = dest();
        int m = static_cast<int>(rng.Below(6));
        if (gFenceSelfOps && m == n) m = (m + 1) % 6;
        const uint32_t imm = rng.Below(256);
        uint32_t choice = rng.Below(11);
        if (gFenceFolds)
        {
            static const uint32_t kSafe[] = { 0, 1, 2, 8, 9, 10 };
            choice = kSafe[rng.Below(6)];
        }
        if (disc)
        {
            if (noR0 && n == 0) n = NonR0(avoid);
            const bool readsN = choice == 1 || (choice >= 3 && choice <= 8);
            const bool readsM = choice == 2 || (choice >= 3 && choice <= 7) || choice >= 9;
            if (readsN && !Def(n)) choice = 0;
            if (choice != 0 && readsM)
            {
                m = PickSrc(m, 5);
                if (m < 0) choice = 0;
            }
        }
        if (!gNarrowAlphabet && !gFenceFolds && !gFenceSelfOps && wideRng.Chance(25))
        {
            Item wide;
            if (MakeWideAlu(n, m, imm, wide)) return wide;
        }
        if (choice == 0) everConst[n] = true;
        switch (choice)
        {
        case 0: it.shape = "alu.mov_imm"; it.words = { uint16_t(0xE000 | n << 8 | imm) }; break;
        case 1: it.shape = "alu.add_imm"; it.words = { uint16_t(0x7000 | n << 8 | imm) }; break;
        case 2: it.shape = "alu.mov"; it.words = { uint16_t(0x6003 | n << 8 | m << 4) }; break;
        case 3: it.shape = "alu.add"; it.words = { uint16_t(0x300C | n << 8 | m << 4) }; break;
        case 4: it.shape = "alu.sub"; it.words = { uint16_t(0x3008 | n << 8 | m << 4) }; break;
        case 5: it.shape = "alu.and"; it.words = { uint16_t(0x2009 | n << 8 | m << 4) }; break;
        case 6: it.shape = "alu.or"; it.words = { uint16_t(0x200B | n << 8 | m << 4) }; break;
        case 7: it.shape = "alu.xor"; it.words = { uint16_t(0x200A | n << 8 | m << 4) }; break;
        case 8: it.shape = "alu.shll2"; it.words = { uint16_t(0x4008 | n << 8) }; break;
        case 9: it.shape = "alu.extu_b"; it.words = { uint16_t(0x600C | n << 8 | m << 4) }; break;
        default: it.shape = "alu.extu_w"; it.words = { uint16_t(0x600D | n << 8 | m << 4) }; break;
        }
        if (disc) defined |= 1u << n;
        return it;
    }

    // A load or store. 'single': exactly one instruction word (a delay slot).
    // The GBR-based accesses: and.b / or.b / xor.b / tst.b #imm,@(r0,GBR) after a mov #k,r0, and the
    // mov.b / mov.w / mov.l @(disp,GBR),r0 and back. GBR is set to scratch RAM by the runner and the code
    // never writes it, so the address is a pointer the decompiler cannot know (unaff_gbr + k).
    bool MakeGbrMem(bool single, Item& it)
    {
        if (single || gNarrowAlphabet || gFenceFolds || gFenceSelfOps) return false;
        const bool logic = wideRng.Chance(50);
        const uint32_t k = wideRng.Below(16), imm = wideRng.Below(256);
        if (logic)
        {
            if (disc && noR0) return false;
            static const uint16_t kOps[] = { 0xCD00, 0xCF00, 0xCE00, 0xCC00 };   // and.b or.b xor.b tst.b
            const uint32_t which = wideRng.Below(4);
            it.words = { uint16_t(0xE000 | k), uint16_t(kOps[which] | imm) };
            it.shape = "mem.gbr.logic";
            it.width = 1;
            everConst[0] = true;
            if (disc) defined |= 1u;
            return true;
        }
        const bool write = wideRng.Chance(40);
        const uint32_t w = 1u << wideRng.Below(3), disp = wideRng.Below(16);
        if (disc && (write ? !Def(0) : noR0)) return false;
        it.words = { uint16_t((write ? 0xC000 : 0xC400) | (w == 1 ? 0 : w == 2 ? 0x100 : 0x200) | disp) };
        it.shape = write ? "mem.gbr.st" : "mem.gbr.ld";
        it.width = static_cast<int>(w);
        if (disc && !write) defined |= 1u;
        return true;
    }

    Item MakeMem(int avoid, bool single)
    {
        Item it;
        it.type = Item::Mem;
        if (wideRng.Chance(10) && MakeGbrMem(single, it))
        {
            wideReads |= 1u << 17;   // the address of each of them is GBR's value, read on purpose
            return it;
        }
        int base = 4 + static_cast<int>(rng.Below(4));
        bool write = rng.Chance(45);
        if (disc)
        {
            // A pointer that holds a value and is not the jump table's integer.
            int pool[4], n = 0;
            for (int r = 4; r < 8; ++r)
                if (Def(r) && r != selReg) pool[n++] = r;
            if (n == 0) return MakeAlu(avoid);
            if (!(Def(base) && base != selReg)) base = pool[rng.Below(static_cast<uint32_t>(n))];
        }
        int w = PickWidth(base);
        const int sz = w == 1 ? 0 : w == 2 ? 1 : 2;
        int form = static_cast<int>(rng.Below(100));
        form = form < 40 ? 0 : form < 70 ? 1 : form < 85 ? 2 : form < 94 ? 3 : 4;   // plain disp indexed inc tas
        if (single && form == 2) form = 0;
        if (form == 3 && base >= 6) form = 0;
        // In a loop body the pointer is advanced again by the next iteration, which the widths drawn before it do not
        // know of: only an advance that keeps its alignment is made there (A11-2).
        if (gTrackAlignment && form == 3 && loopDepth > 0 && w < align[base]) form = 0;
        int data = static_cast<int>(rng.Below(4));
        if (!write && data == avoid) data = (data + 1) & 3;
        if (disc)
        {
            if (write)
            {
                const int d = PickSrc(data, 3);
                if (d < 0) write = false;
                else data = d;
            }
            if (!write)
            {
                if (data == avoid) data = (data + 1) & 3;
                if (noR0 && data == 0) data = NonR0(avoid);
            }
            // The byte and word displacement forms move r0, the indexed forms load r0 first.
            if (form == 1 && w < 4 && (write ? !Def(0) : noR0)) form = 0;
            if (form == 2 && noR0) form = 0;
        }
        it.base = base;
        const char* kindName[2][5] = { { "mem.ld.plain", "mem.ld.disp", "mem.ld.indexed", "mem.ld.inc", "mem.tas" },
                                       { "mem.st.plain", "mem.st.disp", "mem.st.indexed", "mem.st.dec", "mem.tas" } };
        it.shape = kindName[write ? 1 : 0][form];
        switch (form)
        {
        case 0:
            it.words = { uint16_t(write ? 0x2000 | base << 8 | data << 4 | sz : 0x6000 | data << 8 | base << 4 | sz) };
            break;
        case 1:
        {
            const uint32_t d = rng.Below(16);
            if (w == 4)
                it.words = { uint16_t(write ? 0x1000 | base << 8 | data << 4 | d : 0x5000 | data << 8 | base << 4 | d) };
            else   // byte and word forms move r0 only
                it.words = { uint16_t((write ? 0x8000 : 0x8400) | sz << 8 | base << 4 | d) };
            break;
        }
        case 2:
        {
            const uint32_t k = w * rng.Below(8);
            everConst[0] = true;
            it.words = { uint16_t(0xE000 | k),
                         uint16_t(write ? 0x0004 | base << 8 | data << 4 | sz : 0x000C | data << 8 | base << 4 | sz) };
            break;
        }
        case 3:
            it.words = { uint16_t(write ? 0x2004 | base << 8 | data << 4 | sz : 0x6004 | data << 8 | base << 4 | sz) };
            break;
        default:
            it.words = { uint16_t(0x401B | base << 8) };
            w = 1;
            break;
        }
        it.width = w;
        if (gTrackAlignment && form == 3) align[base] = std::min(align[base], w);   // @Rm+ / @-Rn moves the pointer by the width
        if (disc && !write)
        {
            if (form == 1 && w < 4) defined |= 1u;
            else if (form != 4) defined |= 1u << data;
        }
        if (disc && form == 2) defined |= 1u;
        return it;
    }

    // mac.l / mac.w @Rm+,@Rn+: two reads in one instruction, each through a pointer that is then advanced
    // by the width. Rm and Rn are among r4-r7 (a pointer into scratch RAM, a hardware register, an ordinary
    // RAM address or a mailbox), sometimes the same register (the second read is then the next word). A
    // literal pointer must be aligned for the width, and is only as aligned as that afterwards. Version 1
    // only: the discipline of version 2 (registers that hold a value on every path) does not apply to it.
    Item MakeMac()
    {
        Item it;
        it.type = Item::Mem;
        const bool isLong = macRng.Chance(50);
        const int w = isLong ? 4 : 2;
        int pool[4], k = 0;
        for (int r = 4; r < 8; ++r)
            if (r < 6 || align[r] >= w) pool[k++] = r;
        const int n = pool[macRng.Below(static_cast<uint32_t>(k))];
        const int m = macRng.Chance(15) ? n : pool[macRng.Below(static_cast<uint32_t>(k))];
        it.shape = isLong ? "mem.mac.l" : "mem.mac.w";
        it.words = { uint16_t((isLong ? 0x000F : 0x400F) | n << 8 | m << 4) };
        it.base = n;
        it.width = w;
        for (int r : { n, m })
            if (r >= 6) align[r] = std::min(align[r], w);
        return it;
    }

    Item MakeSimple(int avoid, bool single, bool allowLit)
    {
        const uint32_t r = rng.Below(100);
        if (allowLit && r < 12) return MakeLitPtr(6 + static_cast<int>(rng.Below(2)), disc ? PickKind2() : PickKind());
        return r < 50 ? MakeAlu(avoid) : MakeMem(avoid, single);
    }

    Item MakeSkip()
    {
        Item it;
        it.type = Item::Skip;
        int n = static_cast<int>(rng.Below(4));
        int m = static_cast<int>(rng.Below(4));
        if (gFenceSelfOps && m == n) m = (m + 1) & 3;
        uint32_t form = rng.Below(5);
        bool noCond = false;
        if (disc)
        {
            if ((form == 1 || form == 2) && !Def(0)) form = 0;
            n = PickSrc(n, 7);
            m = PickSrc(m, 7);
            if (n < 0 || m < 0) noCond = true;
        }
        if (gFenceFolds)
        {
            // Registers that were never given a constant, or the pointer parameters.
            std::vector<int> ok;
            for (int r : { 0, 1, 2, 4, 5 })
                if (!everConst[r]) ok.push_back(r);
            const int a = ok[rng.Below(static_cast<uint32_t>(ok.size()))], b = ok[rng.Below(static_cast<uint32_t>(ok.size()))];
            it.cond = form < 3 ? uint16_t(0x2008 | a << 8 | b << 4) : uint16_t(0x4015 | a << 8);   // tst Rm,Rn / cmp/pl Rn
            form = 99;
        }
        switch (noCond ? 98u : form)
        {
        case 98: it.cond = 0x0018; break;                                                  // sett: nothing holds a value to test
        case 99: break;
        case 0: it.cond = uint16_t(0x2008 | n << 8 | m << 4); break;                       // tst Rm,Rn
        case 1: it.cond = uint16_t(0xC800 | (1u << rng.Below(8))); break;                  // tst #imm,r0
        case 2: it.cond = uint16_t(0x8800 | rng.Below(4)); break;                           // cmp/eq #imm,r0
        case 3: it.cond = uint16_t(0x4015 | n << 8); break;                                // cmp/pl Rn
        default: it.cond = uint16_t(0x3000 | n << 8 | m << 4); break;                       // cmp/eq Rm,Rn
        }
        it.onTrue = rng.Chance(50);
        it.delayed = rng.Chance(35);
        it.shape = it.delayed ? (it.onTrue ? "skip.bt_s" : "skip.bf_s") : (it.onTrue ? "skip.bt" : "skip.bf");
        if (it.delayed) it.slot.push_back(MakeSimple(-1, true, false));
        const uint32_t afterSlot = defined;   // the slot runs on both paths, the skipped part on one
        const int kids = 1 + static_cast<int>(rng.Below(3));
        for (int i = 0; i < kids; ++i) it.kids.push_back(MakeSimple(-1, false, true));
        if (disc) defined = afterSlot;
        return it;
    }

    Item MakeLoop()
    {
        Item it;
        it.type = Item::Loop;
        it.shape = "loop";
        it.count = 2 + static_cast<int>(rng.Below(3));
        everConst[it.counter] = true;
        const uint32_t before = defined;
        const int kids = 1 + static_cast<int>(rng.Below(3));
        ++loopDepth;
        for (int i = 0; i < kids; ++i) it.kids.push_back(rng.Chance(40) ? MakeAlu(it.counter) : MakeMem(it.counter, false));
        --loopDepth;
        if (disc) defined = before | (1u << it.counter);
        return it;
    }

    Item MakePoll()
    {
        Item it;
        it.type = Item::Poll;
        it.reg = 7;
        const uint32_t r = rng.Below(100);
        const LitKind kind = r < 60 ? LitKind::Mbox : r < 85 ? LitKind::Hw : LitKind::Ram;
        Item ptr = MakeLitPtr(7, kind);   // registers the literal; the poll item carries it
        it.lit = ptr.lit;
        it.width = p.lits[it.lit].width;
        it.pollWrite = rng.Chance(70);
        it.pollValue = 1 + static_cast<int>(rng.Below(3));
        it.untilSet = rng.Chance(30);
        it.shape = kind == LitKind::Hw ? "poll.hw" : kind == LitKind::Ram ? "poll.ram" : "poll.mbox";
        return it;
    }

    // ---- Version 2: modules of functions with stacks, calls and flow -----------------------------

    LitKind PickKind2()
    {
        const uint32_t r = rng.Below(100);
        return r < 45 ? LitKind::Hw : r < 85 ? LitKind::Ram : LitKind::Mbox;
    }

    // A local: a load or store through r15 (or the frame pointer r14), inside the frame.
    Item MakeStackMem(int avoid)
    {
        Item it;
        it.type = Item::Mem;
        it.stack = true;
        const int base = fp ? 14 : 15;
        bool write = rng.Chance(50);
        const int w = 1 << rng.Below(3);
        const int sz = w == 1 ? 0 : w == 2 ? 1 : 2;
        int data = static_cast<int>(rng.Below(4));
        uint32_t form = rng.Below(100);
        form = form < 12 ? 0 : form < 68 ? 1 : 2;   // plain at offset 0, displacement, indexed by a constant
        if (write)
        {
            const int d = PickSrc(data, 3);
            if (d < 0) write = false;
            else data = d;
        }
        if (!write && data == avoid) data = (data + 1) & 3;
        if (!write && noR0 && data == 0) data = NonR0(avoid);
        if (form == 1 && w < 4 && (write ? !Def(0) : noR0)) form = 0;   // the byte and word forms move r0
        if (form == 2 && noR0) form = 0;                                 // the indexed form loads r0 first
        const char* names[2][3] = { { "stack.ld.plain", "stack.ld.disp", "stack.ld.indexed" },
                                    { "stack.st.plain", "stack.st.disp", "stack.st.indexed" } };
        it.shape = names[write ? 1 : 0][form];
        it.base = base;
        it.width = w;
        const uint32_t room = static_cast<uint32_t>(frame / w);   // how many units of w fit the frame
        switch (form)
        {
        case 0:
            it.words = { uint16_t(write ? 0x2000 | base << 8 | data << 4 | sz : 0x6000 | data << 8 | base << 4 | sz) };
            break;
        case 1:
        {
            const uint32_t d = rng.Below(std::min<uint32_t>(16, room));
            if (w == 4)
                it.words = { uint16_t(write ? 0x1000 | base << 8 | data << 4 | d : 0x5000 | data << 8 | base << 4 | d) };
            else
                it.words = { uint16_t((write ? 0x8000 : 0x8400) | sz << 8 | base << 4 | d) };
            break;
        }
        default:
        {
            const uint32_t k = static_cast<uint32_t>(w) * rng.Below(std::min<uint32_t>(8, room));
            it.words = { uint16_t(0xE000 | k),
                         uint16_t(write ? 0x0004 | base << 8 | data << 4 | sz : 0x000C | data << 8 | base << 4 | sz) };
            break;
        }
        }
        if (disc && !write)
        {
            if (form == 1 && w < 4) defined |= 1u;
            else defined |= 1u << data;
        }
        if (form == 2) defined |= 1u;
        return it;
    }

    // A frame address in r2 walked down the frame by a loop that stores through it (`mov.l r1,@-r2`), then cleared: the address
    // is lowered at every pass, which A3-9's bases never settled on (ledger A3-16, 3.4 s and a function withheld "too many paths").
    // r1 is the counter and ends 0, r2 ends 0: both are scratch that later items may use. T changes.
    Item MakeFrameWalk()
    {
        Item it;
        it.type = Item::Alu;
        it.shape = "frame.walk";
        const int steps = std::min(frame / 4, 3);
        it.words = { 0x62F3,                                  // mov r15,r2
                     uint16_t(0x7200 | frame),                // add #frame,r2: the top of the locals
                     uint16_t(0xE100 | steps),                // mov #steps,r1
                     0x2216,                                  // L: mov.l r1,@-r2
                     0x4110,                                  //    dt r1
                     0x8BFC,                                  //    bf L
                     0xE200 };                                // mov #0,r2
        defined |= 0x6u;
        return it;
    }

    // push Rn ; block ; pop Rn
    Item MakeSpill()
    {
        Item it;
        it.type = Item::Spill;
        it.shape = "spill";
        it.spillReg = 1 + static_cast<int>(rng.Below(5));
        const uint32_t before = defined;
        const bool was = inSpill;
        inSpill = true;
        const int kids = 1 + static_cast<int>(rng.Below(3));
        for (int i = 0; i < kids; ++i) it.kids.push_back(MakeSimple(-1, false, false));
        inSpill = was;
        const uint32_t bit = 1u << it.spillReg;
        defined = (defined & ~bit) | (before & bit);   // the pop gives back what it was
        return it;
    }

    // The instruction that copies register 'bit' (r8-r14, or 17 gbr, 18 vbr, 19 mach, 20 macl) into Rd.
    static uint16_t ReadRegisterInto(int bit, int d)
    {
        switch (bit)
        {
        case 17: return uint16_t(0x0012 | d << 8);   // stc gbr,Rd
        case 18: return uint16_t(0x0022 | d << 8);   // stc vbr,Rd
        case 19: return uint16_t(0x000A | d << 8);   // sts mach,Rd
        case 20: return uint16_t(0x001A | d << 8);   // sts macl,Rd
        default: return uint16_t(0x6003 | d << 8 | bit << 4);   // mov Rn,Rd
        }
    }

    static void CalleesOf(const std::vector<Item>& items, std::vector<int>& out)
    {
        for (const Item& it : items)
        {
            if (it.type == Item::Call) out.push_back(it.callee);
            CalleesOf(it.slot, out);
            CalleesOf(it.kids, out);
            for (const std::vector<Item>& c : it.cases) CalleesOf(c, out);
        }
    }

    // The registers r4-r7 (bits 4-7) that code uses as the base of a memory access, a poll or a table's index: the ones a
    // caller must pass a pointer in.
    static uint32_t UsedAsPointer(const std::vector<Item>& items)
    {
        uint32_t m = 0;
        for (const Item& it : items)
        {
            if ((it.type == Item::Mem && !it.stack && it.base >= 4 && it.base < 8) || (it.type == Item::Poll && it.reg >= 4 && it.reg < 8))
                m |= 1u << (it.type == Item::Mem ? it.base : it.reg);
            m |= UsedAsPointer(it.slot) | UsedAsPointer(it.kids);
            for (const std::vector<Item>& c : it.cases) m |= UsedAsPointer(c);
        }
        return m;
    }
    // ...by function 'fi' or anything it calls, jumps to or runs on into (A4-9: a constant passed to a callee that stores
    // through it ends the run at an address nothing is mapped at).
    static uint32_t PointerArgs(int fi, const std::vector<Func>& fns, std::set<int>& seen)
    {
        if (fi < 0 || fi >= static_cast<int>(fns.size()) || !seen.insert(fi).second) return 0;
        const Func& f = fns[fi];
        uint32_t m = UsedAsPointer(f.items) | UsedAsPointer(f.pathA) | UsedAsPointer(f.pathB) | UsedAsPointer(f.tail);
        std::vector<int> callees;
        CalleesOf(f.items, callees);
        CalleesOf(f.pathA, callees);
        CalleesOf(f.pathB, callees);
        for (int c : callees) m |= PointerArgs(c, fns, seen);
        if (f.end == Func::TailJump) m |= PointerArgs(f.tailTo, fns, seen);
        if (f.end == Func::Fall) m |= PointerArgs(fi + 1, fns, seen);
        return m;
    }

    // A call: arguments in r4-r7 (some set in the delay slot), bsr or jsr through a literal, the
    // arguments kept in callee-saved registers restored after it.
    Item MakeCall(int callee, const std::vector<Func>& fns)
    {
        Item it;
        it.type = Item::Call;
        it.callee = callee;
        it.viaJsr = rng.Chance(40);
        it.shape = it.viaJsr ? "call.jsr" : "call.bsr";
        const int nargs = static_cast<int>(rng.Below(5));
        std::vector<uint16_t> args;
        uint32_t def = defined;
        for (int k = 0; k < nargs; ++k)
        {
            const int reg = 4 + k;
            const uint32_t r = rng.Below(100);
            uint16_t w = uint16_t(0xE000 | reg << 8 | rng.Below(256));   // mov #imm,Rk
            if (r >= 35 && r < 70)
            {
                int pool[16], n = 0;
                for (int i = 0; i < 8; ++i)
                    if ((def >> i) & 1u) pool[n++] = i;
                if (n)
                {
                    const int src = pool[rng.Below(static_cast<uint32_t>(n))];
                    // A jump table leaves a code address in r0 (mova) and r1 (the entry) on the paths
                    // through a case, and arithmetic carries it to r2 and r3. Passed on as an argument it
                    // is a pointer the callee may store through: into the program's own literal pool
                    // (seed 104955: the callee's 'mov.w r1,@r6' wrote the word a later 'jmp @r2' loaded,
                    // and the run restarted the function until the step limit). An integer in its place;
                    // the draws are the same, so every other program of a seed is unchanged.
                    w = codeInRegs && src < 4 ? uint16_t(0xE000 | reg << 8 | (0x20 + src))
                                              : uint16_t(0x6003 | reg << 8 | src << 4);   // mov Rm,Rk
                }
            }
            else if (r >= 70 && !saves.empty())
                w = uint16_t(0x6003 | reg << 8 | saves[rng.Below(static_cast<uint32_t>(saves.size()))].second << 4);   // mov Rsaved,Rk
            args.push_back(w);
            def |= 1u << reg;
        }
        if (!args.empty() && rng.Chance(50))
        {
            it.slotWords = { args.back() };
            args.pop_back();
        }
        it.setup = args;
        // The generator's own contract (A4-9, A4-15): an argument is never a code address or a constant that the callee (or what it
        // calls) stores or loads through. The jsr's target register is loaded first, so an argument copied from it is the
        // callee's address (seed 301364: written through, it overwrote the caller's next literal); a small constant is no
        // address at all (seed 305201). Both are replaced by the caller's own copy of the argument register it was entered
        // with, a pointer into scratch memory, without a draw: no other program of a seed changes.
        std::set<int> seen;
        const uint32_t derefs = PointerArgs(callee, fns, seen);
        auto safeArg = [&](uint16_t w) {
            const int dst = (w >> 8) & 0xF;
            return saves.empty() ? w : uint16_t(0x6003 | dst << 8 | saves[0].second << 4);   // mov Rsaved,Rdst
        };
        auto fixArgs = [&](std::vector<uint16_t>& words, int jsrReg) {
            for (uint16_t& w : words)
            {
                const int dst = (w >> 8) & 0xF;
                const bool constant = (w & 0xF000) == 0xE000 && dst >= 4 && dst < 8 && ((derefs >> dst) & 1u);
                const bool target = jsrReg >= 0 && (w & 0xF00F) == 0x6003 && static_cast<int>((w >> 4) & 0xF) == jsrReg;
                // A4-15: a scratch register this function reads for the value its caller left (r1-r3) is, to a caller that called it
                // through jsr, that caller's target register: the callee's address (seed 8306: the callee passed it on, a callee of
                // its own stored through it, over the literal the caller's tail jump loaded). Not passed to a callee that dereferences.
                const bool left = (w & 0xF00F) == 0x6003 && ((leftScratch >> ((w >> 4) & 0xF)) & 1u) && dst >= 4 && dst < 8 && ((derefs >> dst) & 1u);
                if (constant || target || left) w = safeArg(w);
            }
        };
        if (it.viaJsr)
        {
            it.jsrReg = 1 + static_cast<int>(rng.Below(3));
            Lit l;
            l.kind = LitKind::Fn;
            l.fn = callee;
            it.fnLit = static_cast<int>(p.lits.size());
            p.lits.push_back(l);
        }
        fixArgs(it.setup, it.viaJsr ? it.jsrReg : -1);
        fixArgs(it.slotWords, it.viaJsr ? it.jsrReg : -1);
        for (const std::pair<int, int>& s : saves) it.after.push_back(uint16_t(0x6003 | s.first << 8 | s.second << 4));
        // A callee that leaves a register changed (a convention of its own): read right after the call, into
        // a scratch register, which later items may use or return. A caller that took the register to be
        // kept would print the value it had before the call (ledger A3-3).
        int observed = 0;
        if (convModule && fns[callee].effClobbers && convRng.Chance(75))
        {
            std::vector<int> bits;
            for (int b = 0; b < 32; ++b)
                if ((fns[callee].effClobbers >> b) & 1u) bits.push_back(b);
            const int b = bits[convRng.Below(static_cast<uint32_t>(bits.size()))];
            observed = 1 + static_cast<int>(convRng.Below(3));
            it.after.push_back(ReadRegisterInto(b, observed));
            it.observes = true;
        }
        // After the call only what was restored, and r0 if the callee returns, holds a value.
        defined &= ~0xFFu;
        for (const std::pair<int, int>& s : saves) defined |= 1u << s.first;
        if (fns[callee].returns) defined |= 1u;
        if (observed) defined |= 1u << observed;
        return it;
    }

    // A switch through mova + shll2 + mov.l @(r0,Rn) + jmp @Rn with a bound check.
    Item MakeTable(int idx)
    {
        Item it;
        it.type = Item::Table;
        it.shape = "table";
        it.sel = selReg;
        const int n = 3 + static_cast<int>(rng.Below(4));
        it.useHs = rng.Chance(50);
        int cases = 0;
        for (int i = 0; i < n; ++i)
        {
            const uint32_t r = rng.Below(100);
            if (r < 15) it.target.push_back(-1);
            else if (i > 0 && r < 40 && it.target[i - 1] >= 0) it.target.push_back(it.target[i - 1]);
            else it.target.push_back(cases++);
        }
        if (cases == 0)
        {
            it.target[0] = 0;
            cases = 1;
        }
        // A signed bound check lets the negative indices through, so it is made only where the harness alone
        // chooses the index: the first function of a module is entered by nothing else (a callee gets whatever
        // its caller left in the register, a negative number among them, and would jump anywhere).
        if (guardRng.Chance(25) && idx == 0)
        {
            it.signedGuard = true;
            it.preCase = static_cast<int>(guardRng.Below(static_cast<uint32_t>(cases)));
        }
        const uint32_t before = defined;
        for (int c = 0; c < cases; ++c)
        {
            std::vector<Item> body;
            const int kids = 1 + static_cast<int>(rng.Below(2));
            for (int i = 0; i < kids; ++i) body.push_back(MakeSimple(-1, false, true));
            it.cases.push_back(body);
            defined = before;
        }
        const int kids = static_cast<int>(rng.Below(3));
        for (int i = 0; i < kids; ++i) it.kids.push_back(MakeSimple(-1, false, true));
        defined = before | 2u;   // r1 holds the bound on every path
        return it;
    }

    Item MakeDataSkip()
    {
        Item it;
        it.type = Item::DataSkip;
        it.shape = "bra.data";
        const int n = 1 + static_cast<int>(rng.Below(4));
        for (int i = 0; i < n; ++i)
        {
            const uint32_t r = rng.Below(100);
            it.data.push_back(r < 20 ? 0x000B : r < 35 ? 0xFFFF : r < 50 ? 0x0009 : static_cast<uint16_t>(rng.Next() & 0xFFFF));
        }
        return it;
    }

    std::vector<std::pair<int, int>> saves;   // (argument register, callee-saved copy) of the function being made

    // One function. 'ends' says how every function of the module ends; later functions exist already.
    Func MakeFunc(int idx, int nFn, const std::vector<int>& ends, const std::vector<Func>& fns)
    {
        Func f;
        f.v2 = true;
        wideClobbers = wideReads = 0;
        f.end = static_cast<Func::End>(ends[idx]);
        const bool falls = f.end == Func::Fall;
        if (f.end == Func::TailJump) f.tailTo = idx + 1 + static_cast<int>(rng.Below(static_cast<uint32_t>(nFn - idx - 1)));
        if (falls) f.returns = fns[idx + 1].returns;
        else if (f.end == Func::TailJump) f.returns = fns[f.tailTo].returns;
        else f.returns = rng.Chance(55);
        f.noR0 = !f.returns;
        const bool table = !f.noR0 && rng.Chance(25);   // mova writes r0
        int nCalls = 0;
        if (!falls && idx < nFn - 1 && rng.Chance(65)) nCalls = 1 + static_cast<int>(rng.Below(2));
        f.savePr = !falls && (nCalls > 0 || (f.end == Func::TailJump && rng.Chance(50)));
        if (f.savePr && rng.Chance(30))
        {
            f.savePr = false;
            f.prReg = convModule && clobberReg == 12 ? 13 : 12;   // never the register this module's functions change
        }
        int nsave = nCalls > 0 ? 2 + static_cast<int>(rng.Below(3)) : 0;
        if (table)
        {
            f.sel = 4 + static_cast<int>(rng.Below(4));
            if (nCalls > 0) nsave = std::max(nsave, f.sel - 3);   // the index survives the calls
        }
        saves.clear();
        for (int k = 0; k < nsave; ++k)
        {
            f.pushed.push_back(8 + k);
            f.argSave.push_back(uint16_t(0x6003 | (8 + k) << 8 | (4 + k) << 4));
            saves.push_back(std::make_pair(4 + k, 8 + k));
        }
        if (f.prReg >= 0) f.pushed.push_back(f.prReg);
        if (!falls && rng.Chance(55))
        {
            static const int kFrames[] = { 4, 8, 12, 16, 24, 32, 48, 64 };
            f.frame = kFrames[rng.Below(8)];
            f.fp = rng.Chance(30) && !(convModule && clobberReg == 14);
            if (f.fp) f.pushed.push_back(14);
        }
        if (!falls && rng.Chance(15) && !(convModule && clobberReg == 13)) f.pushed.push_back(13);
        std::sort(f.pushed.begin(), f.pushed.end());
        f.pushed.erase(std::unique(f.pushed.begin(), f.pushed.end()), f.pushed.end());
        f.shared = !falls && rng.Chance(22);
        f.epilogueBelow = !falls && rng.Chance(25) && (idx == 0 || ends[idx - 1] != Func::Fall);

        // An input the compiler would not pass (a hand-written routine's convention, ledger A3-5): a scratch
        // register that holds a value the caller left (r1-r3), or a register read for its value on entry
        // before anything writes it -- r8-r14 that the function does not save, GBR, VBR, MACH, MACL -- into a
        // scratch register the later items may use. Drawn from the stream of its own.
        uint32_t convDefined = 0;
        leftScratch = 0;
        Item convRead;
        bool hasConvRead = false;
        if (convModule && convRng.Chance(45))
        {
            const uint32_t r = convRng.Below(100);
            if (r < 30)
            {
                const int k = 1 + static_cast<int>(convRng.Below(3));
                convDefined = 1u << k;
                leftScratch = 1u << k;
                f.reads |= 1u << k;
            }
            else
            {
                int bit = -1;
                if (r < 60)
                {
                    int pool[7], n = 0;
                    for (int g = 8; g <= 14; ++g)
                        if (std::find(f.pushed.begin(), f.pushed.end(), g) == f.pushed.end()) pool[n++] = g;
                    if (n) bit = pool[convRng.Below(static_cast<uint32_t>(n))];
                }
                else bit = 17 + static_cast<int>(convRng.Below(4));
                if (bit >= 0)
                {
                    const int d = 1 + static_cast<int>(convRng.Below(3));
                    convRead.type = Item::Alu;
                    convRead.shape = "conv.read";
                    convRead.words = { ReadRegisterInto(bit, d) };
                    hasConvRead = true;
                    convDefined = 1u << d;
                    f.reads |= 1u << bit;
                }
            }
        }
        disc = true;
        noR0 = f.noR0;
        codeInRegs = table;
        selReg = f.sel;
        frame = f.frame;
        fp = f.fp;
        inSpill = false;
        defined = 0xF0u | convDefined;
        for (int i = 0; i < 8; ++i) align[i] = 4, pref[i] = 4;

        std::vector<char> plan;
        const int n = 2 + static_cast<int>(rng.Below(6));
        // alu, mem, stack, litptr, skip, loop, bra-loop, back-loop, spill, data
        const uint32_t weights[10] = { 15, 22, f.frame ? 14u : 0u, 8, 13, 5, 4, 4, 7, 4 };
        const char kinds[10] = { 'a', 'm', 's', 'l', 'k', 'o', 'b', 'B', 'p', 'd' };
        uint32_t total = 0;
        for (uint32_t w : weights) total += w;
        for (int i = 0; i < n; ++i)
        {
            uint32_t r = rng.Below(total);
            int k = 0;
            while (r >= weights[k]) r -= weights[k++];
            plan.push_back(kinds[k]);
        }
        for (int i = 0; i < nCalls; ++i) plan.insert(plan.begin() + rng.Below(static_cast<uint32_t>(plan.size() + 1)), 'C');
        if (table) plan.insert(plan.begin() + rng.Below(static_cast<uint32_t>(plan.size() + 1)), 'T');
        // A walk of a frame address down the frame, by a loop (A3-16): drawn from a stream of its own, so no other draw moves.
        if (f.frame && frameRng.Chance(20)) plan.insert(plan.begin() + frameRng.Below(static_cast<uint32_t>(plan.size() + 1)), 'F');

        for (char c : plan)
        {
            switch (c)
            {
            case 'a': f.items.push_back(MakeAlu(-1)); break;
            case 'm': f.items.push_back(MakeMem(-1, false)); break;
            case 's': f.items.push_back(MakeStackMem(-1)); break;
            case 'l': f.items.push_back(MakeLitPtr(6 + static_cast<int>(rng.Below(2)), PickKind2())); break;
            case 'k': f.items.push_back(MakeSkip()); break;
            case 'o': f.items.push_back(MakeLoop()); break;
            case 'b':
            {
                Item it = MakeLoop();
                it.type = Item::BraLoop;
                it.shape = "loop.bra";
                f.items.push_back(it);
                break;
            }
            case 'B':
            {
                Item it = MakeLoop();
                it.type = Item::BraBack;
                it.shape = "loop.back";
                f.items.push_back(it);
                break;
            }
            case 'p': f.items.push_back(MakeSpill()); break;
            case 'd': f.items.push_back(MakeDataSkip()); break;
            case 'T': f.items.push_back(MakeTable(idx)); break;
            case 'F': f.items.push_back(MakeFrameWalk()); break;
            default:
            {
                const int callee = idx + 1 + static_cast<int>(rng.Below(static_cast<uint32_t>(nFn - idx - 1)));
                if (rng.Chance(25))
                {
                    // A conditional call: the skipped part of a branch.
                    Item sk = MakeSkip();
                    const uint32_t before = defined;
                    Item call = MakeCall(callee, fns);
                    uint32_t restored = 0;
                    for (const std::pair<int, int>& s : saves) restored |= 1u << s.first;
                    defined = (before & ~0xFFu) | (before & restored);
                    sk.kids.clear();
                    sk.kids.push_back(call);
                    f.items.push_back(sk);
                }
                else f.items.push_back(MakeCall(callee, fns));
                break;
            }
            }
        }

        // Registers this function changes for good: the module's r12-r14 (no function of it uses that for
        // anything else, so the others' saves and frames do not meet it), GBR, VBR, MACH, MACL. Written from
        // r4-r7 or a constant, at a place among the items that every path passes.
        if (convModule && convRng.Chance(55))
        {
            static const struct { uint16_t op; int bit; } kState[] = { { 0x401E, 17 }, { 0x402E, 18 }, { 0x400A, 19 }, { 0x401A, 20 } };
            const int count = 1 + static_cast<int>(convRng.Below(2));
            for (int i = 0; i < count; ++i)
            {
                Item c;
                c.type = Item::Alu;
                const int src = 4 + static_cast<int>(convRng.Below(4));
                if (convRng.Chance(45))
                {
                    c.shape = "conv.clobber.gpr";
                    c.words = { convRng.Chance(50) ? uint16_t(0xE000 | clobberReg << 8 | convRng.Below(256))   // mov #imm,Rn
                                                   : uint16_t(0x6003 | clobberReg << 8 | src << 4) };          // mov Rm,Rn
                    f.clobbers |= 1u << clobberReg;
                }
                else
                {
                    const auto& st = kState[convRng.Below(4)];
                    c.shape = "conv.clobber.state";
                    c.words = { uint16_t(st.op | src << 8) };   // ldc Rm,gbr / ldc Rm,vbr / lds Rm,mach / lds Rm,macl
                    f.clobbers |= 1u << st.bit;
                }
                f.items.insert(f.items.begin() + convRng.Below(static_cast<uint32_t>(f.items.size() + 1)), c);
            }
        }
        if (hasConvRead) f.items.insert(f.items.begin(), convRead);   // first: before anything can write what it reads
        f.effClobbers = f.clobbers;
        {
            std::vector<int> callees;
            CalleesOf(f.items, callees);
            if (falls) callees.push_back(idx + 1);
            if (f.end == Func::TailJump) callees.push_back(f.tailTo);
            for (int c : callees) f.effClobbers |= fns[c].effClobbers;
        }

        if (f.shared)
        {
            const uint32_t before = defined;
            int a = PickSrc(static_cast<int>(rng.Below(8)), 7), b = PickSrc(static_cast<int>(rng.Below(8)), 7);
            if (a < 0 || b < 0) f.sharedCond = 0x0018;   // sett
            else f.sharedCond = rng.Chance(60) ? uint16_t(0x2008 | a << 8 | b << 4) : uint16_t(0x4015 | a << 8);   // tst Rm,Rn / cmp/pl Rn
            f.sharedOnTrue = rng.Chance(50);
            const int na = static_cast<int>(rng.Below(3)), nb = static_cast<int>(rng.Below(3));
            for (int i = 0; i < na; ++i) f.pathA.push_back(MakeSimple(-1, false, true));
            defined = before;
            for (int i = 0; i < nb; ++i) f.pathB.push_back(MakeSimple(-1, false, true));
            defined = before;
            if (rng.Chance(35)) f.slotA = { uint16_t(0xE000 | (1 + rng.Below(3)) << 8 | rng.Below(256)) };
            if (rng.Chance(35)) f.slotB = { uint16_t(0xE000 | (1 + rng.Below(3)) << 8 | rng.Below(256)) };
        }
        // The return value: the last write of r0 on every path.
        if (f.returns && f.end == Func::Rts)
        {
            const uint32_t r = rng.Below(100);
            f.retWord = uint16_t(0xE000 | rng.Below(256));   // mov #imm,r0
            if (r >= 30 && r < 65)
            {
                const int s = PickSrcNotR0(1 + static_cast<int>(rng.Below(7)));
                if (s >= 0) f.retWord = uint16_t(0x6003 | s << 4);   // mov Rm,r0
            }
            else if (r >= 65 && r < 85)
            {
                int pool[4], k = 0;
                for (int q = 4; q < 8; ++q)
                    if (Def(q) && q != selReg) pool[k++] = q;
                if (k && !gTrackAlignment)
                    f.retWord = uint16_t(0x6000 | pool[rng.Below(static_cast<uint32_t>(k))] << 4 | rng.Below(3));   // as before A11-2, draw order and all
                else if (k)
                {
                    const int q = pool[rng.Below(static_cast<uint32_t>(k))];
                    uint32_t sz = rng.Below(3);   // byte, word or longword
                    // ...of the width the pointer is still aligned for (an increment or decrement earlier in the function, or a
                    // literal's address, may have left it at 2 or 1): a misaligned read is an address error (A11-2)
                    if (gTrackAlignment) sz = std::min<uint32_t>(sz, align[q] >= 4 ? 2u : align[q] >= 2 ? 1u : 0u);
                    f.retWord = uint16_t(0x6000 | q << 4 | sz);   // mov.x @Rm,r0
                }
            }
            else if (r >= 85)
            {
                const int s = PickSrcNotR0(1 + static_cast<int>(rng.Below(7)));
                if (s >= 0) f.retWord = uint16_t(0x600C | s << 4 | rng.Below(2));   // extu.b / extu.w Rm,r0
            }
            f.retInSlot = rng.Chance(40);
        }
        if (f.end != Func::Fall && !f.retInSlot) f.slotPop = rng.Chance(45);
        if (f.end == Func::TailJump)
        {
            f.tailReg = 1 + static_cast<int>(rng.Below(3));
            Lit l;
            l.kind = LitKind::Fn;
            l.fn = f.tailTo;
            f.tailLit = static_cast<int>(p.lits.size());
            p.lits.push_back(l);
        }
        // What the wide alphabet made it do to the state registers (see StateWritten): it changes them for good
        // like a convention of the grammar's, and so do the functions that call or run on into it.
        f.clobbers |= wideClobbers;
        f.effClobbers |= wideClobbers;
        f.reads |= wideReads;
        disc = false;
        return f;
    }

    // A module of one to three functions.
    Program Module()
    {
        p.version = 2;
        convModule = convRng.Chance(35);
        clobberReg = 12 + static_cast<int>(convRng.Below(3));
        const uint32_t r = rng.Below(100);
        const int nFn = r < 30 ? 1 : r < 75 ? 2 : 3;
        std::vector<int> ends(nFn, Func::Rts);
        for (int i = 0; i + 1 < nFn; ++i)
        {
            const uint32_t e = rng.Below(100);
            ends[i] = e < 12 ? Func::Fall : e < 32 ? Func::TailJump : Func::Rts;
        }
        std::vector<Func> fns(nFn);
        for (int i = nFn - 1; i >= 0; --i) fns[i] = MakeFunc(i, nFn, ends, fns);
        p.fns = fns;
        return p;
    }

    Program Run()
    {
        p.fns.resize(1);
        Func& f = p.fns[0];
        std::vector<Item>& items = f.items;
        items.push_back(MakeLitPtr(6, PickKind()));
        if (rng.Chance(70)) items.push_back(MakeLitPtr(7, PickKind()));
        const int n = 3 + static_cast<int>(rng.Below(12));
        bool polled = false;
        for (int i = 0; i < n; ++i)
        {
            const uint32_t r = rng.Below(100);
            if (r < 22) items.push_back(MakeAlu(-1));
            else if (r < 58) items.push_back(MakeMem(-1, false));
            else if (r < 64) items.push_back(MakeLitPtr(6 + static_cast<int>(rng.Below(2)), PickKind()));
            else if (r < 80) items.push_back(MakeSkip());
            else if (r < 87) items.push_back(MakeLoop());
            else if (!polled)
            {
                items.push_back(MakePoll());
                polled = true;
            }
            else items.push_back(MakeMem(-1, false));
        }
        if (rng.Chance(30)) f.tail.push_back(MakeSimple(-1, true, false));
        // mac, in one program of eight; never ahead of the pointer registers' literals
        if (macRng.Chance(12))
            items.insert(items.begin() + 1 + macRng.Below(static_cast<uint32_t>(items.size())), MakeMac());
        return p;
    }
};

Program Generate(uint64_t seed)
{
    Gen g(seed);
    return g.Run();
}

// A version 2 module. 'attempt' re-draws it when the layout does not fit (a pool out of reach).
Program Generate2(uint64_t seed, int attempt)
{
    Gen g(seed ^ (static_cast<uint64_t>(attempt) << 40));
    return g.Module();
}

// ---- Assembly -------------------------------------------------------------------------------

// Where a jump table's dispatch is and where each of its entries goes: what the checks compare the
// run's destinations and the printed cases with.
struct TableLayout
{
    uint32_t jmpInsn = 0;          // the jmp @Rn that dispatches
    int sel = 4;                   // the register that indexes (an integer, set by the harness)
    int n = 0;                     // entries
    std::vector<uint32_t> entry;   // for each index, where it goes (the default's address for the ones that go there)
    uint32_t defaultAddr = 0;
    // The code of each destination, [lo, hi): its case body up to the bra to the join (the default's
    // up to the join itself).
    std::vector<std::pair<uint32_t, uint32_t>> range;
    std::pair<uint32_t, uint32_t> defaultRange;
    // A signed guard lets index -1 through: the word before the table is an entry of its own.
    bool hasPre = false;
    uint32_t preEntry = 0;
    std::pair<uint32_t, uint32_t> preRange;
};

struct UnitLayout
{
    uint32_t start = 0;            // the first word of the function's code (its exit may sit before the entry)
    uint32_t entry = 0;
    uint32_t end = 0;              // one past the last word of code, before the literal pool
    std::vector<std::pair<uint32_t, uint32_t>> data;   // words embedded in the code, [lo, hi)
    std::vector<TableLayout> tables;
    bool falls = false;            // the code runs on into the next function
};

struct Assembled
{
    std::vector<uint16_t> words;   // from kCode: the code, any padding, the literal pools
    size_t codeWords = 0;          // version 1: the words before the pool
    std::vector<uint32_t> litAddr; // per program literal; 0 when unused
    std::vector<UnitLayout> units;
    bool ok = true;                // false: a branch or a pool out of reach
};

struct Emitter
{
    struct W
    {
        uint16_t w = 0;
        // 1: bt/bf (8-bit) to 'label'; 2: pool load of literal 'lit'; 3: bra/bsr (12-bit) to 'label';
        // 4: mova to 'label'; 5, 6: high, low half of a label's address; 7, 8: high, low half of a literal's value
        int fix = 0;
        int label = 0, lit = 0;
    };
    struct Table
    {
        size_t jmpIndex = 0;
        int sel = 0;
        std::vector<int> entryLabels;
        std::vector<int> endLabels;      // per entry: the end of its case body
        int defaultLabel = 0, defaultEnd = 0;
        int preLabel = -1, preEnd = -1;  // a signed guard: the case the word before the table goes to
    };
    const Program& p;
    std::vector<W> w;
    std::vector<int> labelAt;
    std::vector<int> pending;         // literals loaded since the last pool, once each, in order
    std::vector<int> litWord;         // per literal: the word index of its pool entry, -1 if none yet
    std::vector<int> entryLabel, startLabel;
    std::vector<Table> tables;
    std::vector<std::vector<size_t>> tablesOf;           // per unit: indices into 'tables'
    std::vector<std::vector<std::pair<size_t, size_t>>> dataOf;   // per unit: embedded data, word indices
    int unit = 0;

    explicit Emitter(const Program& prog) : p(prog)
    {
        litWord.assign(p.lits.size(), -1);
        for (size_t i = 0; i < p.fns.size(); ++i)
        {
            entryLabel.push_back(NewLabel());
            startLabel.push_back(NewLabel());
        }
        tablesOf.resize(p.fns.size());
        dataOf.resize(p.fns.size());
    }
    int NewLabel() { labelAt.push_back(-1); return static_cast<int>(labelAt.size()) - 1; }
    void Place(int label) { labelAt[label] = static_cast<int>(w.size()); }
    void Put(uint16_t word)
    {
        W x;
        x.w = word;
        w.push_back(x);
    }
    void PutFix(uint16_t opcode, int fix, int label, int lit)
    {
        W x;
        x.w = opcode;
        x.fix = fix;
        x.label = label;
        x.lit = lit;
        w.push_back(x);
    }
    void PutBranch(uint16_t opcode, int label) { PutFix(opcode, 1, label, 0); }
    void PutBranch12(uint16_t opcode, int label) { PutFix(opcode, 3, label, 0); }
    void PutLitLoad(int reg, int lit)
    {
        PutFix(static_cast<uint16_t>(0xD000 | reg << 8), 2, 0, lit);
        if (litWord[lit] < 0 && std::find(pending.begin(), pending.end(), lit) == pending.end()) pending.push_back(lit);
    }
    void Align4()
    {
        if (w.size() & 1) Put(0x0009);
    }
    // The pool: the literals loaded since the last one, 4-aligned.
    void FlushPool()
    {
        if (pending.empty()) return;
        Align4();
        for (int lit : pending)
        {
            litWord[lit] = static_cast<int>(w.size());
            PutFix(0, 7, 0, lit);
            PutFix(0, 8, 0, lit);
        }
        pending.clear();
    }
    void EmitList(const std::vector<Item>& items)
    {
        for (const Item& it : items) Emit(it);
    }

    // The stack instructions of the exit: release the frame, restore PR, pop the registers.
    struct ExitOps
    {
        std::vector<uint16_t> ops;
        bool lastMovable = false;   // the last one may sit in the delay slot (everything but the PR restore)
    };
    static ExitOps ExitSequence(const Func& f)
    {
        ExitOps e;
        if (f.fp)
        {
            e.ops.push_back(static_cast<uint16_t>(0x7E00 | f.frame));              // add #frame,r14
            e.ops.push_back(0x6FE3);                                               // mov r14,r15
        }
        else if (f.frame) e.ops.push_back(static_cast<uint16_t>(0x7F00 | f.frame));   // add #frame,r15
        int prAt = -1;
        if (f.savePr)
        {
            prAt = static_cast<int>(e.ops.size());
            e.ops.push_back(0x4F26);                                               // lds.l @r15+,pr
        }
        else if (f.prReg >= 0)
        {
            prAt = static_cast<int>(e.ops.size());
            e.ops.push_back(static_cast<uint16_t>(0x402A | f.prReg << 8));         // lds Rn,pr
        }
        for (size_t i = f.pushed.size(); i-- > 0;) e.ops.push_back(static_cast<uint16_t>(0x60F6 | f.pushed[i] << 8));   // mov.l @r15+,Rn
        e.lastMovable = !e.ops.empty() && static_cast<int>(e.ops.size()) - 1 != prAt;
        return e;
    }
    void EmitExit(const Func& f)
    {
        if (f.end == Func::TailJump) PutLitLoad(f.tailReg, f.tailLit);
        if (f.returns && f.end == Func::Rts && !f.retInSlot) Put(f.retWord);
        const ExitOps e = ExitSequence(f);
        const bool slotOp = f.slotPop && e.lastMovable && !f.retInSlot;
        for (size_t i = 0; i < e.ops.size(); ++i)
            if (!(slotOp && i + 1 == e.ops.size())) Put(e.ops[i]);
        const uint16_t slot = f.retInSlot ? f.retWord : slotOp ? e.ops.back() : uint16_t(0x0009);
        if (f.end == Func::Rts)
        {
            Put(0x000B);   // rts
            Put(slot);
        }
        else if (f.end == Func::TailJump)
        {
            Put(static_cast<uint16_t>(0x402B | f.tailReg << 8));   // jmp @Rn
            Put(slot);
        }
    }

    void EmitFunc(int i)
    {
        const Func& f = p.fns[i];
        unit = i;
        Place(startLabel[i]);
        if (!f.v2)
        {
            Place(entryLabel[i]);
            EmitList(f.items);
            Put(0x000B);   // rts
            if (f.tail.empty()) Put(0x0009);
            else EmitList(f.tail);
            return;
        }
        const int exitLabel = NewLabel();
        if (f.epilogueBelow)
        {
            Place(exitLabel);
            EmitExit(f);
        }
        Place(entryLabel[i]);
        for (int r : f.pushed) Put(static_cast<uint16_t>(0x2F06 | r << 4));             // mov.l Rn,@-r15
        if (f.savePr) Put(0x4F22);                                                      // sts.l pr,@-r15
        else if (f.prReg >= 0) Put(static_cast<uint16_t>(0x002A | f.prReg << 8));       // sts pr,Rn
        if (f.frame) Put(static_cast<uint16_t>(0x7F00 | ((-f.frame) & 0xFF)));          // add #-frame,r15
        if (f.fp) Put(0x6EF3);                                                          // mov r15,r14
        for (uint16_t a : f.argSave) Put(a);
        EmitList(f.items);
        if (f.shared)
        {
            const int second = NewLabel();
            Put(f.sharedCond);
            PutBranch(f.sharedOnTrue ? 0x8900 : 0x8B00, second);
            EmitList(f.pathA);
            PutBranch12(0xA000, exitLabel);                                    // bra exit
            Put(f.slotA.empty() ? uint16_t(0x0009) : f.slotA[0]);
            Place(second);
            EmitList(f.pathB);
            PutBranch12(0xA000, exitLabel);                                    // bra exit
            Put(f.slotB.empty() ? uint16_t(0x0009) : f.slotB[0]);
            if (!f.epilogueBelow)
            {
                Place(exitLabel);
                EmitExit(f);
            }
        }
        else if (f.end != Func::Fall)
        {
            if (f.epilogueBelow)
            {
                PutBranch12(0xA000, exitLabel);
                Put(0x0009);
            }
            else EmitExit(f);
        }
    }

    void Emit(const Item& it)
    {
        switch (it.type)
        {
        case Item::Alu:
        case Item::Mem:
            for (uint16_t x : it.words) Put(x);
            break;
        case Item::LitPtr:
            PutLitLoad(it.reg, it.lit);
            break;
        case Item::Skip:
        {
            const int skip = NewLabel();
            Put(it.cond);
            PutBranch(static_cast<uint16_t>((it.onTrue ? 0x8900 : 0x8B00) | (it.delayed ? 0x0400 : 0)), skip);
            if (it.delayed) EmitList(it.slot);
            EmitList(it.kids);
            Place(skip);
            break;
        }
        case Item::Loop:
        {
            const int top = NewLabel();
            Put(static_cast<uint16_t>(0xE000 | it.counter << 8 | it.count));
            Place(top);
            EmitList(it.kids);
            Put(static_cast<uint16_t>(0x4010 | it.counter << 8));   // dt
            PutBranch(0x8B00, top);                                   // bf
            break;
        }
        case Item::Poll:
        {
            const int sz = it.width == 1 ? 0 : it.width == 2 ? 1 : 2;
            PutLitLoad(it.reg, it.lit);
            if (it.pollWrite)
            {
                Put(static_cast<uint16_t>(0xE000 | it.pollValue));
                Put(static_cast<uint16_t>(0x2000 | it.reg << 8 | sz));   // mov.x r0,@r7
            }
            const int top = NewLabel();
            Place(top);
            Put(static_cast<uint16_t>(0x6000 | it.reg << 4 | sz));       // mov.x @r7,r0
            Put(0xC801);                                                                  // tst #1,r0
            PutBranch(it.untilSet ? 0x8900 : 0x8B00, top);                                // bt (until set) / bf
            break;
        }
        case Item::Spill:
            Put(static_cast<uint16_t>(0x2F06 | it.spillReg << 4));        // mov.l Rn,@-r15
            EmitList(it.kids);
            Put(static_cast<uint16_t>(0x60F6 | it.spillReg << 8));        // mov.l @r15+,Rn
            break;
        case Item::Call:
        {
            for (uint16_t x : it.setup) Put(x);
            if (it.viaJsr)
            {
                PutLitLoad(it.jsrReg, it.fnLit);
                Put(static_cast<uint16_t>(0x400B | it.jsrReg << 8));      // jsr @Rn
            }
            else PutBranch12(0xB000, entryLabel[it.callee]);               // bsr
            Put(it.slotWords.empty() ? uint16_t(0x0009) : it.slotWords[0]);
            for (uint16_t x : it.after) Put(x);
            break;
        }
        case Item::Table:
        {
            const int n = static_cast<int>(it.target.size());
            const int def = NewLabel(), join = NewLabel(), tbl = NewLabel();
            std::vector<int> caseLabel, caseEnd;
            for (size_t c = 0; c < it.cases.size(); ++c)
            {
                caseLabel.push_back(NewLabel());
                caseEnd.push_back(NewLabel());
            }
            Put(static_cast<uint16_t>(0xE100 | (it.useHs ? n : n - 1)));                 // mov #bound,r1
            // cmp/hs r1,Rn / cmp/hi r1,Rn, or the signed cmp/ge r1,Rn / cmp/gt r1,Rn
            Put(static_cast<uint16_t>((it.signedGuard ? (it.useHs ? 0x3013 : 0x3017) : (it.useHs ? 0x3012 : 0x3016)) | it.sel << 8));
            PutBranch(0x8900, def);                                                       // bt default
            Put(static_cast<uint16_t>(0x4008 | it.sel << 8));                             // shll2 Rn
            PutFix(0xC700, 4, tbl, 0);                                                    // mova @(table,pc),r0
            Put(static_cast<uint16_t>(0x010E | it.sel << 4));                             // mov.l @(r0,Rn),r1
            Table t;
            t.jmpIndex = w.size();
            t.sel = it.sel;
            t.defaultLabel = def;
            Put(0x412B);                                                                  // jmp @r1
            Put(0x0009);
            Align4();
            if (it.signedGuard)
            {
                // Index -1 passes the guard and reads the word before the table: an entry of its own, to a case.
                const int c = it.preCase % static_cast<int>(it.cases.size());
                t.preLabel = caseLabel[c];
                t.preEnd = caseEnd[c];
                PutFix(0, 5, caseLabel[c], 0);
                PutFix(0, 6, caseLabel[c], 0);
            }
            Place(tbl);
            for (int e : it.target)
            {
                const int target = e < 0 ? def : caseLabel[e];
                t.entryLabels.push_back(target);
                t.endLabels.push_back(e < 0 ? join : caseEnd[e]);
                PutFix(0, 5, target, 0);
                PutFix(0, 6, target, 0);
            }
            for (size_t c = 0; c < it.cases.size(); ++c)
            {
                Place(caseLabel[c]);
                EmitList(it.cases[c]);
                Place(caseEnd[c]);
                PutBranch12(0xA000, join);                                                // bra join
                Put(0x0009);
            }
            Place(def);
            EmitList(it.kids);
            Place(join);
            t.defaultEnd = join;
            tablesOf[unit].push_back(tables.size());
            tables.push_back(t);
            break;
        }
        case Item::BraLoop:
        {
            const int body = NewLabel(), test = NewLabel();
            Put(static_cast<uint16_t>(0xE000 | it.counter << 8 | it.count));
            PutBranch12(0xA000, test);                                // bra test
            Put(0x0009);
            Place(body);
            EmitList(it.kids);
            Place(test);
            Put(static_cast<uint16_t>(0x4010 | it.counter << 8));   // dt
            PutBranch(0x8B00, body);                                  // bf body
            break;
        }
        case Item::BraBack:
        {
            const int top = NewLabel(), out = NewLabel();
            Put(static_cast<uint16_t>(0xE000 | it.counter << 8 | it.count));
            Place(top);
            EmitList(it.kids);
            Put(static_cast<uint16_t>(0x4010 | it.counter << 8));   // dt
            PutBranch(0x8900, out);                                   // bt out
            PutBranch12(0xA000, top);                                 // bra top (backward)
            Put(0x0009);
            Place(out);
            break;
        }
        case Item::DataSkip:
        {
            const int over = NewLabel();
            PutBranch12(0xA000, over);                                // bra over
            Put(0x0009);
            const size_t lo = w.size();
            for (uint16_t d : it.data) Put(d);
            dataOf[unit].push_back(std::make_pair(lo, w.size()));
            Place(over);
            break;
        }
        }
    }
};

Assembled Assemble(const Program& p)
{
    Emitter e(p);
    Assembled a;
    std::vector<size_t> unitEnd(p.fns.size());
    for (size_t i = 0; i < p.fns.size(); ++i)
    {
        e.EmitFunc(static_cast<int>(i));
        unitEnd[i] = e.w.size();
        if (p.fns[i].end != Func::Fall) e.FlushPool();
    }
    e.FlushPool();
    const auto addrOf = [&](size_t word) { return kCode + 2 * static_cast<uint32_t>(word); };
    const auto labelAddr = [&](int label) { return addrOf(static_cast<size_t>(e.labelAt[label])); };
    const auto litValue = [&](int lit) {
        const Lit& l = p.lits[lit];
        return l.kind == LitKind::Fn ? labelAddr(e.entryLabel[l.fn]) : l.value;
    };
    a.codeWords = unitEnd.empty() ? 0 : unitEnd[0];
    a.litAddr.assign(p.lits.size(), 0);
    for (size_t l = 0; l < p.lits.size(); ++l)
        if (e.litWord[l] >= 0) a.litAddr[l] = addrOf(static_cast<size_t>(e.litWord[l]));
    for (size_t i = 0; i < e.w.size(); ++i)
    {
        Emitter::W& x = e.w[i];
        const uint32_t at = addrOf(i);
        switch (x.fix)
        {
        case 1:
        case 3:
        {
            const int disp = e.labelAt[x.label] - static_cast<int>(i) - 2;   // words from PC+4
            const int lim = x.fix == 1 ? 128 : 2048;
            if (disp < -lim || disp >= lim) a.ok = false;
            x.w = static_cast<uint16_t>((x.w & (x.fix == 1 ? 0xFF00 : 0xF000)) | (disp & (x.fix == 1 ? 0xFF : 0xFFF)));
            break;
        }
        case 2:
        {
            const uint32_t target = a.litAddr[x.lit];
            const int disp = target ? static_cast<int>((static_cast<int64_t>(target) - ((at & ~3u) + 4)) / 4) : -1;
            if (!target || disp < 0 || disp > 255 || (target & 3)) a.ok = false;
            x.w = static_cast<uint16_t>((x.w & 0xFF00) | (disp & 0xFF));
            break;
        }
        case 4:
        {
            const uint32_t target = labelAddr(x.label);
            const int disp = static_cast<int>((static_cast<int64_t>(target) - ((at & ~3u) + 4)) / 4);
            if (disp < 0 || disp > 255 || (target & 3)) a.ok = false;
            x.w = static_cast<uint16_t>((x.w & 0xFF00) | (disp & 0xFF));
            break;
        }
        case 5: x.w = static_cast<uint16_t>(labelAddr(x.label) >> 16); break;
        case 6: x.w = static_cast<uint16_t>(labelAddr(x.label)); break;
        case 7: x.w = static_cast<uint16_t>(litValue(x.lit) >> 16); break;
        case 8: x.w = static_cast<uint16_t>(litValue(x.lit)); break;
        default: break;
        }
        a.words.push_back(x.w);
    }
    for (size_t i = 0; i < p.fns.size(); ++i)
    {
        UnitLayout u;
        u.start = labelAddr(e.startLabel[i]);
        u.entry = labelAddr(e.entryLabel[i]);
        u.end = addrOf(unitEnd[i]);
        u.falls = p.fns[i].end == Func::Fall;
        for (const std::pair<size_t, size_t>& d : e.dataOf[i]) u.data.push_back(std::make_pair(addrOf(d.first), addrOf(d.second)));
        for (size_t ti : e.tablesOf[i])
        {
            const Emitter::Table& t = e.tables[ti];
            TableLayout tl;
            tl.jmpInsn = addrOf(t.jmpIndex);
            tl.sel = t.sel;
            tl.n = static_cast<int>(t.entryLabels.size());
            for (size_t k = 0; k < t.entryLabels.size(); ++k)
            {
                tl.entry.push_back(labelAddr(t.entryLabels[k]));
                tl.range.push_back(std::make_pair(labelAddr(t.entryLabels[k]), labelAddr(t.endLabels[k])));
            }
            tl.defaultAddr = labelAddr(t.defaultLabel);
            tl.defaultRange = std::make_pair(labelAddr(t.defaultLabel), labelAddr(t.defaultEnd));
            if (t.preLabel >= 0)
            {
                tl.hasPre = true;
                tl.preEntry = labelAddr(t.preLabel);
                tl.preRange = std::make_pair(labelAddr(t.preLabel), labelAddr(t.preEnd));
            }
            u.tables.push_back(tl);
        }
        a.units.push_back(u);
    }
    return a;
}

// ---- The emulator with a trace ---------------------------------------------------------------

struct TraceRec
{
    uint32_t insn;
    bool write;
    int size;
    uint32_t addr;
    uint64_t value = 0;   // what was read or written; not part of the identity
    int depth = 0;        // how many calls deep the instruction ran: 0 is the function itself, more is a callee
    int step = 0;         // which executed instruction of the run made it (not part of the identity): two accesses of
                          // one step are one instruction's operands (mac), the same access of two steps is a loop
    bool operator<(const TraceRec& o) const
    {
        return std::tie(insn, write, size, addr) < std::tie(o.insn, o.write, o.size, o.addr);
    }
    bool operator==(const TraceRec& o) const { return insn == o.insn && write == o.write && size == o.size && addr == o.addr; }
    bool operator!=(const TraceRec& o) const { return !(*this == o); }
};

// The machine's memory, as the code sees it: high work RAM (both partitions, every 1 MiB repeat)
// is real; every other address is "hardware", whose reads return a default and whose writes are
// dropped, all logged. A polled address reads busy a few times and then ready: another processor
// answering.
struct Env
{
    std::vector<uint8_t> ram;
    int hwMode = 0;   // what a hardware read returns: 0 zeros, 1 ones, 2 pseudo-random
    uint64_t hwState = 0x12345;
    struct Poll { uint32_t addr; uint32_t busy, ready; int reads; uint32_t raw; };   // addr canonical, raw as the code has it
    std::vector<Poll> polls;
    std::vector<TraceRec> trace;
    int depth = 0;    // set by the machine before each instruction
    int step = 0;     // ...and the instruction's position in the run
    static bool IsRam(uint32_t a)
    {
        const uint32_t top = a >> 24;
        return top == 0x06 || top == 0x07 || top == 0x26 || top == 0x27;
    }
    static uint32_t Canon(uint32_t a) { return a < 0xC0000000u ? (a & 0x0FFFFFFFu) : a; }

    uint64_t Read(uint32_t a, int size, uint32_t insn)
    {
        const uint64_t v = ReadValue(a, size);
        trace.push_back({ Canon(insn), false, size, a, v, depth, step });
        return v;
    }

    uint64_t ReadValue(uint32_t a, int size)
    {
        const uint64_t mask = size == 4 ? 0xFFFFFFFFull : (1ull << (8 * size)) - 1;
        for (Poll& p : polls)
            if (p.addr == Canon(a))
            {
                const uint32_t v = ++p.reads <= kPollBusyReads ? p.busy : p.ready;
                if (IsRam(a)) Store(a, size, p.ready);   // what the other processor left behind
                return v & mask;
            }
        if (IsRam(a))
        {
            uint64_t v = 0;
            for (int i = 0; i < size; ++i) v = (v << 8) | ram[(a + i) & 0xFFFFF];
            return v;
        }
        if (hwMode == 0) return 0;
        if (hwMode == 1) return mask;
        // Memory that is neither work RAM nor a register (the BIOS area, which the program reaches
        // through a pointer it has tested to be 0): a value of its own, by address. The hardware's
        // sequence is not advanced by a read of ordinary memory, or a run that makes one more such
        // read (a perturbed argument, A3-1) would see other values from the registers after it, and
        // behave differently for a reason the decompiler is right to leave out.
        if (!InHardwareRange(a))
        {
            uint64_t h = (static_cast<uint64_t>(a) + 0x9E3779B97F4A7C15ull) * 0xBF58476D1CE4E5B9ull;
            h ^= h >> 31;
            return ((h * 0x94D049BB133111EBull) >> 32) & mask;
        }
        hwState ^= hwState >> 12;
        hwState ^= hwState << 25;
        hwState ^= hwState >> 27;
        return ((hwState * 2685821657736338717ull) >> 32) & mask;
    }

    void Store(uint32_t a, int size, uint64_t v)
    {
        for (int i = size - 1; i >= 0; --i, v >>= 8) ram[(a + i) & 0xFFFFF] = static_cast<uint8_t>(v);
    }

    void Write(uint32_t a, int size, uint64_t v, uint32_t insn)
    {
        trace.push_back({ Canon(insn), true, size, a, v, depth, step });
        if (IsRam(a)) Store(a, size, v);
    }
};

// Which of r0-r15 (bits 0-15), SR, GBR, VBR, MACH and MACL (16-20) a run read before it had written them,
// and which it wrote at all: the registers are 4 bytes each in the register space, and a read of any byte
// of one counts. (SR is read by every compare, which updates T under a mask: it is not asked about.)
struct RegUse
{
    uint32_t readFirst = 0;   // bit n: Rn was read before the run wrote the bytes it read
    uint32_t written = 0;     // bit n: Rn was written
};

// LOAD and STORE are where the memory accesses happen; hooking them gives the exact width and the
// p-code op's instruction (a delay slot's own address, as the decompiler's ops carry), which a
// MemoryBank cannot: it sees aligned words. Every op's register inputs and output are noted too.
class TracingEmulate : public ghidra::EmulatePcodeCache
{
public:
    TracingEmulate(ghidra::Translate* t, ghidra::MemoryState* s, ghidra::BreakTable* b, Env* env)
        : ghidra::EmulatePcodeCache(t, s, b), mEnv(env)
    {
        mRegSpace = t->getSpaceByName("register");
        for (int i = 0; i < 16; ++i) mRegOff[i] = t->getRegister("r" + std::to_string(i)).offset;
        // ...and after them SR, GBR, VBR, MACH, MACL as bits 16-20, as CallEffects::preserved numbers them.
        static const char* const kState[] = { "sr", "gbr", "vbr", "mach", "macl" };
        for (int i = 0; i < 5; ++i) mRegOff[16 + i] = t->getRegister(kState[i]).offset;
    }
    void SetEnv(Env* env) { mEnv = env; }
    void ResetUse()
    {
        mUse = RegUse();
        for (int i = 0; i < kUse; ++i) mWrittenBytes[i] = 0;
    }
    const RegUse& Use() const { return mUse; }

protected:
    void Note(const ghidra::PcodeOpRaw* op)
    {
        if (!op) return;
        // nop is `r0 = r0` in the spec (to silence its compiler): neither a read nor a write of r0.
        if (op->getOpcode() == ghidra::CPUI_COPY && op->numInput() == 1 && op->getOutput() != nullptr &&
            op->getOutput()->space == op->getInput(0)->space && op->getOutput()->offset == op->getInput(0)->offset &&
            op->getOutput()->size == op->getInput(0)->size)
            return;
        // The word of the instruction this op belongs to, for the one read that is not a read of the value:
        // a push of the register (`mov.l Rn,@-r15`) saves it. Whatever the callee-saved or spilled register held
        // is on the stack for the code to find, but the value is not an input of the code that pushed it: a later
        // read of an uninitialised local that happens to be that slot (the generator has them) is a dependence
        // on the stack's garbage, which is no register to pass.
        uint16_t word = 0;
        if (mEnv != nullptr)
        {
            const uint32_t at = static_cast<uint32_t>(op->getAddr().getOffset()) & 0xFFFFFu;
            if (at + 1 < mEnv->ram.size()) word = static_cast<uint16_t>(mEnv->ram[at] << 8 | mEnv->ram[at + 1]);
        }
        for (int i = 0; i < op->numInput(); ++i)
        {
            const ghidra::VarnodeData* vn = op->getInput(i);
            if (vn->space != mRegSpace) continue;
            for (int r = 0; r < kUse; ++r)
            {
                if (r < 16 && (word & 0xFF0Fu) == 0x2F06u && static_cast<int>((word >> 4) & 0xFu) == r) continue;   // the push
                for (int b = 0; b < 4; ++b)
                {
                    const ghidra::uintb at = mRegOff[r] + b;
                    if (at >= vn->offset && at < vn->offset + vn->size && !((mWrittenBytes[r] >> b) & 1)) mUse.readFirst |= 1u << r;
                }
            }
        }
        const ghidra::VarnodeData* out = op->getOutput();
        if (out && out->space == mRegSpace)
            for (int r = 0; r < kUse; ++r)
                for (int b = 0; b < 4; ++b)
                {
                    const ghidra::uintb at = mRegOff[r] + b;
                    if (at >= out->offset && at < out->offset + out->size)
                    {
                        mWrittenBytes[r] |= 1u << b;
                        mUse.written |= 1u << r;
                    }
                }
    }
    void fallthruOp() override
    {
        Note(currentOp);
        ghidra::EmulatePcodeCache::fallthruOp();
    }
    void executeBranchind() override
    {
        Note(currentOp);
        ghidra::EmulatePcodeCache::executeBranchind();
    }
    void executeCallind() override
    {
        Note(currentOp);
        ghidra::EmulatePcodeCache::executeCallind();
    }
    void executeLoad() override
    {
        ghidra::uintb off = memstate->getValue(currentOp->getInput(1));
        ghidra::AddrSpace* spc = currentOp->getInput(0)->getSpaceFromConst();
        off = ghidra::AddrSpace::addressToByte(off, spc->getWordSize());
        const int size = currentOp->getOutput()->size;
        memstate->setValue(currentOp->getOutput(),
                           mEnv->Read(static_cast<uint32_t>(off), size, static_cast<uint32_t>(currentOp->getAddr().getOffset())));
    }
    void executeStore() override
    {
        const ghidra::uintb val = memstate->getValue(currentOp->getInput(2));
        ghidra::uintb off = memstate->getValue(currentOp->getInput(1));
        ghidra::AddrSpace* spc = currentOp->getInput(0)->getSpaceFromConst();
        off = ghidra::AddrSpace::addressToByte(off, spc->getWordSize());
        mEnv->Write(static_cast<uint32_t>(off), currentOp->getInput(2)->size, val,
                    static_cast<uint32_t>(currentOp->getAddr().getOffset()));
    }

private:
    Env* mEnv;
    ghidra::AddrSpace* mRegSpace = nullptr;
    static constexpr int kUse = 21;   // r0-r15, SR, GBR, VBR, MACH, MACL
    ghidra::uintb mRegOff[kUse];
    uint32_t mWrittenBytes[kUse] = {};
    RegUse mUse;
};

// One run: where it started, where each instruction was, what it touched, where the registers ended.
struct RunOut
{
    bool finished = false;
    std::string error;
    std::vector<TraceRec> trace;       // in order, with values
    std::vector<uint32_t> pcs;         // the address of every instruction executed, in order
    std::vector<int> depths;           // ...and how many calls deep it ran (0: the function's own)
    uint32_t init[16] = {}, fin[16] = {};
    uint32_t initPr = 0, finPr = 0, initSr = 0, finSr = 0;
    uint32_t initState[4] = {}, finState[4] = {};   // GBR, VBR, MACH, MACL (register 17 + n)
    RegUse use;
    int mode = 0;
    static constexpr int kNoSel = INT32_MIN;
    int selValue = kNoSel;             // the integer a jump table was indexed with (kNoSel: none given; -1 is an index)
    int perturbed = -1;                // register whose start value was changed (-1: none)
    bool addressError = false;         // the run made a misaligned word or longword access: an address error on the SH-2 (A2-8 (a))
    TraceRec addressErrorAt;           // ...the first, for the report
};

// A word or longword access at an address that is not a multiple of its size. The emulator makes it; the SH-2 raises an address
// error and does not (sh7095.inc, SH7095_PEX_CPUADDR). The model has no exceptions (A2-8), so a run that makes one is not a
// run of the model.
bool IsMisaligned(const TraceRec& t) { return t.size > 1 && (t.addr & static_cast<uint32_t>(t.size - 1)) != 0; }

class Machine
{
public:
    Machine(const std::shared_ptr<MemorySnapshot>& snap, Env* env) : mSnap(snap)
    {
        mArch.reset(new SaturnArchitecture(mSnap, &mErrors));
        mArch->Initialise(20000, nullptr);
        ghidra::Translate* t = const_cast<ghidra::Translate*>(mArch->translate);
        mRam = t->getDefaultCodeSpace();
        mRamState.reset(new ghidra::MemoryPageOverlay(mRam, 8, 4096, nullptr));
        mRegs.reset(new ghidra::MemoryHashOverlay(t->getSpaceByName("register"), 8, 4096, 4096, nullptr));
        mTemps.reset(new ghidra::MemoryHashOverlay(t->getUniqueSpace(), 8, 4096, 16384, nullptr));
        mState.reset(new ghidra::MemoryState(t));
        mState->setMemoryBank(mRamState.get());
        mState->setMemoryBank(mRegs.get());
        mState->setMemoryBank(mTemps.get());
        mBreaks.reset(new ghidra::BreakTableCallBack(t));
        dts::HookSh2Userops(*mBreaks);
        mEmu.reset(new TracingEmulate(t, mState.get(), mBreaks.get(), env));
    }
    ~Machine()
    {
        mEmu.reset();
        mBreaks.reset();
        mState.reset();
    }
    void Set(const char* reg, uint32_t v) { mState->setValue(reg, v); }
    uint32_t Get(const char* reg) const { return static_cast<uint32_t>(mState->getValue(reg)); }
    void SetEnv(Env* env) { mEmu->SetEnv(env); }

    // From the entry until the return address (or the step limit). False if it did not return.
    // Fills 'out' with the pcs, the register use and the final registers; the memory trace is the Env's.
    bool Run(uint32_t start, uint32_t stop, int limit, RunOut& out, Env& env)
    {
        mEmu->ResetUse();
        bool done = false;
        int depth = 0;
        try
        {
            mEmu->setExecuteAddress(ghidra::Address(mRam, start));
            mEmu->setHalt(false);
            for (int i = 0; i < limit && !done; ++i)
            {
                const uint32_t pc = static_cast<uint32_t>(mEmu->getExecuteAddress().getOffset());
                uint16_t op = 0;
                mSnap->ReadU16(pc, op);
                const sfe::DisassembledInstruction ins = sfe::Sh2Decode(pc, op);
                env.depth = depth;
                env.step = i;
                out.pcs.push_back(Env::Canon(pc));   // a callee reached through the cache-through mirror is the function at the canonical address (A4-6)
                out.depths.push_back(depth);
                // The delay slot runs with the branch, at the depth it was called from.
                if (ins.IsCall) ++depth;
                else if (ins.IsReturn) --depth;
                mEmu->executeInstruction();
                if (mEmu->getExecuteAddress().getOffset() == stop) done = true;
            }
        }
        catch (ghidra::LowlevelError& e)
        {
            out.error = e.explain;
        }
        out.use = mEmu->Use();
        for (int r = 0; r < 16; ++r) out.fin[r] = Get(("r" + std::to_string(r)).c_str());
        out.finPr = Get("pr");
        out.finSr = Get("sr");
        static const char* const kState[4] = { "gbr", "vbr", "mach", "macl" };
        for (int n = 0; n < 4; ++n) out.finState[n] = Get(kState[n]);
        out.finished = done;
        return done;
    }

private:
    std::ostringstream mErrors;
    std::shared_ptr<MemorySnapshot> mSnap;
    std::unique_ptr<SaturnArchitecture> mArch;
    ghidra::AddrSpace* mRam = nullptr;
    std::unique_ptr<ghidra::MemoryPageOverlay> mRamState;
    std::unique_ptr<ghidra::MemoryHashOverlay> mRegs, mTemps;
    std::unique_ptr<ghidra::MemoryState> mState;
    std::unique_ptr<ghidra::BreakTableCallBack> mBreaks;
    std::unique_ptr<TracingEmulate> mEmu;
};

// ---- The ledger's ordering invariants, copied from Sh2DecompilerTests.cpp ---------------------
// (the same functions the hand-written fixtures are held to; keep them in step with that file)

// Two reads in one statement are in an order C leaves open -- unless one feeds the other
// (a pointer read and the read through it), which orders them.
bool Unordered(const MemoryAccess& a, const MemoryAccess& b)
{
    if (a.write || b.write || a.statement != b.statement) return false;
    auto feeds = [](const MemoryAccess& x, const MemoryAccess& y) {
        return std::find(x.through.begin(), x.through.end(), y.id) != x.through.end();
    };
    return !feeds(a, b) && !feeds(b, a);
}

// Reachability over the flow's blocks as first followed (DecompileResult::flowSuccessors): the
// reference for the order two accesses can be made in. The final blocks are joined, split and
// copied by the engine, so op ids, addresses and output lines all mislead (review of 0e3cd12).
struct FlowReach
{
    std::vector<std::vector<char>> reach;    // reach[x][y]: y runs after x on some path
    std::vector<std::vector<char>> uncond;   // uncond[x][y]: ...and on every path, through blocks with one way out
    explicit FlowReach(const std::vector<std::vector<int>>& succ)
    {
        const size_t n = succ.size();
        reach.assign(n, std::vector<char>(n, 0));
        uncond.assign(n, std::vector<char>(n, 0));
        for (size_t x = 0; x < n; ++x)
        {
            std::vector<int> todo(succ[x].begin(), succ[x].end());
            while (!todo.empty())
            {
                const int y = todo.back();
                todo.pop_back();
                if (y < 0 || size_t(y) >= n || reach[x][y]) continue;
                reach[x][y] = 1;
                todo.insert(todo.end(), succ[y].begin(), succ[y].end());
            }
            for (size_t y = x; succ[y].size() == 1;)
            {
                const int z = succ[y][0];
                if (z < 0 || size_t(z) >= n || uncond[x][z] || size_t(z) == x) break;
                uncond[x][z] = 1;
                y = size_t(z);
            }
        }
    }
};

// Whether the code can make 'a' before 'b'. Unknown origins (ops the engine made later) are not judged.
bool FlowOrdered(const FlowReach& fr, const MemoryAccess& a, const MemoryAccess& b)
{
    if (a.origBlock < 0 || b.origBlock < 0) return true;
    if (a.origBlock == b.origBlock) return a.origOrder < b.origOrder;
    return fr.reach[a.origBlock][b.origBlock] != 0;
}

// Two accesses in one final block, printed in an order the code cannot make them in.
bool OutOfFlowOrder(const FlowReach& fr, const MemoryAccess& a, const MemoryAccess& b)
{
    if (a.block < 0 || a.block != b.block) return false;
    return a.order < b.order ? !FlowOrdered(fr, a, b) : !FlowOrdered(fr, b, a);
}

// Two accesses from blocks the flow kept apart by a branch, now in one block that ends in a switch:
// a guard folded into the switch over a read, which then appears made for the guarded-out values.
bool FoldedIntoSwitch(const FlowReach& fr, const MemoryAccess& a, const MemoryAccess& b)
{
    if (a.block < 0 || a.block != b.block || !a.inSwitchBlock || a.origBlock < 0 || b.origBlock < 0 || a.origBlock == b.origBlock)
        return false;
    return !fr.uncond[a.origBlock][b.origBlock] && !fr.uncond[b.origBlock][a.origBlock];
}

// A read printed where its value is used, later in its block, with another access or a call made in
// between: it is shown after them.
bool PrintedPast(const MemoryAccess& m, const std::vector<MemoryAccess>& accesses, const std::vector<CallSite>& calls,
                 std::string* what)
{
    if (m.write || m.statement == m.id || m.block < 0 || m.block != m.statementBlock) return false;
    for (const MemoryAccess& o : accesses)
        if (o.id != m.id && o.block == m.block && o.order > m.order && o.order < m.statementOrder &&
            std::find(m.through.begin(), m.through.end(), o.id) == m.through.end())
        {
            if (what) *what = "the access at " + [&] { char b[12]; std::snprintf(b, sizeof b, "%08X", o.insn); return std::string(b); }();
            return true;
        }
    for (const CallSite& c : calls)
        if (c.block == m.block && c.order > m.order && c.order < m.statementOrder)
        {
            if (what) *what = "the call at " + [&] { char b[12]; std::snprintf(b, sizeof b, "%08X", c.insn); return std::string(b); }();
            return true;
        }
    return false;
}


// The addresses of one execution's accesses (the same instruction, direction and size, in order) that
// no ledger entry stands for. An entry stands for one access, never two: mac.l and mac.w read two
// operands in one instruction, and one printed operand must not excuse a lost second (ledger A5-5,
// which the host's Accounts closes the same way). An entry with a constant address stands only for an
// access at that address; one through a pointer stands for any. Exact pairs are made first, so an
// access through a pointer does not take a constant one's entry; then the accesses the decompiler
// must list ('mustList': a parameter's memory, hardware, marked memory) take the entries through a
// pointer before the ones it may fold into a variable (a mac reading ordinary RAM beside a parameter's
// memory has one entry, and it is the parameter's).
std::vector<uint32_t> UnaccountedAddresses(const std::vector<const MemoryAccess*>& entries, const std::vector<uint32_t>& addrs,
                                           const std::function<bool(uint32_t)>& mustList)
{
    std::vector<char> used(entries.size(), 0), done(addrs.size(), 0);
    for (int round = 0; round < 3; ++round)
        for (size_t i = 0; i < addrs.size(); ++i)
        {
            if (done[i]) continue;
            if (round == 1 && !mustList(addrs[i])) continue;
            for (size_t j = 0; j < entries.size() && !done[i]; ++j)
            {
                if (used[j]) continue;
                const MemoryAccess& e = *entries[j];
                if (round == 0 ? !(e.direct && e.address == addrs[i]) : e.direct) continue;
                used[j] = done[i] = 1;
            }
        }
    std::vector<uint32_t> lost;
    for (size_t i = 0; i < addrs.size(); ++i)
        if (!done[i]) lost.push_back(addrs[i]);
    return lost;
}

// ---- Rule 3b's premise (A5-38, A5-46): does the engine know the address the pointer holds? ----------------------------------
// A read through a pointer is the decompiler's to say when the output shows where the pointer comes from: `&REG`, a number that is a
// register's address (`(xunknown4 *)0x25fe0110`), a variable assigned one (`puVar2 = &VDP2_TVSTAT;`), or such a base with an offset
// (`puVar1[4]`). The premise is the pointer's printed derivation, not a name somewhere in the output: a name that is only the argument
// of a call (`f(p, &VDP2_VRSIZE); x = *extraout_r6;`) is not the pointer's -- r6 after the call is the engine's `extraout`, a value it
// does not know, and a callee decompiled alone cannot know it either.

// The text without the argument lists of calls (`name(...)`), which are not what the expression holds; `if (`, `while(` and casts stay.
std::string WithoutCalls(const std::string& text)
{
    static const char* const keywords[] = { "if", "while", "for", "switch", "return", "sizeof" };
    std::string out;
    for (size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] == '(' && i > 0 && (std::isalnum(static_cast<unsigned char>(text[i - 1])) || text[i - 1] == '_'))
        {
            size_t b = i;
            while (b > 0 && (std::isalnum(static_cast<unsigned char>(text[b - 1])) || text[b - 1] == '_')) --b;
            const std::string word = text.substr(b, i - b);
            bool keyword = false;
            for (const char* k : keywords) keyword |= word == k;
            if (!keyword)
            {
                int depth = 0;
                size_t j = i;
                for (; j < text.size(); ++j)
                {
                    depth += text[j] == '(';
                    depth -= text[j] == ')';
                    if (depth == 0) break;
                }
                i = j;
                continue;
            }
        }
        out += text[i];
    }
    return out;
}

std::vector<std::string> Identifiers(const std::string& text)
{
    std::vector<std::string> out;
    for (size_t i = 0; i < text.size();)
    {
        if (std::isalpha(static_cast<unsigned char>(text[i])) || text[i] == '_')
        {
            size_t j = i;
            while (j < text.size() && (std::isalnum(static_cast<unsigned char>(text[j])) || text[j] == '_')) ++j;
            out.push_back(text.substr(i, j - i));
            i = j;
        }
        else if (std::isdigit(static_cast<unsigned char>(text[i])))   // a number (0x25fe0110) is no identifier
        {
            while (i < text.size() && std::isalnum(static_cast<unsigned char>(text[i]))) ++i;
        }
        else ++i;
    }
    return out;
}

// A register's symbol (`&NAME`) or a hexadecimal number that the bus decodes to a register, in the text.
bool NamesHardwareBase(const std::string& text)
{
    static std::set<std::string> names;
    if (names.empty())
    {
        size_t n = 0;
        const HardwareRegister* regs = SaturnHardwareRegisters(n);
        for (size_t i = 0; i < n; ++i) names.insert(regs[i].name);
        regs = SaturnHardwareWordViews(n);
        for (size_t i = 0; i < n; ++i) names.insert(regs[i].name);
    }
    for (size_t at = text.find('&'); at != std::string::npos; at = text.find('&', at + 1))
    {
        size_t j = at + 1;
        while (j < text.size() && (std::isalnum(static_cast<unsigned char>(text[j])) || text[j] == '_')) ++j;
        if (names.count(text.substr(at + 1, j - at - 1))) return true;
    }
    for (size_t at = text.find("0x"); at != std::string::npos; at = text.find("0x", at + 2))
    {
        size_t j = at + 2;
        uint64_t v = 0;
        while (j < text.size() && std::isxdigit(static_cast<unsigned char>(text[j])) && j - at < 11)
            v = v * 16 + static_cast<uint64_t>(std::isdigit(static_cast<unsigned char>(text[j])) ? text[j] - '0' : std::tolower(text[j]) - 'a' + 10), ++j;
        uint32_t canon = 0;
        if (v >= 0x00100000u && v <= 0xFFFFFFFFu && HardwareAlias(static_cast<uint32_t>(v), canon) && HardwareRegistersCovering(static_cast<uint32_t>(v), 4).size() +
                                                                                                      HardwareRegistersCovering(static_cast<uint32_t>(v), 1).size() > 0)
            return true;
    }
    return false;
}

// The read at 'insn' goes through a pointer that the output derives from a hardware base: the statement holding it names one, or a
// variable in it is assigned one, through assignments of variables to variables. 'unrelated' is set when it does not but the output
// names the register somewhere else (the argument of a call, another statement): counted apart, not folded into anonymous reads.
bool PointerPrintsFromHardware(const DecompileResult& r, uint32_t insn, const std::vector<const HardwareRegister*>& covering, bool& unrelated)
{
    unrelated = false;
    // The read's own tokens on each line that has some: what the instruction contributes to the statement, not the whole statement (a
    // comparison's other operand, `... != &VDP2_CYCB0U`, comes from another instruction and is not the pointer, seed 347).
    std::vector<std::string> statements;
    for (const Line& l : r.lines)
    {
        std::string own;
        bool any = false;
        for (const Token& t : l.tokens)
            if (t.addr == insn)
            {
                any = true;
                own += l.text.substr(t.begin, t.end - t.begin) + " ";
            }
        if (!any) continue;
        // (a line whose own tokens name nothing, only an operator, is judged whole)
        bool names = false;
        for (char c : own) names |= std::isalnum(static_cast<unsigned char>(c)) || c == '_';
        statements.push_back(names ? own : l.text);
    }
    bool elsewhere = false;
    for (const HardwareRegister* reg : covering)
        for (const Line& l : r.lines)
            elsewhere |= l.text.find(std::string("&") + reg->name) != std::string::npos;
    if (statements.empty()) return elsewhere;   // (the read's text was not found: the old premise, never narrower)
    // Assignments `name = value;` in the output.
    std::vector<std::pair<std::string, std::string>> assigns;
    for (const Line& l : r.lines)
    {
        size_t b = l.text.find_first_not_of(' ');
        if (b == std::string::npos) continue;
        size_t e = b;
        while (e < l.text.size() && (std::isalnum(static_cast<unsigned char>(l.text[e])) || l.text[e] == '_')) ++e;
        if (e == b || l.text.compare(e, 3, " = ") != 0) continue;
        std::string rhs = l.text.substr(e + 3);
        while (!rhs.empty() && (rhs.back() == ';' || rhs.back() == ' ')) rhs.pop_back();
        assigns.emplace_back(l.text.substr(b, e - b), rhs);
    }
    std::set<std::string> seen;
    std::vector<std::string> todo;
    for (const std::string& st : statements)
    {
        std::string text = st;
        const size_t b = text.find_first_not_of(' ');
        size_t e = b == std::string::npos ? 0 : b;
        while (e < text.size() && (std::isalnum(static_cast<unsigned char>(text[e])) || text[e] == '_')) ++e;
        if (b != std::string::npos && e > b && text.compare(e, 3, " = ") == 0) text = text.substr(e + 3);   // (not the variable assigned to)
        text = WithoutCalls(text);
        if (NamesHardwareBase(text)) return true;
        for (const std::string& id : Identifiers(text)) todo.push_back(id);
    }
    while (!todo.empty() && seen.size() < 256)
    {
        const std::string id = todo.back();
        todo.pop_back();
        if (!seen.insert(id).second) continue;
        for (const auto& a : assigns)
        {
            if (a.first != id) continue;
            const std::string rhs = WithoutCalls(a.second);
            if (NamesHardwareBase(rhs)) return true;
            for (const std::string& q : Identifiers(rhs)) todo.push_back(q);
        }
    }
    unrelated = elsewhere;
    return false;
}

// The output says, on this instruction, that its access is missing because the code around it was
// removed as unreachable: the caution itself (the same test as Sh2DecompilerTests.cpp's).
bool SaidMissing(const DecompileResult& r, uint32_t insn)
{
    for (const HardwareNote& n : r.hardwareNotes)
        if (n.address == insn && n.caution && n.text.find("here is missing") != std::string::npos) return true;
    return false;
}

// ---- One seed -------------------------------------------------------------------------------

struct Finding
{
    std::string sig;      // the class: stable under shrinking, e.g. "dropped:W2:mbox"
    std::string detail;   // the line that says what is wrong
};

struct Report
{
    std::vector<Finding> findings;
    std::string text;        // everything the verbose report prints
    int unfinishedRuns = 0;  // emulator runs that hit the step limit or an engine error
    int volatileRuns = 0;
    size_t traced = 0;       // distinct (instruction, direction, size) the emulator saw
    double decompileMs = 0;
    std::map<std::string, int> counts;   // what the checks saw: lent prototypes, tables traced, ...
};

// One function of what is checked: where its code is, and what the generator meant it to do.
struct FnInfo
{
    UnitLayout layout;
    bool intent = false;     // generated: 'returns' and 'sel' mean something
    bool returns = false;    // r0 is written on every path just before the return
    int sel = -1;            // the register a jump table indexes with
    uint32_t clobbers = 0;   // registers it leaves changed (Func::effClobbers): bit n is Rn, 17 gbr, 18 vbr, 19 mach, 20 macl
    uint32_t reads = 0;      // registers whose entry value it reads on purpose (Func::reads)
};

// What is checked: the code and its literal pool as words from kCode, the functions in it, what the
// emulator's other processor does, and the volatile range the second request marks. A generated
// program makes one (MakeSubject); so does a raw word list from the environment, which is how a
// finding is reduced by hand.
struct Subject
{
    std::vector<uint16_t> words;
    size_t codeWords = 0;                  // version 1 and raw: the words before the pool (the disassembly stops there)
    std::vector<FnInfo> fns;               // at least one
    bool v2 = false;
    bool bad = false;                      // the layout did not fit (a pool out of reach)
    std::vector<Env::Poll> polls;          // addresses read busy, then ready
    bool markVolatile = false;             // a second request marks [volFirst, volLast] volatile
    uint32_t volFirst = 0, volLast = 0;
    std::string literals;                  // for the report

    uint16_t WordAt(uint32_t addr) const
    {
        const size_t i = (addr - kCode) / 2;
        return addr >= kCode && i < words.size() ? words[i] : uint16_t(0);
    }
    int UnitOf(uint32_t addr) const
    {
        for (size_t i = 0; i < fns.size(); ++i)
            if (fns[i].layout.start <= addr && addr < fns[i].layout.end) return static_cast<int>(i);
        return -1;
    }
    int FnAtEntry(uint32_t addr) const
    {
        for (size_t i = 0; i < fns.size(); ++i)
            if (fns[i].layout.entry == addr) return static_cast<int>(i);
        return -1;
    }
};

Subject MakeSubject(const Program& p)
{
    Subject s;
    const Assembled a = Assemble(p);
    s.v2 = p.version == 2;
    s.bad = !a.ok;
    s.words = a.words;
    s.codeWords = a.codeWords;
    for (size_t i = 0; i < p.fns.size(); ++i)
    {
        FnInfo f;
        f.layout = a.units[i];
        f.intent = p.fns[i].v2;
        f.returns = p.fns[i].returns;
        f.sel = p.fns[i].sel;
        f.clobbers = p.fns[i].effClobbers;
        f.reads = p.fns[i].reads;
        s.fns.push_back(f);
    }
    for (const Func& fn : p.fns)
        for (const Item& it : fn.items)
            if (it.type == Item::Poll)
                s.polls.push_back({ Env::Canon(p.lits[it.lit].value), it.untilSet ? 0u : 1u, it.untilSet ? 1u : 0u, 0, p.lits[it.lit].value });
    std::ostringstream o;
    for (size_t i = 0; i < p.lits.size(); ++i)
        if (a.litAddr[i])
        {
            const uint32_t v = p.lits[i].kind == LitKind::Fn ? a.units[p.lits[i].fn].entry : p.lits[i].value;
            o << " [" << Hex(a.litAddr[i]) << "]=" << Hex(v) << " (" << KindName(p.lits[i].kind) << ")";
            if (p.lits[i].kind == LitKind::Mbox) s.markVolatile = true;
        }
    s.literals = o.str();
    s.volFirst = p.mbox;
    s.volLast = p.mbox + 15;
    return s;
}

std::string SubjectText(const Subject& s, Sh2Decompiler& d, const std::shared_ptr<MemorySnapshot>& snap)
{
    std::ostringstream o;
    o << "  fixture: { 0x" << Hex(kCode) << ", { ";
    for (size_t i = 0; i < s.words.size(); ++i) o << (i ? ", " : "") << "0x" << Hex(s.words[i], 4);
    // The Fixture struct writes one 32-bit word at its literal offset: the first pool word (or
    // zeros past the end), so that pasting it does not overwrite code.
    const size_t pool = (s.codeWords * 2 + 3) & ~size_t(3);
    const uint32_t first = pool / 2 + 1 < s.words.size() ? (uint32_t(s.words[pool / 2]) << 16) | s.words[pool / 2 + 1] : 0;
    o << " }, 0x" << Hex(static_cast<uint32_t>(pool), 2) << ", 0x" << Hex(first) << " }   (the literal pool is among the words too)\n";
    // The replay line: the words, and for more than one function where each one is.
    o << "  replay: SE_GENERATED_WORDS=\"";
    for (size_t i = 0; i < s.words.size(); ++i) o << (i ? " " : "") << Hex(s.words[i], 4);
    o << "\"";
    if (s.v2)
    {
        o << " SE_GENERATED_FUNCS=\"";
        for (size_t i = 0; i < s.fns.size(); ++i)
            o << (i ? "," : "") << std::hex << (s.fns[i].layout.start - kCode) << "/" << (s.fns[i].layout.entry - kCode) << "/"
              << (s.fns[i].layout.end - kCode) << std::dec;
        o << "\"";
        std::ostringstream tabs, data;
        for (const FnInfo& f : s.fns)
        {
            for (const TableLayout& t : f.layout.tables)
            {
                tabs << (tabs.str().empty() ? "" : ";") << std::hex << (t.jmpInsn - kCode) << ":" << std::dec << t.sel << ":" << t.n << ":" << std::hex
                     << (t.defaultAddr - kCode) << ":";
                for (size_t k = 0; k < t.entry.size(); ++k) tabs << (k ? "," : "") << (t.entry[k] - kCode);
                if (t.hasPre) tabs << ":" << (t.preEntry - kCode);   // the entry for index -1, the word before the table
                tabs << std::dec;
            }
            for (const std::pair<uint32_t, uint32_t>& r : f.layout.data)
                data << (data.str().empty() ? "" : ",") << std::hex << (r.first - kCode) << "-" << (r.second - kCode) << std::dec;
        }
        if (!tabs.str().empty()) o << " SE_GENERATED_TABLES=\"" << tabs.str() << "\"";
        if (!data.str().empty()) o << " SE_GENERATED_DATA=\"" << data.str() << "\"";
        o << " SE_GENERATED_RETURNS=\"";
        for (size_t i = 0; i < s.fns.size(); ++i) o << (i ? "," : "") << (s.fns[i].returns ? 1 : 0);
        o << "\"";
        bool conv = false;
        for (const FnInfo& f : s.fns) conv = conv || f.clobbers || f.reads;
        if (conv)
        {
            o << " SE_GENERATED_CLOBBERS=\"";
            for (size_t i = 0; i < s.fns.size(); ++i) o << (i ? "," : "") << std::hex << s.fns[i].clobbers << std::dec;
            o << "\" SE_GENERATED_READS=\"";
            for (size_t i = 0; i < s.fns.size(); ++i) o << (i ? "," : "") << std::hex << s.fns[i].reads << std::dec;
            o << "\"";
        }
    }
    o << "\n";
    if (!s.literals.empty()) o << "  literals:" << s.literals << "\n";
    if (s.markVolatile) o << "  second request marks volatile: " << Hex(s.volFirst) << "-" << Hex(s.volLast) << "\n";
    for (const Env::Poll& p : s.polls)
        o << "  polled address " << Hex(p.addr) << ": reads " << p.busy << " " << kPollBusyReads << " times, then " << p.ready << "\n";
    for (size_t i = 0; i < s.fns.size(); ++i)
    {
        const UnitLayout& u = s.fns[i].layout;
        o << "  function " << i << " at " << Hex(u.entry) << (u.start != u.entry ? " (exit before the entry, from " + Hex(u.start) + ")" : std::string())
          << (u.falls ? " (runs on into the next)" : "") << (s.fns[i].intent ? (s.fns[i].returns ? " returns r0" : " leaves r0 alone") : "") << "\n";
        const int count = s.v2 ? static_cast<int>((u.end - u.start) / 2) : static_cast<int>(s.codeWords);
        for (const std::string& line : d.Disassemble(snap, u.start, count)) o << "    " << line << "\n";
    }
    return o.str();
}

std::string AccessText(const MemoryAccess& m)
{
    std::ostringstream o;
    o << (m.write ? "write" : "read") << " " << unsigned(m.size) << " at " << Hex(m.insn) << (m.direct ? " direct " + Hex(m.address) : " via pointer")
      << (m.inLoop ? " in loop" : "") << " printed " << m.printed << "x/" << m.printedLines << " line(s)"
      << (m.repeatedOnPath ? " REPEATED-ON-PATH" : "") << (m.conditionalInPrint ? " CONDITIONAL-IN-PRINT" : "");
    return o.str();
}

// Where r4/r5, and r6/r7 before a literal is loaded into them, point: the pointers that are
// parameters, not constants, so an access through one is never a value the decompiler may fold.
bool InScratch(uint32_t addr) { return (addr & 0x0FFFFFFFu) >= 0x06020000u && (addr & 0x0FFFFFFFu) < 0x06030000u; }

// The stack area: an access there through r15, or the frame pointer r14, is a local.
bool InStack(uint32_t addr) { return (addr & 0x0FFFFFFFu) >= kStackLo && (addr & 0x0FFFFFFFu) < kStackHi; }

// The register the memory operand of an instruction word is based on, -1 if it has none.
int MemBase(uint16_t w)
{
    const int n = (w >> 8) & 15, m = (w >> 4) & 15, z = w & 15;
    switch (w >> 12)
    {
    case 0x0:   // mov.x Rm,@(r0,Rn) / mov.x @(r0,Rm),Rn
        if (z >= 4 && z <= 6) return n;
        if (z >= 12 && z <= 14) return m;
        return -1;
    case 0x1: return n;   // mov.l Rm,@(disp,Rn)
    case 0x5: return m;   // mov.l @(disp,Rm),Rn
    case 0x2:             // mov.x Rm,@Rn / @-Rn
        return (z <= 2 || (z >= 4 && z <= 6)) ? n : -1;
    case 0x6:             // mov.x @Rm,Rn / @Rm+,Rn
        return (z <= 2 || (z >= 4 && z <= 6)) ? m : -1;
    case 0x8:
        switch (w & 0xFF00)
        {
        case 0x8000: case 0x8100: case 0x8400: case 0x8500: return m;   // mov.b/w r0,@(disp,Rn) and back
        default: return -1;
        }
    case 0x4:
        if ((w & 0xF0FF) == 0x401B) return n;   // tas.b @Rn
        switch (w & 0xFF)
        {
        case 0x02: case 0x03: case 0x12: case 0x13: case 0x22: case 0x23:   // sts.l / stc.l ,@-Rn
        case 0x06: case 0x07: case 0x16: case 0x17: case 0x26: case 0x27:   // lds.l / ldc.l @Rn+,
            return n;
        default: return -1;
        }
    default: return -1;
    }
}
bool IsLocalAccess(uint16_t w)
{
    const int b = MemBase(w);
    return b == 15 || b == 14;
}

// ---- Running the emulator ---------------------------------------------------------------------

struct RunSpec
{
    int mode = 0;              // the register and hardware inputs: 0 zeros, 1 ones, 2 and up pseudo-random
    int selReg = -1;           // a register holding an integer (a jump table's index), and its value
    uint32_t selValue = 0;
    int perturbReg = -1;       // a register started with another value (0-15, or 17-20 for GBR, VBR, MACH, MACL)
    uint32_t perturbValue = 0;
    bool volatileRun = false;  // the other processor answers what the second request marks
};

// Runs a subject's functions, one machine for all of it.
class Runner
{
public:
    Runner(const Subject& s, const std::shared_ptr<MemorySnapshot>& snap) : mS(s), mSnap(snap) {}

    // A decompilation builds an architecture of its own, which takes the engine's global translator
    // over: a machine made before it must not run after it. The next run makes a new one.
    void Invalidate() { mM.reset(); }

    RunOut One(const FnInfo& f, const RunSpec& rs)
    {
        std::vector<Env::Poll> polls;
        for (const Env::Poll& poll : mS.polls)
        {
            bool marked = false;
            if (rs.volatileRun && mS.markVolatile)
                for (const HardwareRange& h : VolatileAliases(mS.volFirst, mS.volLast))
                    if (h.first <= poll.raw && poll.raw <= h.last) marked = true;
            if (InHardwareRange(poll.addr) || marked) polls.push_back(poll);
        }
        Env env;
        env.ram = mSnap->regions[1].bytes;   // HWRAM
        env.hwMode = rs.mode % 3;
        env.polls = polls;
        if (!mM) mM.reset(new Machine(mSnap, &mEnv));
        mM->SetEnv(&env);
        Rng init(0xABCD00u + rs.mode);
        RunOut out;
        out.mode = rs.mode;
        for (int r = 0; r < 4; ++r) out.init[r] = rs.mode == 0 ? 0u : init.Next();
        out.init[4] = kScratch4;
        out.init[5] = kScratch5;
        out.init[6] = kScratch6;
        out.init[7] = kScratch7;
        // Callee-saved registers: distinct values, so a function that fails to restore one shows.
        for (int r = 8; r < 15; ++r) out.init[r] = 0x80000000u | uint32_t(rs.mode + 1) << 20 | uint32_t(r) << 8 | uint32_t(r);
        out.init[15] = kStackTop;
        if (rs.selReg >= 4)
        {
            out.init[rs.selReg] = rs.selValue;
            out.selValue = static_cast<int>(rs.selValue);
        }
        // GBR, VBR, MACH and MACL start at values of their own too (distinct per register and per mode), so a
        // function that sets one to zero, or to what it already was, shows.
        for (int n = 0; n < 4; ++n) out.initState[n] = (0x0A000000u + 0x01000000u * n) | uint32_t(rs.mode + 1) << 8 | 0x5Au;
        // ...but GBR is also the base of the @(disp,GBR) and @(r0,GBR) accesses (area 2's alphabet), so it points into scratch RAM,
        // at an address of its own per mode: distinct as the others are (a function that loads it with what it already was shows),
        // aligned, and clear of the other scratch areas.
        out.initState[0] = kScratchGbr + 0x20u * uint32_t(rs.mode & 7);
        if (rs.perturbReg >= 0 && rs.perturbReg < 16)
        {
            out.init[rs.perturbReg] = rs.perturbValue;
            out.perturbed = rs.perturbReg;
        }
        else if (rs.perturbReg >= 17 && rs.perturbReg <= 20)
        {
            out.initState[rs.perturbReg - 17] = rs.perturbValue;
            out.perturbed = rs.perturbReg;
        }
        out.initPr = kSentinel;
        out.initSr = 0;
        for (int r = 0; r < 16; ++r) mM->Set(("r" + std::to_string(r)).c_str(), out.init[r]);
        mM->Set("pr", kSentinel);
        mM->Set("sr", 0);
        static const char* const kState[4] = { "gbr", "vbr", "mach", "macl" };
        for (int n = 0; n < 4; ++n) mM->Set(kState[n], out.initState[n]);
        mM->Run(f.layout.entry, kSentinel, kStepLimit, out, env);
        out.trace = std::move(env.trace);
        for (const TraceRec& t : out.trace)
            if (IsMisaligned(t))
            {
                out.addressError = true;
                out.addressErrorAt = t;
                break;
            }
        return out;
    }

    // The ordinary inputs of a function: 3 runs (version 1) or 4 (version 2) that differ in the
    // registers and what hardware reads return, and for a jump table one more per index it can see.
    std::vector<RunOut> Ordinary(const FnInfo& f, bool volatileRun)
    {
        std::vector<RunOut> runs;
        const int modes = mS.v2 ? 4 : 3;
        int tableMax = 0;
        for (const TableLayout& t : f.layout.tables) tableMax = std::max(tableMax, t.n);
        const bool useSel = f.sel >= 4 && tableMax > 0;
        std::set<int> sels;
        bool negative = false;   // a table with a signed guard: index -1 is run too (the word before the table)
        for (const TableLayout& t : f.layout.tables) negative = negative || t.hasPre;
        for (int mode = 0; mode < modes; ++mode)
        {
            RunSpec rs;
            rs.mode = mode;
            rs.volatileRun = volatileRun;
            if (useSel)
            {
                Rng pick(0x5E10 + mode);
                rs.selReg = f.sel;
                rs.selValue = mode == 0 ? 0u : mode == 1 ? 1u : pick.Below(static_cast<uint32_t>(tableMax + 2));
                sels.insert(static_cast<int>(rs.selValue));
            }
            runs.push_back(One(f, rs));
        }
        if (useSel)
            for (int v = negative ? -1 : 0; v <= tableMax + 1; ++v)
                if (!sels.count(v))
                {
                    RunSpec rs;
                    rs.mode = 1 + (v < 0 ? 0 : v % 3);
                    rs.volatileRun = volatileRun;
                    rs.selReg = f.sel;
                    rs.selValue = static_cast<uint32_t>(v);
                    runs.push_back(One(f, rs));
                }
        return runs;
    }

private:
    const Subject& mS;
    std::shared_ptr<MemorySnapshot> mSnap;
    Env mEnv;
    std::unique_ptr<Machine> mM;
};

// Whether two runs of one function did the same to the world it must keep: the same accesses of
// hardware, of marked memory and through the parameter pointers (which the decompiler keeps) in the same
// order with the same values, and (when it returns something) the same r0. Ordinary memory at a constant
// address is a variable the analysis may drop, the function's own stack is its own, and the program's own
// words (literal pools, jump tables) are not accesses to the world at all.
bool SameBehaviour(const RunOut& a, const RunOut& b, bool compareR0, const std::vector<HardwareRange>& marked)
{
    auto kept = [&](const RunOut& r) {
        std::vector<TraceRec> v;
        for (const TraceRec& t : r.trace)
        {
            bool inMarked = false;
            for (const HardwareRange& h : marked)
                if (h.first <= t.addr && t.addr <= h.last) inMarked = true;
            if (InScratch(t.addr) || InHardwareRange(t.addr) || inMarked) v.push_back(t);
        }
        return v;
    };
    const std::vector<TraceRec> x = kept(a), y = kept(b);
    if (x.size() != y.size()) return false;
    for (size_t i = 0; i < x.size(); ++i)
        if (x[i] != y[i] || x[i].value != y[i].value) return false;
    return !compareR0 || a.fin[0] == b.fin[0];
}

// The instructions a run executed: each pc, and the delay slot of a delayed branch (which the
// emulator runs as part of the branch). 'own' are the ones the function itself ran, 'deep' the ones
// a callee ran.
void ExecutedOf(const Subject& s, const RunOut& run, std::set<uint32_t>& own, std::set<uint32_t>& deep)
{
    for (size_t i = 0; i < run.pcs.size(); ++i)
    {
        const uint32_t pc = run.pcs[i];
        std::set<uint32_t>& into = run.depths[i] == 0 ? own : deep;
        into.insert(pc);
        if (sfe::Sh2Decode(pc, s.WordAt(pc)).HasDelaySlot) into.insert(pc + 2);
    }
}

// The labels the output prints, line by line: "case N:" (the number may follow a cast, when the switch
// variable is a pointer) and "default:".
struct LineLabels
{
    std::vector<long> cases;
    bool isDefault = false;
    bool Any() const { return isDefault || !cases.empty(); }
};

std::vector<LineLabels> ParseLabels(const std::vector<Line>& lines)
{
    std::vector<LineLabels> out(lines.size());
    for (size_t i = 0; i < lines.size(); ++i)
    {
        const std::string& t = lines[i].text;
        if (t.find("default:") != std::string::npos) out[i].isDefault = true;
        size_t at = 0;
        while ((at = t.find("case ", at)) != std::string::npos)
        {
            at += 5;
            size_t from = at;
            if (from < t.size() && t[from] == '(')
            {
                const size_t close = t.find(')', from);
                if (close == std::string::npos) continue;
                from = close + 1;
            }
            char* end = nullptr;
            const long v = std::strtol(t.c_str() + from, &end, 0);
            if (end != t.c_str() + from && *end == ':') out[i].cases.push_back(v);
        }
    }
    return out;
}

// The switch of the output that dispatches on the jump at 'jmpInsn': the line of its "switch" (the one
// whose tokens came from that instruction) and the line after its closing brace.
bool FindSwitch(const std::vector<Line>& lines, uint32_t jmpInsn, size_t& begin, size_t& end)
{
    size_t at = lines.size();
    for (size_t i = 0; i < lines.size() && at == lines.size(); ++i)
        if (lines[i].text.find("switch") != std::string::npos)
            for (const Token& t : lines[i].tokens)
                if (t.addr == jmpInsn) at = i;
    // No switch of this table's own: the engine took it out (every entry the same destination, one case).
    if (at == lines.size()) return false;
    int depth = 0;
    bool opened = false;
    for (size_t k = at; k < lines.size(); ++k)
    {
        for (char c : lines[k].text)
        {
            if (c == '{') { ++depth; opened = true; }
            else if (c == '}') --depth;
        }
        if (opened && depth <= 0)
        {
            begin = at + 1;
            end = k;
            return true;
        }
    }
    begin = at + 1;
    end = lines.size();
    return true;
}

// The label of the switch (lines [begin, end)) that handles index 'v' -- its own "case v:", else the
// "default:" (a bounded switch prints the commonest destination as the default) -- and the addresses of
// the tokens under it, the label's own among them: the label carries the address its code starts at.
// A group runs from the label, past any labels stacked on it, to the next label or the end of the switch.
bool GroupAddrs(const std::vector<Line>& lines, const std::vector<LineLabels>& labels, size_t begin, size_t end, long v,
                std::set<uint32_t>& out)
{
    size_t at = end;
    for (size_t i = begin; i < end && at == end; ++i)
        if (std::find(labels[i].cases.begin(), labels[i].cases.end(), v) != labels[i].cases.end()) at = i;
    for (size_t i = begin; i < end && at == end; ++i)
        if (labels[i].isDefault) at = i;
    if (at == end) return false;
    size_t j = at;
    while (j + 1 < end && labels[j + 1].Any()) ++j;
    for (size_t k = at; k <= j; ++k)
        for (const Token& t : lines[k].tokens)
            if (t.addr) out.insert(t.addr);
    int depth = 0;
    for (size_t k = j + 1; k < end; ++k)
    {
        const std::string& t = lines[k].text;
        if (depth == 0 && labels[k].Any()) break;
        depth += static_cast<int>(std::count(t.begin(), t.end(), '{')) - static_cast<int>(std::count(t.begin(), t.end(), '}'));
        for (const Token& tok : lines[k].tokens)
            if (tok.addr) out.insert(tok.addr);
    }
    return true;
}

// Whether 'word' stands in 'text' as a name of its own (not inside a longer one).
bool WholeWord(const std::string& text, const std::string& word)
{
    auto isName = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; };
    for (size_t at = text.find(word); at != std::string::npos; at = text.find(word, at + 1))
    {
        const size_t end = at + word.size();
        if ((at == 0 || !isName(text[at - 1])) && (end >= text.size() || !isName(text[end]))) return true;
    }
    return false;
}

// What the SE_GENERATED_MUTATE modes do to a result before it is checked: each simulates one defect
// that one family of checks exists to find, so a run can show the check sees it.
std::string gMutateKind;   // "", "ledger", "second", "params", "ret", "body", "cases", "warn", "preserved", "lent", "data", "honour", "kept"
void Mutate(DecompileResult& r, const Subject& s, int fi)
{
    if (gMutateKind.empty() || r.status != DecompileStatus::Ok) return;
    if (gMutateKind == "ledger") { if (!r.accesses.empty()) r.accesses.erase(r.accesses.begin()); }
    else if (gMutateKind == "second")
    {
        // Lose the second of two entries of one instruction (mac's two operands): the first still has
        // the same instruction, direction and size, which must not excuse the access that lost its own.
        for (size_t i = 0; i < r.accesses.size(); ++i)
        {
            bool twin = false;
            for (size_t j = 0; j < r.accesses.size() && !twin; ++j)
                twin = j != i && r.accesses[j].insn == r.accesses[i].insn && r.accesses[j].write == r.accesses[i].write &&
                       r.accesses[j].size == r.accesses[i].size;
            if (!twin) continue;
            r.accesses.erase(r.accesses.begin() + i);
            break;
        }
    }
    else if (gMutateKind == "params") { if (r.prototype.known && !r.prototype.params.empty()) r.prototype.params.pop_back(); }
    else if (gMutateKind == "ret")
    {
        if (r.prototype.known) r.prototype.ret.kind = r.prototype.ret.kind == TypeKind::Void ? TypeKind::Int : TypeKind::Void;
    }
    else if (gMutateKind == "body") { if (!r.body.empty()) r.body.pop_back(); }
    else if (gMutateKind == "cases")
    {
        std::vector<Line> keep;
        for (const Line& l : r.lines)
            if (l.text.find("case ") == std::string::npos) keep.push_back(l);
        r.lines = keep;
    }
    else if (gMutateKind == "warn") { r.flowWarnings.clear(); }
    else if (gMutateKind == "preserved") { if (r.prototype.known) r.prototype.effects.preserved = (1u << 22) - 1; }
    else if (gMutateKind == "lent") { r.prototype.known = true; r.prototypeWithheld.clear(); }
    else if (gMutateKind == "honour")
    {
        // A caller that ignores the effects it was lent reads a kept register as a call's output.
        Line l;
        l.text = "  return extraout_r4 + extraout_r5 + extraout_r6 + extraout_r7 + extraout_macl + extraout_mach + extraout_sr;";
        r.lines.push_back(l);
    }
    else if (gMutateKind == "kept")
    {
        // A caller that ignores that a callee changed r8-r14, GBR or VBR reads them as they were on entry.
        Line l;
        l.text = "  return unaff_r8 + unaff_r9 + unaff_r10 + unaff_r11 + unaff_r12 + unaff_r13 + unaff_r14 + unaff_gbr + unaff_vbr;";
        r.lines.push_back(l);
    }
    else if (gMutateKind == "data")
        for (const FnInfo& f : s.fns)
            if (!f.layout.data.empty()) r.body.push_back(f.layout.data[0]);
    (void)fi;
}

// Whether the function calls one that changes r8-r14, GBR or VBR for good (Func::effClobbers) and was lent no signature:
// the engine's model keeps those registers across every call it knows nothing about.
bool CallsUnlentChanger(const Subject& subject, const std::map<uint32_t, Prototype>& lent, const DecompileResult& r)
{
    constexpr uint32_t kKept = (0x7Fu << 8) | (1u << 17) | (1u << 18);
    for (uint32_t c : r.calledAt)
    {
        const int i = subject.FnAtEntry(c & ~0x20000000u);
        if (i < 0 || !(subject.fns[i].clobbers & kKept)) continue;
        const auto l = lent.find(c);
        if (l == lent.end() || !l->second.known) return true;
    }
    return false;
}

// One decompilation of function 'fi', checked against its runs. 'lent' holds the prototypes of the
// functions already decompiled (the later ones), 'results' their results.
void CheckFn(Sh2Decompiler& d, const std::shared_ptr<MemorySnapshot>& snap, const Subject& subject, int fi,
             const std::map<uint32_t, Prototype>& lent, const std::vector<DecompileResult>& results, bool volatileRun,
             const std::vector<RunOut>& runs, Runner& runner, Report& rep, bool wantText, DecompileResult& r)
{
    const FnInfo& fn = subject.fns[fi];
    DecompileRequest q;
    q.entry = fn.layout.entry;
    if (subject.fns.size() > 1)
        for (size_t j = 0; j < subject.fns.size(); ++j)
            if (static_cast<int>(j) != fi) q.knownFunctions.push_back(subject.fns[j].layout.entry);
    for (const auto& kv : lent) q.prototypes.push_back(kv);
    std::vector<HardwareRange> marked;
    if (volatileRun)
    {
        q.volatileMemory.emplace_back(subject.volFirst, subject.volLast);
        marked = VolatileAliases(subject.volFirst, subject.volLast);
    }
    const std::string mode = volatileRun ? "v:" : "n:";
    const auto t0 = std::chrono::steady_clock::now();
    r = d.Decompile(snap, q);
    runner.Invalidate();
    rep.decompileMs += dts::MsSince(t0);
    // The check checked: SE_GENERATED_MUTATE=<kind> damages the result as one defect would, and the
    // check must then report it (the run passes only if it does).
    Mutate(r, subject, fi);
    auto add = [&](const std::string& sig, const std::string& detail) {
        rep.findings.push_back({ mode + sig, (subject.fns.size() > 1 ? "[function " + std::to_string(fi) + " at " + Hex(fn.layout.entry) + "] " : std::string()) + detail });
    };
    auto count = [&](const std::string& what, int n = 1) { rep.counts[what] += n; };

    // The ground truth: every run's accesses united, and the instructions executed.
    // What the function itself did: a callee's accesses and instructions are the callee's to account for
    // (the same code can be both: a function that calls another and also jumps into it).
    std::set<TraceRec> united;
    std::set<uint32_t> executed, executedDeep;
    int finished = 0;
    for (const RunOut& run : runs)
    {
        for (const TraceRec& t : run.trace)
            if (t.depth == 0) united.insert(t);
        ExecutedOf(subject, run, executed, executedDeep);
        if (run.finished) ++finished;
    }
    const std::vector<TraceRec> trace(united.begin(), united.end());

    if (r.status != DecompileStatus::Ok)
    {
        add("status", "status " + std::to_string(int(r.status)) + ": " + r.error);
    }
    else
    {
        if (r.unsupported) add("unsupported", "unsupported at " + Hex(r.unsupportedAt));
        if (r.unimplemented) add("unimplemented", "the spec has no semantics for an instruction");
        if (r.truncated) add("truncated", "flow left captured memory: " + r.truncatedWhy);
        if (!r.badDataAt.empty()) add("baddata", "bad data at " + Hex(r.badDataAt[0]));

        auto inMarked = [&](uint32_t addr) {
            for (const HardwareRange& h : marked)
                if (h.first <= addr && addr <= h.last) return true;
            return false;
        };
        // ---- Area 5: every access the code made, in an instruction of the body, is in the ledger ----
        // (instruction, direction, size) -> the addresses it touched
        std::map<std::tuple<uint32_t, bool, int>, std::vector<uint32_t>> seen;
        for (const TraceRec& t : trace) seen[std::make_tuple(t.insn, t.write, t.size)].push_back(t.addr);
        // ...and, of those, the accesses the ledger does not account for: per execution of the instruction
        // (a run's step), the entries of its instruction, direction and size are spent one per access.
        std::map<std::tuple<uint32_t, bool, int>, std::vector<uint32_t>> lost;
        for (const RunOut& run : runs)
        {
            // An indirect jump the output says it treats as a call (a table it could not recover, A4-7: a
            // signed bound lets the negative indices through) hands the rest of the run to code the output does
            // not follow: what the run does after the jump's delay slot is not the output's to list, though the
            // instruction may be in the body by another path (the join after the cases).
            int handedOver = INT32_MAX;
            for (size_t k = 0; k < run.pcs.size() && handedOver == INT32_MAX; ++k)
                if (run.depths[k] == 0)
                    for (const FlowWarning& w : r.flowWarnings)
                        if (w.address == run.pcs[k] && w.text.find("Treating indirect jump as call") != std::string::npos)
                            handedOver = static_cast<int>(k) + 2;
            std::map<std::tuple<int, uint32_t, bool, int>, std::vector<uint32_t>> steps;
            for (const TraceRec& t : run.trace)
                if (t.depth == 0 && t.step < handedOver) steps[std::make_tuple(t.step, t.insn, t.write, t.size)].push_back(t.addr);
            for (const auto& st : steps)
            {
                std::vector<const MemoryAccess*> entries;
                for (const MemoryAccess& m : r.accesses)
                    if (m.insn == std::get<1>(st.first) && m.write == std::get<2>(st.first) && m.size == std::get<3>(st.first))
                        entries.push_back(&m);
                std::vector<uint32_t>& out = lost[std::make_tuple(std::get<1>(st.first), std::get<2>(st.first), std::get<3>(st.first))];
                for (uint32_t a : UnaccountedAddresses(entries, st.second, [&](uint32_t x) { return InScratch(x) || InHardwareRange(x) || inMarked(x); }))
                    if (std::find(out.begin(), out.end(), a) == out.end()) out.push_back(a);
            }
        }
        for (const auto& entry : seen)
        {
            const uint32_t insn = std::get<0>(entry.first);
            const bool write = std::get<1>(entry.first);
            const int size = std::get<2>(entry.first);
            if (subject.UnitOf(insn) < 0)
            {
                add("trace-outside", "the emulator accessed memory from " + Hex(insn) + ", outside the function");
                continue;
            }
            if (!r.InBody(insn)) continue;   // another function's instruction, or one flow missed (below)
            // A PC-relative load (mov.l or mov.w @(disp,pc)): a literal-pool word, folded by design.
            const uint16_t word = subject.WordAt(insn);
            if (!write && ((word & 0xF000) == 0xD000 || (word & 0xF000) == 0x9000)) continue;
            // Which of the accesses no ledger entry stands for, one entry to each, execution by execution.
            const std::vector<uint32_t>& addrs = lost[entry.first];
            if (addrs.empty()) continue;

            // A local: the stack is the function's own, a variable (A11-1). Only an access through r15
            // or the frame pointer is one; a pointer into the stack handed to a callee is listed.
            bool allStack = true;
            for (uint32_t addr : addrs)
                if (!InStack(addr)) allStack = false;
            if (allStack && IsLocalAccess(word))
            {
                count("stack.exempt");
                continue;
            }

            // Ordinary memory at a constant address may be folded into a variable: the address is
            // neither hardware nor marked, and not through a parameter (the pointers that are
            // parameters point into the scratch area; every other address came from a literal). The
            // pointer's other possible values, on paths that never ran, do not count: the
            // decompiler may know they cannot.
            bool exempt = true;
            std::set<std::string> from;
            for (uint32_t addr : addrs)
            {
                if (InScratch(addr)) from.insert("param");
                else if (InHardwareRange(addr)) from.insert("hardware");
                else if (inMarked(addr)) from.insert("marked");
                else from.insert("RAM");
                if (InScratch(addr) || InHardwareRange(addr) || inMarked(addr)) exempt = false;
            }
            if (exempt) continue;
            if (SaidMissing(r, insn)) continue;
            // A call to a function that was not lent a signature leaves r8-r14, GBR and VBR as the calling convention has
            // them, kept, however much its code changes them (the model's, and A3-3's limit for a callee not decompiled
            // first): a branch on a register such a callee changed is folded as if it kept it, and the code it guards can
            // be removed as unreachable. The conventions grammar makes such callees, and the wide alphabet's ldc / lds make
            // more of them (seed 301832). Not a finding of the flow; counted.
            if (CallsUnlentChanger(subject, lent, r))
            {
                count("dropped.after-unlent-changer");
                continue;
            }

            std::string provenance;
            for (const std::string& f : from) provenance += (provenance.empty() ? "" : "+") + f;
            // How the engine says the access ended (patch 0017's record), for the finding's label.
            std::string fate;
            for (const AccessFate& f : r.accessFates)
                if (f.insn == insn && f.write == write && f.size == size)
                    fate = f.fate == AccessFate::Unreachable ? "removed-unsaid" : f.fate == AccessFate::Dead ? "dead" :
                           f.fate == AccessFate::FoldedOrdinary ? "folded" : "switch-lookup";
            add(std::string("dropped:") + (write ? "W" : "R") + std::to_string(size) + ":" + provenance + (fate.empty() ? "" : ":" + fate),
                std::string("the ") + (write ? "write" : "read") + " of " + std::to_string(size) + " byte(s) at " + Hex(insn) + " (" +
                    Hex(addrs[0]) + (addrs.size() > 1 ? ", ..." : "") + "; " + provenance + ") has no access in the output" +
                    (fate.empty() ? std::string("; the engine has no record of it") : "; the engine's record says: " + fate));
        }
        // ---- Area 5, round 4 (A5-19): a read of a register through the cached partition is a cache read ----
        // The emulator below has no cache, so a cached-partition register literal behaves like its 0x2xxxxxxx twin in the
        // trace; Mednafen does not: with CCR.CE a hit reads nothing and a miss reads the whole line. The decompiler must
        // say so on the instruction (a caution "... a cache read ..."), whichever way the literal was written.
        for (const TraceRec& t : trace)
        {
            uint32_t canon = 0;
            if (t.write || t.addr >= 0x20000000u || !r.InBody(t.insn) || !HardwareAlias(t.addr, canon) || canon >= 0xFFFFFE00u) continue;
            const uint16_t word = subject.WordAt(t.insn);
            if ((word & 0xF000) == 0xD000 || (word & 0xF000) == 0x9000 || SaidMissing(r, t.insn)) continue;
            // An access the output shows at that constant address, or a read through a pointer the output derives from a hardware base
            // (PointerPrintsFromHardware): either must be said. A pointer it does not (a parameter, a loaded or computed address, a
            // callee's output) is a plain load (patches/0009) with no register for a note to be about, and is excused.
            bool shown = false;
            for (const MemoryAccess& m : r.accesses) shown |= !m.write && m.insn == t.insn && m.direct && m.address == t.addr && m.size == t.size;
            if (!shown)
            {
                bool unrelated = false;
                if (!PointerPrintsFromHardware(r, t.insn, HardwareRegistersCovering(t.addr, t.size), unrelated))
                {
                    count(unrelated ? "cache.read.pointer.unrelated" : "cache.read.anonymous");
                    continue;
                }
            }
            bool said = false;
            for (const HardwareNote& n : r.hardwareNotes)
                said |= n.address == t.insn && n.caution && n.text.find("cache read") != std::string::npos;
            count(std::string(shown ? "cache.read." : "cache.read.pointer.") + (said ? "said" : "unsaid"));
            if (!said)
                add(shown ? "cache-read-unsaid" : "cache-read-pointer-unsaid",
                    "the read of " + std::to_string(t.size) + " byte(s) at " + Hex(t.insn) + " (" + Hex(t.addr) + ", the cached partition)" +
                        (shown ? std::string() : std::string(" through a pointer the output derives from a register address")) + " is not called a cache read");
        }
        for (const MemoryAccess& m : r.accesses)
        {
            if (m.printed != 1) add("printed:" + std::to_string(m.printed), "printed " + std::to_string(m.printed) + " time(s): " + AccessText(m));
            if (m.repeatedOnPath) add("repeated", "made twice on a path: " + AccessText(m));
            if (m.conditionalInPrint) add("conditional-in-print", "printed only on some paths: " + AccessText(m));
        }
        // No two reads in one statement, the accesses in an order the code can make them in, no
        // switch block joined across a branch, no read printed past another access or a call.
        std::vector<MemoryAccess> got = r.accesses;
        std::stable_sort(got.begin(), got.end(), [](const MemoryAccess& x, const MemoryAccess& y) {
            return x.insn != y.insn ? x.insn < y.insn : x.order < y.order;
        });
        const FlowReach fr(r.flowSuccessors);
        // The rule that two accesses joined across a branch are not made together reports only what a run
        // shows: a branch whose condition is constant (a folded test) joins them rightly.
        auto Ran = [](const RunOut& run, uint32_t insn) {
            for (size_t k = 0; k < run.pcs.size(); ++k)
                if (run.pcs[k] == insn && run.depths[k] == 0) return true;
            return false;
        };
        auto separatedByRuns = [&](uint32_t x, uint32_t y) {
            for (const RunOut& run : runs)
            {
                const bool hasX = Ran(run, x), hasY = Ran(run, y);
                if (hasX != hasY) return true;
            }
            return false;
        };
        for (size_t i = 0; i < got.size(); ++i)
            for (size_t j = i + 1; j < got.size(); ++j)
            {
                const MemoryAccess& x = got[i];
                const MemoryAccess& y = got[j];
                if (Unordered(x, y)) add("same-statement", Hex(x.insn) + " and " + Hex(y.insn) + " are read in one statement");
                else if (OutOfFlowOrder(fr, x, y))
                    add("out-of-order", Hex(x.insn) + " and " + Hex(y.insn) + " are out of the code's order in block " + std::to_string(x.block));
                else if (FoldedIntoSwitch(fr, x, y) && separatedByRuns(x.insn, y.insn))
                    add("folded-into-switch", Hex(x.insn) + " and " + Hex(y.insn) + " were joined into a switch's block across a branch" +
                                                  ", and a run made one without the other");
            }
        for (const MemoryAccess& m : got)
        {
            std::string what;
            if (PrintedPast(m, got, r.calls, &what)) add("printed-past", "the read at " + Hex(m.insn) + " is printed past " + what);
        }

        // ---- Area 4: control flow and boundaries ----
        // The functions this one calls, transitively, as the results name them: their code runs inside
        // this function's run, and is theirs to account for.
        std::set<int> calls;
        {
            std::vector<int> todo;
            auto addCallees = [&](const DecompileResult& res) {
                for (uint32_t c : res.callees)
                {
                    const int j = subject.FnAtEntry(c);
                    if (j >= 0 && j != fi && !calls.count(j))
                    {
                        calls.insert(j);
                        todo.push_back(j);
                    }
                }
            };
            addCallees(r);
            while (!todo.empty())
            {
                const int j = todo.back();
                todo.pop_back();
                if (static_cast<size_t>(j) < results.size() && results[j].status == DecompileStatus::Ok) addCallees(results[j]);
            }
        }
        // A jump the output warns of as a tail call, or the engine of as an indirect jump it treats as a call
        // (a table whose guard admits one index, read as a pointer in RAM), hands the rest of the run to code
        // that is not part of this function: the warning says so, and names where when it knows. What a run
        // executes after it is named by that warning, unless another run executed the same instruction
        // before any such jump (A4-5: the design is a call and a return, not flow into the destination).
        std::set<uint32_t> afterTail, beforeTail;
        {
            auto addPc = [&](std::set<uint32_t>& into, uint32_t pc) {
                into.insert(pc);
                if (sfe::Sh2Decode(pc, subject.WordAt(pc)).HasDelaySlot) into.insert(pc + 2);
            };
            for (const RunOut& run : runs)
            {
                size_t cut = run.pcs.size();
                for (size_t k = 0; k < run.pcs.size() && cut == run.pcs.size(); ++k)
                    if (run.depths[k] == 0)
                        for (const FlowWarning& w : r.flowWarnings)
                            if (w.address == run.pcs[k] && (w.kind == FlowWarning::TailJump || w.text.find("Treating indirect jump as call") != std::string::npos))
                                cut = k + 1;
                for (size_t k = 0; k < run.pcs.size(); ++k) addPc(k < cut ? beforeTail : afterTail, run.pcs[k]);
            }
        }
        std::set<uint32_t> outsideBody;   // executed, in no part of this function's body and in no function it calls
        std::set<uint32_t> everything = executed;
        everything.insert(executedDeep.begin(), executedDeep.end());
        for (uint32_t pc : everything)
        {
            if (subject.UnitOf(pc) < 0)
            {
                add("trace-outside", "the emulator executed " + Hex(pc) + ", outside every function");
                continue;
            }
            if (r.InBody(pc)) continue;
            const int u = subject.UnitOf(pc);

            // ...including the code a callee runs on into.
            bool callees = false;
            for (int j : calls)
                if (u == j || (static_cast<size_t>(j) < results.size() && results[j].status == DecompileStatus::Ok && results[j].InBody(pc))) callees = true;
            if (u != fi && callees) continue;
            outsideBody.insert(pc);
            bool named = false;
            for (const FlowWarning& w : r.flowWarnings)
                if (w.address == pc) named = true;
            if (named)
            {
                count("flow.outside-but-named");
                continue;
            }
            if (afterTail.count(pc) && !beforeTail.count(pc))
            {
                count("flow.outside-after-tail-jump");
                continue;
            }
            add("outside-body", "the instruction at " + Hex(pc) + " (function " + std::to_string(u) + ") was executed and is not in the body" +
                                    " and no flow warning names it");
        }
        count("flow.executed", static_cast<int>(executed.size()));
        // Another function's entry inside this body is flow that ran into it: said, never absorbed.
        for (size_t j = 0; j < subject.fns.size(); ++j)
        {
            if (static_cast<int>(j) == fi) continue;
            const uint32_t e = subject.fns[j].layout.entry;
            if (!r.InBody(e)) continue;
            bool said = false;
            for (const FlowWarning& w : r.flowWarnings)
                if (w.kind == FlowWarning::IntoFunction && w.address == e) said = true;
            count(said ? "flow.into-said" : "flow.into-silent");
            if (!said) add("into-function", "the body holds the entry of function " + std::to_string(j) + " at " + Hex(e) + " and no warning says flow runs into it");
        }
        // Data between code, skipped by a bra, is not code.
        for (const FnInfo& f : subject.fns)
            for (const std::pair<uint32_t, uint32_t>& range : f.layout.data)
                for (uint32_t a = range.first; a < range.second; a += 2)
                    if (r.InBody(a))
                    {
                        add("data-in-body", "the body holds " + Hex(a) + ", a data word skipped by a bra");
                        break;
                    }
        // A jump table: every destination the run took is a case the output prints, or a warning says
        // cases may be missing.
        bool tableWarned = false;
        for (const FlowWarning& w : r.flowWarnings)
            if (w.kind == FlowWarning::JumpTable || w.text.find("cases may be missing") != std::string::npos ||
                w.text.find("umptable") != std::string::npos)
                tableWarned = true;
        const std::vector<LineLabels> labels = ParseLabels(r.lines);
        bool tableLost = false;
        for (const TableLayout& tl : fn.layout.tables)
        {
            std::map<int, uint32_t> byIndex;   // the index the run dispatched with -> where it went
            bool defaultPath = false;
            for (const RunOut& run : runs)
                for (size_t k = 0; k + 1 < run.pcs.size(); ++k)
                    if (run.pcs[k] == tl.jmpInsn && run.depths[k] == 0)
                    {
                        if (run.selValue >= 0 && run.selValue < tl.n) byIndex[run.selValue] = run.pcs[k + 1];
                        else if (run.selValue == -1 && tl.hasPre) byIndex[-1] = run.pcs[k + 1];   // the word before the table
                        else defaultPath = true;
                    }
            if (defaultPath) count("table.out-of-range-index");
            for (const auto& dest : byIndex)
            {
                count("table.destination");
                // The code the index runs, and whether it does anything the ledger lists: a case that
                // does nothing may be merged into another or the default.
                std::pair<uint32_t, uint32_t> range = std::make_pair(dest.second, dest.second + 2);
                if (dest.second == tl.defaultAddr) range = tl.defaultRange;
                for (size_t k = 0; k < tl.entry.size(); ++k)
                    if (tl.entry[k] == dest.second && k < tl.range.size()) range = tl.range[k];
                if (dest.first == -1 && tl.hasPre && tl.preEntry == dest.second) range = tl.preRange;
                bool observable = false;
                for (const MemoryAccess& m : r.accesses)
                    if (range.first <= m.insn && m.insn < range.second) observable = true;
                if (!observable)
                {
                    count("table.inert");
                    continue;
                }
                // The label that handles this index holds that code: its case, or the default of a
                // switch whose bound check was taken out of it. With no switch printed at all, the code
                // only has to be there.
                std::set<uint32_t> group;
                bool printed = false;
                size_t swBegin = 0, swEnd = 0;
                if (FindSwitch(r.lines, tl.jmpInsn, swBegin, swEnd))
                {
                    if (GroupAddrs(r.lines, labels, swBegin, swEnd, dest.first, group))
                    {
                        for (uint32_t a : group)
                            if (range.first <= a && a < range.second) printed = true;
                    }
                    else
                    {
                        // No label for it and no default: the index leaves the switch, and its code is
                        // what follows (the join).
                        for (size_t k = swEnd; k < r.lines.size(); ++k)
                            for (const Token& t : r.lines[k].tokens)
                                if (range.first <= t.addr && t.addr < range.second) printed = true;
                    }
                }
                else
                    for (const Line& l : r.lines)
                        for (const Token& t : l.tokens)
                            if (range.first <= t.addr && t.addr < range.second) printed = true;
                if (printed) count("table.printed");
                else if (tableWarned) count("table.warned");
                else
                {
                    tableLost = true;
                    add("table-case", "the jump table's index " + std::to_string(dest.first) + " goes to " + Hex(dest.second) +
                                          ", which does something the ledger lists, and the output's case for it does not hold that code" +
                                          ", and no warning says cases may be missing");
                }
            }
        }

        // ---- Area 3: the prototype this function lends ----
        const Prototype& pr = r.prototype;
        if (pr.known != r.prototypeWithheld.empty())
            add("lent-flag", std::string("prototype.known is ") + (pr.known ? "true" : "false") + " and prototypeWithheld is " +
                                 (r.prototypeWithheld.empty() ? "empty" : "\"" + r.prototypeWithheld + "\""));
        if (pr.known)
        {
            count("proto.lent");
            // Incomplete analysis: something the code did that the analysis cannot have seen or said.
            std::string why;
            if (r.truncated) why = "truncated";
            else if (r.unsupported) why = "unsupported";
            else if (r.unimplemented) why = "unimplemented";
            else if (!r.badDataAt.empty()) why = "bad-data";
            else if (!outsideBody.empty()) why = "executed-outside-body";
            else if (tableLost) why = "table-case-missing";
            else
                for (const FlowWarning& w : r.flowWarnings)
                    if (w.text.find("cases may be missing") != std::string::npos) why = "cases-may-be-missing";
            if (!why.empty()) add("lent-incomplete:" + why, "the prototype is lent (known) although the analysis is incomplete: " + why);

            // Arguments: every register of r4-r7 the function reads before writing, and whose value
            // matters, is a parameter. (A parameter list reaches r_k only by holding k-3 entries.)
            uint32_t readFirst = 0;
            for (const RunOut& run : runs)
                if (run.finished) readFirst |= run.use.readFirst;
            const bool compareR0 = pr.ret.kind != TypeKind::Void;
            for (int k = 4; k < 8; ++k)
            {
                if (!((readFirst >> k) & 1u) || static_cast<size_t>(k - 3) <= pr.params.size()) continue;
                count("param.candidate");
                bool matters = false;
                for (const RunOut& base : runs)
                {
                    if (!base.finished || !((base.use.readFirst >> k) & 1u) || matters) continue;
                    // +0x200 and 0, and (A3-12) each of the other argument registers' values: the registers start at
                    // four distinct pointers, so without it a compare of two of them (`cmp/eq r6,r7`) always has the same answer
                    // and a function that decides on it reads as insensitive to both.
                    std::vector<uint32_t> values = { base.init[k] + 0x200u, 0u };
                    for (int j = 4; j < 8; ++j)
                        if (j != k && base.init[j] != base.init[k]) values.push_back(base.init[j]);
                    for (uint32_t value : values)
                    {
                        RunSpec rs;
                        rs.mode = base.mode;
                        rs.volatileRun = volatileRun;
                        if (base.selValue != RunOut::kNoSel && fn.sel >= 4)
                        {
                            rs.selReg = fn.sel;
                            rs.selValue = static_cast<uint32_t>(base.selValue);
                        }
                        rs.perturbReg = k;
                        rs.perturbValue = value;
                        const RunOut other = runner.One(fn, rs);
                        count("param.perturbed-runs");
                        if (other.addressError)
                        {
                            count("excluded.address-error.perturbed-runs");   // a run of no model: it says nothing about the parameter
                            continue;
                        }
                        if (!SameBehaviour(base, other, compareR0, marked))
                        {
                            matters = true;
                            break;
                        }
                    }
                }
                if (matters)
                    add("param-dropped:r" + std::to_string(k), "the function reads r" + std::to_string(k) + " before writing it and its value changes what the" +
                                                                    " function does, but the lent prototype has " + std::to_string(pr.params.size()) + " parameter(s)");
                else count("param.insensitive");
            }
            count("param.checked");
            // The same for every other register a function can read for its value on entry (round 3, A3-5):
            // r0-r3, r8-r14, GBR, VBR, MACH and MACL. No parameter list holds one, so a lent prototype is wrong
            // if what the function does depends on any of them. r0 is compared too when the function writes it
            // on every path, whatever the prototype returns: a return the engine dropped (`rts ; sts macl,r0`
            // printed as `return;`) is exactly a void prototype over a changing r0.
            {
                bool anyFinished = false, r0Always = true;
                for (const RunOut& run : runs)
                    if (run.finished)
                    {
                        anyFinished = true;
                        if (!(run.use.written & 1u)) r0Always = false;
                    }
                static const int kInputs[] = { 0, 1, 2, 3, 8, 9, 10, 11, 12, 13, 14, 17, 18, 19, 20 };
                static const char* const kStateNames[4] = { "gbr", "vbr", "mach", "macl" };
                for (int k : kInputs)
                {
                    if (!((readFirst >> k) & 1u)) continue;
                    count("input.candidate");
                    bool matters = false;
                    for (const RunOut& base : runs)
                    {
                        if (!base.finished || !((base.use.readFirst >> k) & 1u) || matters) continue;
                        const uint32_t start = k < 16 ? base.init[k] : base.initState[k - 17];
                        for (uint32_t value : { start + 0x200u, 0u })
                        {
                            RunSpec rs;
                            rs.mode = base.mode;
                            rs.volatileRun = volatileRun;
                            if (base.selValue >= 0 && fn.sel >= 4)
                            {
                                rs.selReg = fn.sel;
                                rs.selValue = static_cast<uint32_t>(base.selValue);
                            }
                            rs.perturbReg = k;
                            rs.perturbValue = value;
                            const RunOut other = runner.One(fn, rs);
                            count("input.perturbed-runs");
                            if (other.addressError)
                            {
                                count("excluded.address-error.perturbed-runs");
                                continue;
                            }
                            if (!SameBehaviour(base, other, compareR0 || (anyFinished && r0Always), marked))
                            {
                                matters = true;
                                break;
                            }
                        }
                    }
                    const std::string name = k < 16 ? "r" + std::to_string(k) : kStateNames[k - 17];
                    if (matters)
                        add("input-dropped:" + name, "the function reads " + name + " before writing it and its value changes what the function does" +
                                                         " (or what it returns in r0), but no signature passes it: the prototype is lent");
                    else count("input.insensitive");
                }
                count("input.checked");
            }
            // The return: claimed only if r0 is written on every path.
            bool everUnwritten = false;
            for (const RunOut& run : runs)
                if (run.finished && !(run.use.written & 1u)) everUnwritten = true;
            if (pr.ret.kind != TypeKind::Void)
            {
                count("ret.claimed");
                if (everUnwritten)
                    add("return-invented", "the prototype claims a return, but on some input r0 is never written before the return");
            }
            else count("ret.void");
            if (fn.intent && fn.returns && pr.ret.kind == TypeKind::Void)
                add("return-dropped", "the function writes r0 as its result on every path and the prototype returns nothing");
            // What a call leaves alone: a register the prototype says is preserved is the same at every return.
            for (const RunOut& run : runs)
            {
                if (!run.finished) continue;
                for (int bit = 0; bit < 22; ++bit)
                {
                    if (!((pr.effects.preserved >> bit) & 1u)) continue;
                    uint32_t was = 0, now = 0;
                    std::string name;
                    if (bit < 16) { was = run.init[bit]; now = run.fin[bit]; name = "r" + std::to_string(bit); }
                    else if (bit == 16) { was = run.initSr; now = run.finSr; name = "sr"; }
                    else if (bit >= 17 && bit <= 20)
                    {
                        static const char* const kNames[4] = { "gbr", "vbr", "mach", "macl" };
                        was = run.initState[bit - 17];
                        now = run.finState[bit - 17];
                        name = kNames[bit - 17];
                    }
                    else if (bit == 21) { was = run.initPr; now = run.finPr; name = "pr"; }
                    else continue;
                    if (was != now)
                    {
                        add("preserved-wrong:" + name, "the prototype says " + name + " is preserved and a run ends with " + Hex(now) + " where it started with " + Hex(was));
                        break;
                    }
                }
            }
            count("preserved.checked");
        }
        else
        {
            count("proto.withheld");
            std::string why = r.prototypeWithheld;
            if (why.size() > 40) why = why.substr(0, 40);
            for (char& c : why)
                if (c == ' ') c = '_';
            count("proto.withheld:" + why);
            // A limit of the analysis, not a property of any program the grammar makes (round 5, A3-16: a loop that moves a stack
            // address ran ReturnCheck to its step limit and lost the signature).
            if (r.prototypeWithheld.find("too many paths") != std::string::npos)
                add("lend-too-many-paths", "the signature is withheld: " + r.prototypeWithheld);
        }
        // A lent preserved set is honoured by the caller's text: the registers every callee keeps
        // (CallEffects::preserved of each, among those the default model kills: r2-r7, SR, MACH, MACL)
        // are never a call's output in what this function prints, lent or not itself. Only when every
        // call is to a function that was lent: one that is not, or the function itself, kills them all.
        if (!r.calledAt.empty())
        {
            uint32_t keptByAll = ~0u;
            for (uint32_t c : r.calledAt)
            {
                const auto l = lent.find(c);
                keptByAll &= (l == lent.end() || c == fn.layout.entry) ? 0u : l->second.effects.preserved;
            }
            static const struct { int bit; const char* name; } kKilled[] = { { 2, "r2" }, { 3, "r3" }, { 4, "r4" }, { 5, "r5" }, { 6, "r6" },
                                                                             { 7, "r7" }, { 16, "sr" }, { 19, "mach" }, { 20, "macl" } };
            const std::string text = r.Text();
            for (const auto& k : kKilled)
            {
                if (!((keptByAll >> k.bit) & 1u)) continue;
                count("lent.honoured.checked");
                if (text.find(std::string("extraout_") + k.name) != std::string::npos)
                    add(std::string("effects-ignored:") + k.name, std::string("every function it calls was lent a signature that keeps ") + k.name +
                                                                       ", and the text reads " + k.name + " after a call as the call's output (extraout_" + k.name + ")");
            }
        }

        // The mirror of that (round 3, A3-3): a register the model keeps across a call (r8-r14, GBR, VBR) that
        // a lent callee is known to change is not read as kept. The generator's functions never read their own
        // entry r8-r14, GBR or VBR but on purpose (Func::reads), so the text of one that calls such a callee
        // shows the register's entry value (`unaff_r13`, `in_gbr`) only if a call was taken to leave it alone:
        // after the call it reads the callee's value, which prints as `extraout_r13`. Only when every function it
        // calls was lent a signature (as in check 11): a call to one that was not leaves the register as the
        // calling convention has it, kept, however much the code there changes it.
        bool everyCalleeLent = !r.calledAt.empty();
        for (uint32_t c : r.calledAt)
        {
            const auto l = lent.find(c);
            if (l == lent.end() || c == fn.layout.entry || !l->second.known) everyCalleeLent = false;
        }
        if (everyCalleeLent)
        {
            static const struct { int bit; const char* name; } kKept[] = { { 8, "r8" }, { 9, "r9" }, { 10, "r10" }, { 11, "r11" }, { 12, "r12" },
                                                                           { 13, "r13" }, { 14, "r14" }, { 17, "gbr" }, { 18, "vbr" } };
            const std::string text = r.Text();
            for (const auto& k : kKept)
            {
                if ((fn.reads >> k.bit) & 1u) continue;
                bool changed = false;
                for (uint32_t c : r.calledAt)
                {
                    const auto l = lent.find(c);
                    if (l != lent.end() && c != fn.layout.entry && l->second.known && !((l->second.effects.preserved >> k.bit) & 1u)) changed = true;
                }
                if (!changed) continue;
                count("lent.changed.checked");
                if (WholeWord(text, std::string("unaff_") + k.name) || WholeWord(text, std::string("in_") + k.name))
                    add(std::string("kept-read:") + k.name, std::string("a function it calls was lent a signature that changes ") + k.name +
                                                                ", and the text reads " + k.name + " as the value it had on entry");
            }
        }

        // The generator's own contract: a version 2 function returns, restores what it saved, and ends
        // at the return address. A run that does not is the generator's fault, said so it is fixed.
        if (subject.v2 && fn.intent)
            for (const RunOut& run : runs)
            {
                if (!run.finished)
                {
                    if (subject.polls.empty()) add("generator:no-return", "mode " + std::to_string(run.mode) + " did not return" + (run.error.empty() ? "" : ": " + run.error));
                    continue;
                }
                for (int reg = 8; reg < 16; ++reg)
                    if (run.fin[reg] != run.init[reg] && !((fn.clobbers >> reg) & 1u))
                        add("generator:callee-saved", "r" + std::to_string(reg) + " ends as " + Hex(run.fin[reg]) + ", started as " + Hex(run.init[reg]));
                for (int n = 0; n < 4; ++n)
                    if (run.finState[n] != run.initState[n] && !((fn.clobbers >> (17 + n)) & 1u))
                        add("generator:state-saved", std::string(n == 0 ? "gbr" : n == 1 ? "vbr" : n == 2 ? "mach" : "macl") + " ends as " +
                                                         Hex(run.finState[n]) + ", started as " + Hex(run.initState[n]));
                if (run.finPr != run.initPr) add("generator:pr", "PR ends as " + Hex(run.finPr));
            }
    }

    if (wantText)
    {
        std::ostringstream o;
        o << "  --- function " << fi << " at " << Hex(fn.layout.entry) << ", " << (volatileRun ? "range marked volatile" : "default request") << ": status "
          << int(r.status) << ", " << r.elapsedMs << " ms; " << finished << " of " << runs.size() << " runs finished\n";
        for (const std::string& w : r.warnings) o << "  warning: " << w << "\n";
        for (const HardwareNote& n : r.hardwareNotes) o << "  note " << Hex(n.address) << (n.caution ? " (caution)" : "") << ": " << n.text << "\n";
        if (!r.accessFates.empty())
        {
            o << "  fates:";
            for (const AccessFate& f : r.accessFates)
                o << " " << Hex(f.insn) << (f.write ? "W" : "R") << int(f.size) << "="
                  << (f.fate == AccessFate::Unreachable ? "unreachable" : f.fate == AccessFate::Dead ? "dead" :
                      f.fate == AccessFate::FoldedOrdinary ? "folded" : "switch-lookup");
            o << "\n";
        }
        o << "  prototype: " << (r.prototype.known ? "known, " + std::to_string(r.prototype.params.size()) + " param(s), " +
                                                         (r.prototype.ret.kind == TypeKind::Void ? "void" : "returns") : "withheld: " + r.prototypeWithheld) << "\n";
        o << "  body:";
        for (const auto& b : r.body) o << " " << Hex(b.first) << "-" << Hex(b.second);
        o << "\n  ledger:\n";
        for (const MemoryAccess& m : r.accesses) o << "    " << AccessText(m) << "\n";
        o << "  decompiled:\n";
        static const bool tokens = std::getenv("SE_GENERATED_TOKENS") != nullptr;   // each line with the addresses its tokens came from
        for (const Line& l : r.lines)
        {
            o << "    " << l.text;
            if (tokens)
            {
                std::set<uint32_t> at;
                for (const Token& t : l.tokens)
                    if (t.addr) at.insert(t.addr);
                o << "    //";
                for (uint32_t a : at) o << " " << Hex(a, 4);
            }
            o << "\n";
        }
        rep.text += o.str();
    }
}

// Emulates the subject, decompiles it function by function (the last first, lending what each
// learned to the ones before it, as the panel does), and checks every decompilation against its runs.
// Leaves the runs that made an address error out of the ones the checks are made against (A2-8 (a), A11-2): the model has no
// exceptions, so what such a run does after the access is not what the code does, and the access itself is not one the
// decompiler is to list as made. They are counted, and said in the verbose report; the function is still decompiled and
// checked against the runs that remain (and, if none is left, against its own flow and prototype).
void ExcludeAddressErrors(const Subject& subject, std::vector<RunOut>& runs, Report& rep, std::string& notes, int fi, bool volatileRun)
{
    std::vector<RunOut> kept;
    for (RunOut& run : runs)
    {
        if (!run.addressError)
        {
            kept.push_back(std::move(run));
            continue;
        }
        const TraceRec& t = run.addressErrorAt;
        rep.counts["excluded.address-error.runs"] += 1;
        notes += "  excluded from the checks (A2-8 (a): an address error on the SH-2, not modelled): function " + std::to_string(fi) +
                 (volatileRun ? " with the mailbox marked" : "") + ", input mode " + std::to_string(run.mode) + ": the " +
                 std::to_string(t.size) + "-byte " + (t.write ? "write" : "read") + " at " + Hex(t.addr) + " by " + Hex(t.insn) + " (" +
                 Hex(subject.WordAt(t.insn), 4) + ")\n";
    }
    if (kept.empty() && !runs.empty()) rep.counts["excluded.address-error.functions-without-runs"] += 1;
    runs.swap(kept);
}

Report EvaluateSubject(Sh2Decompiler& d, const Subject& s, bool wantText)
{
    Report rep;
    if (s.bad)
    {
        rep.findings.push_back({ "layout", "the program does not fit (a pool or branch out of reach)" });
        return rep;
    }
    std::vector<uint8_t> hw;
    for (size_t i = 0; i < s.words.size(); ++i) dts::Put16(hw, kCode - 0x06000000u + 2 * i, s.words[i]);
    dts::Put16(hw, kSentinel - 0x06000000u, 0x0009);   // the return address must decode
    dts::Put16(hw, kSentinel - 0x06000000u + 2, 0x0009);
    auto snap = dts::MakeSnapshot(hw);

    // The ground truth: the same code, several runs with different inputs (so more of the branches
    // are taken one way or the other), their traces united. The other processor answers a poll of
    // hardware always, and a poll of memory the request marks volatile; a poll of unmarked work RAM
    // is ordinary memory to the decompiler (it folds), so the run it is compared with has no one
    // answering, and may never leave the loop (the step limit ends it: what follows did not run).
    Runner runner(s, snap);
    std::string engineError;
    std::set<std::tuple<uint32_t, bool, int>> kinds;
    std::string excludedNotes;
    std::vector<std::vector<RunOut>> truth[2];   // per world, per function
    const int worlds = s.markVolatile ? 2 : 1;
    for (int world = 0; world < worlds; ++world)
    {
        const bool volatileRun = world == 1;
        if (volatileRun) ++rep.volatileRuns;
        std::map<uint32_t, Prototype> lent;
        std::vector<DecompileResult> results(s.fns.size());
        truth[world].resize(s.fns.size());
        for (int fi = static_cast<int>(s.fns.size()) - 1; fi >= 0; --fi)
        {
            // Without polls the other processor changes nothing: the second world's runs are the first's.
            if (world == 1 && s.polls.empty()) truth[1][fi] = truth[0][fi];
            else
            {
                truth[world][fi] = runner.Ordinary(s.fns[fi], volatileRun);
                for (const RunOut& run : truth[world][fi])
                {
                    if (!run.finished) ++rep.unfinishedRuns;
                    if (!run.error.empty()) engineError = run.error;
                    rep.counts["runs"] += 1;
                }
                ExcludeAddressErrors(s, truth[world][fi], rep, excludedNotes, static_cast<int>(fi), volatileRun);
            }
            for (const RunOut& run : truth[world][fi])
                for (const TraceRec& t : run.trace) kinds.insert(std::make_tuple(t.insn, t.write, t.size));
            CheckFn(d, snap, s, fi, lent, results, volatileRun, truth[world][fi], runner, rep, wantText, results[fi]);
            if (results[fi].status == DecompileStatus::Ok && results[fi].prototype.known)
                lent[s.fns[fi].layout.entry] = results[fi].prototype;
        }
    }
    rep.traced = kinds.size();
    if (!engineError.empty()) rep.findings.push_back({ "emulator-error", "the emulator stopped: " + engineError });
    if (wantText)
    {
        std::ostringstream o;
        o << SubjectText(s, d, snap);
        for (int world = 0; world < worlds; ++world)
            for (size_t fi = 0; fi < s.fns.size(); ++fi)
            {
                if (world == 1 && truth[1][fi].size() && s.polls.empty()) continue;   // the same runs
                std::set<TraceRec> united;
                for (const RunOut& run : truth[world][fi]) united.insert(run.trace.begin(), run.trace.end());
                o << "  trace of function " << fi << (world ? " with the mailbox marked (the other processor answers it)" : "")
                  << " (instruction, direction, size, address):\n";
                for (const TraceRec& t : united) o << "    " << Hex(t.insn) << " " << (t.write ? "write" : "read ") << " " << t.size << " " << Hex(t.addr) << "\n";
            }
        rep.text = o.str() + excludedNotes + rep.text;
    }
    return rep;
}

Report Evaluate(Sh2Decompiler& d, const Program& p, bool wantText) { return EvaluateSubject(d, MakeSubject(p), wantText); }

// ---- Shrinking -------------------------------------------------------------------------------

bool Has(const Report& r, const std::string& sig)
{
    for (const Finding& f : r.findings)
        if (f.sig == sig) return true;
    return false;
}

// Visits every item of a function, however deep.
void ForEachItem(std::vector<Item>& list, const std::function<void(Item&)>& visit)
{
    for (Item& it : list)
    {
        visit(it);
        ForEachItem(it.slot, visit);
        ForEachItem(it.kids, visit);
        for (std::vector<Item>& c : it.cases) ForEachItem(c, visit);
    }
}
void ForEachItem(Func& f, const std::function<void(Item&)>& visit)
{
    ForEachItem(f.items, visit);
    ForEachItem(f.tail, visit);
    ForEachItem(f.pathA, visit);
    ForEachItem(f.pathB, visit);
}

struct Shrinker
{
    Sh2Decompiler& d;
    Program prog;
    std::string sig;
    int evals = 0;
    int budget = 250;

    bool Still()
    {
        if (evals >= budget) return false;
        ++evals;
        return Has(Evaluate(d, prog, false), sig);
    }

    static bool HasContents(const Item& it)
    {
        return it.type == Item::Skip || it.type == Item::Loop || it.type == Item::Spill || it.type == Item::BraLoop ||
               it.type == Item::BraBack;
    }

    // Try removing each item of 'list', then shrinking inside the ones that stay.
    bool Pass(std::vector<Item>& list)
    {
        bool changed = false;
        for (size_t i = list.size(); i-- > 0;)
        {
            Item saved = list[i];
            list.erase(list.begin() + i);
            if (Still())
            {
                changed = true;
                continue;
            }
            list.insert(list.begin() + i, saved);
            Item& it = list[i];
            if (HasContents(it))
            {
                // Replace the construct by its contents.
                std::vector<Item> kids = it.kids;
                list.erase(list.begin() + i);
                list.insert(list.begin() + i, kids.begin(), kids.end());
                if (Still())
                {
                    changed = true;
                    continue;
                }
                list.erase(list.begin() + i, list.begin() + i + kids.size());
                list.insert(list.begin() + i, saved);
            }
            Item& cur = list[i];
            if (cur.type == Item::Skip && cur.delayed)
            {
                cur.delayed = false;
                cur.shape = cur.onTrue ? "skip.bt" : "skip.bf";
                std::vector<Item> slot = cur.slot;
                cur.slot.clear();
                if (Still()) changed = true;
                else
                {
                    cur.delayed = true;
                    cur.slot = slot;
                    cur.shape = cur.onTrue ? "skip.bt_s" : "skip.bf_s";
                }
            }
            if ((cur.type == Item::Loop || cur.type == Item::BraLoop || cur.type == Item::BraBack) && cur.count > 1)
            {
                const int c = cur.count;
                cur.count = 1;
                if (Still()) changed = true;
                else cur.count = c;
            }
            if (cur.type == Item::Poll && cur.pollWrite)
            {
                cur.pollWrite = false;
                if (Still()) changed = true;
                else cur.pollWrite = true;
            }
            if (cur.type == Item::Call && cur.viaJsr)
            {
                // bsr for jsr: the same call without the literal.
                const bool jsr = cur.viaJsr;
                cur.viaJsr = false;
                if (Still()) changed = true;
                else cur.viaJsr = jsr;
            }
            if (cur.type == Item::Call && !cur.setup.empty())
            {
                std::vector<uint16_t> setup = cur.setup;
                cur.setup.clear();
                if (Still()) changed = true;
                else cur.setup = setup;
            }
            if (cur.type == Item::Table && cur.signedGuard)
            {
                cur.signedGuard = false;
                if (Still()) changed = true;
                else cur.signedGuard = true;
            }
            if (cur.type == Item::Table)
                for (size_t c = 0; c < cur.cases.size(); ++c)
                    if (Pass(list[i].cases[c])) changed = true;
            if (!list[i].kids.empty() && Pass(list[i].kids)) changed = true;
        }
        return changed;
    }

    // A flag of a function set to the value that removes a construct, if the finding survives it.
    bool Try(bool& flag, bool to)
    {
        const bool was = flag;
        if (was == to) return false;
        flag = to;
        if (Still()) return true;
        flag = was;
        return false;
    }

    static bool References(Program& p, int k)
    {
        bool found = false;
        for (size_t i = 0; i < p.fns.size(); ++i)
        {
            Func& f = p.fns[i];
            if (f.end == Func::TailJump && f.tailTo == k) found = true;
            if (f.end == Func::Fall && static_cast<int>(i) + 1 == k) found = true;
            ForEachItem(f, [&](Item& it) {
                if (it.type == Item::Call && it.callee == k) found = true;
            });
        }
        return found;
    }

    // Take function k out, renumbering the ones after it.
    static void Remove(Program& p, int k)
    {
        p.fns.erase(p.fns.begin() + k);
        for (Func& f : p.fns)
        {
            if (f.tailTo > k) --f.tailTo;
            ForEachItem(f, [&](Item& it) {
                if (it.type == Item::Call && it.callee > k) --it.callee;
            });
        }
        for (Lit& l : p.lits)
            if (l.kind == LitKind::Fn && l.fn > k) --l.fn;
    }

    Program Run()
    {
        while (evals < budget)
        {
            bool changed = false;
            for (size_t i = 0; i < prog.fns.size(); ++i)
            {
                Func& f = prog.fns[i];
                if (Pass(prog.fns[i].items)) changed = true;
                if (Pass(prog.fns[i].pathA)) changed = true;
                if (Pass(prog.fns[i].pathB)) changed = true;
                if (!f.tail.empty())
                {
                    std::vector<Item> saved = f.tail;
                    f.tail.clear();
                    if (Still()) changed = true;
                    else f.tail = saved;
                }
                if (f.v2)
                {
                    if (Try(f.shared, false)) changed = true;
                    if (Try(f.epilogueBelow, false)) changed = true;
                    if (Try(f.retInSlot, false)) changed = true;
                    if (Try(f.slotPop, false)) changed = true;
                }
            }
            // A function nothing refers to goes.
            for (int k = static_cast<int>(prog.fns.size()) - 1; k >= 1; --k)
            {
                if (References(prog, k)) continue;
                Program saved = prog;
                Remove(prog, k);
                if (Still()) changed = true;
                else prog = saved;
            }
            if (!changed) break;
        }
        return prog;
    }
};

// ---- Coverage ---------------------------------------------------------------------------------

void CountShapes(const std::vector<Item>& items, std::map<std::string, int>& shapes)
{
    for (const Item& it : items)
    {
        ++shapes[it.shape];
        if (it.type == Item::Mem) ++shapes["width." + std::to_string(it.width)];
        if (it.type == Item::Poll)
        {
            if (it.pollWrite) ++shapes["poll.write_first"];
            if (it.untilSet) ++shapes["poll.until_set"];
        }
        if (it.type == Item::Call)
        {
            if (!it.slotWords.empty()) ++shapes["call.arg_in_slot"];
            if (!it.after.empty()) ++shapes["call.restores"];
            if (it.observes) ++shapes["conv.observe"];
        }
        if (it.type == Item::Table)
        {
            shapes["table.entries"] += static_cast<int>(it.target.size());
            shapes["table.cases"] += static_cast<int>(it.cases.size());
            shapes[it.signedGuard ? (it.useHs ? "table.cmp_ge" : "table.cmp_gt") : it.useHs ? "table.cmp_hs" : "table.cmp_hi"] += 1;
            for (size_t e = 0; e < it.target.size(); ++e)
            {
                if (it.target[e] < 0) ++shapes["table.entry_default"];
                else if (e > 0 && it.target[e] == it.target[e - 1]) ++shapes["table.entry_shared"];
            }
        }
        CountShapes(it.slot, shapes);
        CountShapes(it.kids, shapes);
        for (const std::vector<Item>& c : it.cases) CountShapes(c, shapes);
    }
}

// ---- Opcode coverage: which instructions the generated programs contain ---------------------------
// (SE_GENERATED_COVERAGE=1.) Counted from the items' words, never the literal pools, tables or data words
// that follow the code, plus the instructions the layout itself adds (branches, rts, the stack traffic).
std::string MnemonicOfWord(uint16_t w)
{
    const sfe::DisassembledInstruction ins = sfe::Sh2Decode(kCode, w);
    return ins.IsValid ? ins.Mnemonic : std::string("(invalid)");
}

void CountWords(const std::vector<uint16_t>& words, std::map<std::string, int>& m)
{
    for (uint16_t w : words) ++m[MnemonicOfWord(w)];
}

void CountMnemonics(const std::vector<Item>& items, std::map<std::string, int>& m)
{
    for (const Item& it : items)
    {
        CountWords(it.words, m);
        CountWords(it.setup, m);
        CountWords(it.slotWords, m);
        CountWords(it.after, m);
        switch (it.type)
        {
        case Item::Skip:
            m[it.delayed ? (it.onTrue ? "bt/s" : "bf/s") : (it.onTrue ? "bt" : "bf")] += 1;
            CountWords({ it.cond }, m);
            break;
        case Item::Loop: m["dt"] += 1; m["bf"] += 1; m["mov"] += 1; break;
        case Item::Call: m[it.viaJsr ? "jsr" : "bsr"] += 1; break;
        case Item::Table: m["mova"] += 1; m["shll2"] += 1; m["jmp"] += 1; m["cmp/hi"] += 1; break;
        case Item::BraLoop: case Item::BraBack: m["bra"] += 1; break;
        case Item::DataSkip: m["bra"] += 1; break;
        default: break;
        }
        CountMnemonics(it.slot, m);
        CountMnemonics(it.kids, m);
        for (const std::vector<Item>& c : it.cases) CountMnemonics(c, m);
    }
}

void CountProgramMnemonics(const Program& p, std::map<std::string, int>& m)
{
    for (const Func& f : p.fns)
    {
        CountMnemonics(f.items, m);
        CountMnemonics(f.pathA, m);
        CountMnemonics(f.pathB, m);
        CountMnemonics(f.tail, m);
        CountWords(f.argSave, m);
        CountWords(f.slotA, m);
        CountWords(f.slotB, m);
        m["rts"] += 1;
        if (f.retWord) CountWords({ f.retWord }, m);
        if (f.savePr) { m["sts.l"] += 1; m["lds.l"] += 1; }
        if (f.prReg >= 0) { m["sts"] += 1; m["lds"] += 1; }
        if (!f.pushed.empty()) { m["mov.l"] += 2 * static_cast<int>(f.pushed.size()); }
        if (f.end == Func::TailJump) m["jmp"] += 1;
    }
}

void CountProgram(const Program& p, std::map<std::string, int>& shapes)
{
    ++shapes[p.version == 2 ? "module.v2" : "module.v1"];
    for (const Func& f : p.fns)
    {
        CountShapes(f.items, shapes);
        CountShapes(f.pathA, shapes);
        CountShapes(f.pathB, shapes);
        if (!f.tail.empty()) ++shapes["rts.slot_insn"];
        if (!f.v2) continue;
        ++shapes["fn"];
        ++shapes[f.end == Func::Rts ? "fn.end.rts" : f.end == Func::TailJump ? "fn.end.tail" : "fn.end.fall"];
        if (f.returns) ++shapes["fn.returns"];
        else ++shapes["fn.void"];
        shapes["fn.pushed"] += static_cast<int>(f.pushed.size());
        if (f.savePr) ++shapes["fn.pr.stack"];
        if (f.prReg >= 0) ++shapes["fn.pr.reg"];
        if (f.frame) ++shapes["fn.frame"];
        if (f.fp) ++shapes["fn.fp"];
        if (f.shared) ++shapes["fn.shared_exit"];
        if (f.epilogueBelow) ++shapes["fn.exit_below"];
        if (f.slotPop) ++shapes["fn.slot_pop"];
        if (f.retInSlot) ++shapes["fn.ret_in_slot"];
        if (!f.argSave.empty()) ++shapes["fn.arg_copies"];
    }
    shapes["module.fns"] += static_cast<int>(p.fns.size());
}

long EnvNumber(const char* name, long fallback)
{
    const char* v = std::getenv(name);
    return v && *v ? std::strtol(v, nullptr, 10) : fallback;
}

// "0x40/0x48/0x90,0x90" -> the units of a hand-written module: start/entry/end, each optional.
std::vector<uint32_t> HexList(const std::string& text, char sep)
{
    std::vector<uint32_t> out;
    std::stringstream in(text);
    std::string tok;
    while (std::getline(in, tok, sep)) out.push_back(static_cast<uint32_t>(std::strtoul(tok.c_str(), nullptr, 16)));
    return out;
}

// A hand-written (or pasted) function or module instead of generated ones: hex words from kCode, the
// literal pool among them.
// One function from instruction words, and nothing from the environment (the pins below must not change with it).
Subject WordsSubject(const char* raw)
{
    Subject s;
    std::istringstream in(raw);
    std::string tok;
    while (in >> tok) s.words.push_back(static_cast<uint16_t>(std::strtoul(tok.c_str(), nullptr, 16)));
    s.codeWords = s.words.size();
    FnInfo f;
    f.layout.start = f.layout.entry = kCode;
    f.layout.end = kCode + 2 * static_cast<uint32_t>(s.words.size());
    s.fns.push_back(f);
    return s;
}

Subject RawSubject(const char* raw)
{
    Subject s;
    std::istringstream in(raw);
    std::string tok;
    while (in >> tok) s.words.push_back(static_cast<uint16_t>(std::strtoul(tok.c_str(), nullptr, 16)));
    s.codeWords = s.words.size();
    const uint32_t endAll = kCode + 2 * static_cast<uint32_t>(s.words.size());
    const char* funcs = std::getenv("SE_GENERATED_FUNCS");
    if (funcs && *funcs)
    {
        s.v2 = true;
        std::stringstream fs(funcs);
        std::string unit;
        while (std::getline(fs, unit, ','))
        {
            const std::vector<uint32_t> parts = HexList(unit, '/');
            FnInfo f;
            f.layout.start = kCode + (parts.size() > 0 ? parts[0] : 0);
            f.layout.entry = kCode + (parts.size() > 1 ? parts[1] : parts.size() > 0 ? parts[0] : 0);
            f.layout.end = parts.size() > 2 ? kCode + parts[2] : endAll;
            s.fns.push_back(f);
        }
        if (const char* ret = std::getenv("SE_GENERATED_RETURNS"))
        {
            const std::vector<uint32_t> flags = HexList(ret, ',');
            for (size_t i = 0; i < flags.size() && i < s.fns.size(); ++i)
            {
                s.fns[i].intent = true;
                s.fns[i].returns = flags[i] != 0;
            }
        }
        // The registers each function leaves changed and the ones it reads on purpose, hex masks (bit n: Rn,
        // 17 gbr, 18 vbr, 19 mach, 20 macl), for a module with conventions of its own.
        if (const char* c = std::getenv("SE_GENERATED_CLOBBERS"))
        {
            const std::vector<uint32_t> masks = HexList(c, ',');
            for (size_t i = 0; i < masks.size() && i < s.fns.size(); ++i) s.fns[i].clobbers = masks[i];
        }
        if (const char* c = std::getenv("SE_GENERATED_READS"))
        {
            const std::vector<uint32_t> masks = HexList(c, ',');
            for (size_t i = 0; i < masks.size() && i < s.fns.size(); ++i) s.fns[i].reads = masks[i];
        }
        if (const char* data = std::getenv("SE_GENERATED_DATA"))
        {
            std::stringstream ds(data);
            std::string range;
            while (std::getline(ds, range, ','))
            {
                const std::vector<uint32_t> lohi = HexList(range, '-');
                if (lohi.size() != 2) continue;
                const int u = s.UnitOf(kCode + lohi[0]);
                if (u >= 0) s.fns[u].layout.data.push_back(std::make_pair(kCode + lohi[0], kCode + lohi[1]));
            }
        }
        if (const char* tabs = std::getenv("SE_GENERATED_TABLES"))
        {
            // jmp:sel:n:default:e0,e1,...[:pre]  (the jmp and addresses as hex offsets from the entry, sel and n decimal;
            // pre: where index -1, the word before the table, goes, for a table with a signed bound check)
            std::stringstream ts(tabs);
            std::string t;
            while (std::getline(ts, t, ';'))
            {
                std::stringstream parts(t);
                std::string a, sel, n, def, entries, pre;
                std::getline(parts, a, ':');
                std::getline(parts, sel, ':');
                std::getline(parts, n, ':');
                std::getline(parts, def, ':');
                std::getline(parts, entries, ':');
                std::getline(parts, pre, ':');
                TableLayout tl;
                tl.jmpInsn = kCode + static_cast<uint32_t>(std::strtoul(a.c_str(), nullptr, 16));
                tl.sel = static_cast<int>(std::strtol(sel.c_str(), nullptr, 10));
                tl.n = static_cast<int>(std::strtol(n.c_str(), nullptr, 10));
                tl.defaultAddr = kCode + static_cast<uint32_t>(std::strtoul(def.c_str(), nullptr, 16));
                for (uint32_t e : HexList(entries, ','))
                {
                    tl.entry.push_back(kCode + e);
                    tl.range.push_back(std::make_pair(kCode + e, kCode + e + 2));
                }
                tl.defaultRange = std::make_pair(tl.defaultAddr, tl.defaultAddr + 2);
                if (!pre.empty())
                {
                    tl.hasPre = true;
                    tl.preEntry = kCode + static_cast<uint32_t>(std::strtoul(pre.c_str(), nullptr, 16));
                    tl.preRange = std::make_pair(tl.preEntry, tl.preEntry + 2);
                }
                const int u = s.UnitOf(tl.jmpInsn);
                if (u >= 0)
                {
                    s.fns[u].layout.tables.push_back(tl);
                    s.fns[u].sel = tl.sel;
                }
            }
        }
    }
    else
    {
        FnInfo f;
        f.layout.start = f.layout.entry = kCode;
        f.layout.end = endAll;
        s.fns.push_back(f);
    }
    if (const char* vol = std::getenv("SE_GENERATED_VOLATILE"))
    {
        unsigned long first = 0, last = 0;
        if (std::sscanf(vol, "%lx-%lx", &first, &last) == 2)
        {
            s.markVolatile = true;
            s.volFirst = static_cast<uint32_t>(first);
            s.volLast = static_cast<uint32_t>(last);
        }
    }
    if (const char* poll = std::getenv("SE_GENERATED_POLL"))
    {
        const bool untilSet = std::strstr(poll, ":until-set") != nullptr;
        const uint32_t at = static_cast<uint32_t>(std::strtoul(poll, nullptr, 16));
        s.polls.push_back({ Env::Canon(at), untilSet ? 0u : 1u, untilSet ? 1u : 0u, 0, at });
    }
    return s;
}

// ---- Grammar 3: register-only functions, the printed C against the emulator ----------------------
// The oracle behind A2-12, A2-14 and A2-15 (REVIEW_LEDGER.md): nothing else compared the value a
// function's pseudocode returns with a run of its code. A function of register-only ALU code is
// decompiled; its text is compiled with the system C compiler, with the engine's types and helpers
// (CONCATxy, SUBxy, ZEXTxy, SEXTxy, CARRY4, SCARRY4, SBORROW4 ...) defined as the engine defines them,
// and every input register (in_rN, in_sr, in_mach, in_macl, unaff_rN) given the value the emulator's
// run started with; the C function is called with the same r4-r7 and its value is compared with the
// emulator's r0 (r0 and r1 for an 8-byte return). The emulator is the spec: Sh2DecodeTests holds it
// equal to Mednafen op by op.
//   SE_GENERATED_PRINTEDC=1|only   run it (only: and nothing else; ctest's entry for it sets 'only')
//   SE_GENERATED_PRINTEDC_INPUTS=n inputs per function (default 32: the first 22 walk the edge values through r4-r7)
//   SE_GENERATED_PRINTEDC_PER=n    functions per seed (default 1)
//   SE_GENERATED_PRINTEDC_WORDS="6153 E000 000B 0009;..."  check hand-written functions, text printed
//   SE_C_COMPILER=<cc>             the compiler (default: cc, gcc, clang found on PATH)
// Without a C compiler the run says so loudly and exits 77, which ctest reports as skipped.
// A mismatch on a function whose text declares a 1- or 2-byte variable or parameter is counted
// apart ('narrow-c'): the engine prints sub-int arithmetic at its own width (`(int4)-param_1` with
// `char param_1` negates a byte) and ISO C widens it, so the difference is in the reading, not the
// value (15_DECOMPILER_STEP5.md, "Area 2, round 3: ...", question 1).

std::string gScratch;

struct RegItem
{
    std::vector<uint16_t> w;
    std::string shape;
};

struct RegFn
{
    std::vector<RegItem> items;
    uint16_t slot = 0x0009;   // rts's delay slot
    std::vector<uint16_t> Words() const
    {
        std::vector<uint16_t> out;
        for (const RegItem& i : items) out.insert(out.end(), i.w.begin(), i.w.end());
        out.push_back(0x000B);
        out.push_back(slot);
        return out;
    }
};

uint16_t RegOpWord(Rng& rng, std::string& shape)
{
    const OpDef& op = kRegOps[rng.Below(static_cast<uint32_t>(kRegOpCount))];
    const uint32_t n = rng.Below(8), m = rng.Below(8);
    static const uint32_t kImm[] = { 0, 1, 2, 0x7F, 0x80, 0xFF, 0xFE, 0x10 };
    const uint32_t imm = rng.Chance(60) ? kImm[rng.Below(8)] : rng.Below(256);
    shape = op.name;
    switch (op.form)
    {
    case 'M': return static_cast<uint16_t>(op.base | n << 8 | m << 4);
    case 'N': return static_cast<uint16_t>(op.base | n << 8);
    case 'I': return static_cast<uint16_t>(op.base | imm);
    case 'A': return static_cast<uint16_t>(op.base | n << 8 | imm);
    default: return op.base;
    }
}

RegFn GenRegFn(uint64_t seed, int k)
{
    Rng rng(seed * 7919u + static_cast<uint64_t>(k) + 0x5EEDu);
    RegFn f;
    const int length = 5 + static_cast<int>(rng.Below(16));
    int words = 0;
    // Most functions start by copying their arguments into r0-r3, as compiled code does; the others
    // read r0-r3 as the inputs they are (the text then names them in_r0 .. in_r3).
    if (rng.Chance(75))
    {
        RegItem copies;
        copies.w = { 0x6043, 0x6153, 0x6263, 0x6373 };
        copies.shape = "mov";
        f.items.push_back(copies);
    }
    while (words < length)
    {
        RegItem it;
        const uint32_t r = rng.Below(100);
        std::string shape;
        if (r < 14)   // bt / bf / bt/s / bf/s over one or two instructions
        {
            if (rng.Chance(50))
            {
                it.w.push_back(RegOpWord(rng, shape));   // something to set T
                it.shape = shape + "+";
            }
            const bool delayed = rng.Chance(40), onTrue = rng.Chance(50);
            const uint32_t skipped = 1 + rng.Below(2);
            it.w.push_back(static_cast<uint16_t>((delayed ? (onTrue ? 0x8D00 : 0x8F00) : (onTrue ? 0x8900 : 0x8B00)) | (delayed ? skipped : skipped - 1)));
            if (delayed) it.w.push_back(RegOpWord(rng, shape));
            for (uint32_t i = 0; i < skipped; ++i) it.w.push_back(RegOpWord(rng, shape));
            it.shape += delayed ? (onTrue ? "bt/s" : "bf/s") : (onTrue ? "bt" : "bf");
        }
        else if (r < 16)   // braf over one to three instructions: mov #2n,Rx ; braf Rx ; slot ; n skipped
        {
            const uint32_t x = rng.Below(8), skipped = 1 + rng.Below(3);
            it.w.push_back(static_cast<uint16_t>(0xE000 | x << 8 | 2 * skipped));
            it.w.push_back(static_cast<uint16_t>(0x0023 | x << 8));
            it.w.push_back(RegOpWord(rng, shape));
            for (uint32_t i = 0; i < skipped; ++i) it.w.push_back(RegOpWord(rng, shape));
            it.shape = "braf";
        }
        else
        {
            it.w.push_back(RegOpWord(rng, shape));
            it.shape = shape;
        }
        words += static_cast<int>(it.w.size());
        f.items.push_back(it);
    }
    const uint32_t s = rng.Below(100);
    std::string shape;
    f.slot = s < 70 ? static_cast<uint16_t>(0x6003 | rng.Below(8) << 4) : s < 90 ? RegOpWord(rng, shape) : uint16_t(0x0009);
    return f;
}

// The compiler: SE_C_COMPILER, else the first of cc, gcc, clang that answers --version.
std::string FindCCompiler()
{
    if (const char* env = std::getenv("SE_C_COMPILER"))
        if (*env) return env;
    for (const char* cc : { "cc", "gcc", "clang" })
        if (std::system((std::string(cc) + " --version > /dev/null 2>&1").c_str()) == 0) return cc;
    return std::string();
}

// The names the engine prints for C, as C: sized integer types and the piece operations
// (the engine's own definitions, `ghidra/Ghidra/Features/Decompiler/src/decompile/datatests/` style).
std::string CHeader()
{
    std::ostringstream o;
    o << "#include <stdio.h>\n#include <stdint.h>\n#include <string.h>\n"
         "typedef unsigned char undefined; typedef unsigned char undefined1; typedef unsigned short undefined2;\n"
         "typedef unsigned int undefined3; typedef unsigned int undefined4; typedef unsigned long long undefined8;\n"
         "typedef unsigned char byte; typedef unsigned char uchar; typedef unsigned short ushort; typedef unsigned int uint;\n"
         "typedef unsigned int uint3; typedef long long longlong; typedef unsigned long long ulonglong;\n"
         "typedef unsigned char bool; typedef unsigned short word; typedef unsigned int dword; typedef unsigned long long qword;\n"
         "typedef int int3; typedef signed char int1; typedef short int2; typedef int int4; typedef long long int8;\n"
         "typedef unsigned char uint1; typedef unsigned short uint2; typedef unsigned int uint4; typedef unsigned long long uint8;\n"
         "typedef unsigned char xunknown1; typedef unsigned short xunknown2; typedef unsigned int xunknown4; typedef unsigned long long xunknown8;\n"
         "#define true 1\n#define false 0\n"
         "#define CARRY4(a, b) ((uint32_t)((uint32_t)(a) + (uint32_t)(b)) < (uint32_t)(a))\n"
         "#define CARRY1(a, b) ((uint8_t)((uint8_t)(a) + (uint8_t)(b)) < (uint8_t)(a))\n"
         "#define SCARRY4(a, b) (((int32_t)(a) >= 0) == ((int32_t)(b) >= 0) && (((int32_t)((uint32_t)(a) + (uint32_t)(b)) >= 0) != ((int32_t)(a) >= 0)))\n"
         "#define SCARRY1(a, b) (((int8_t)(a) >= 0) == ((int8_t)(b) >= 0) && (((int8_t)((uint8_t)(a) + (uint8_t)(b)) >= 0) != ((int8_t)(a) >= 0)))\n"
         "#define SBORROW4(a, b) (((int32_t)(a) >= 0) != ((int32_t)(b) >= 0) && (((int32_t)((uint32_t)(a) - (uint32_t)(b)) >= 0) != ((int32_t)(a) >= 0)))\n"
         "#define SBORROW1(a, b) (((int8_t)(a) >= 0) != ((int8_t)(b) >= 0) && (((int8_t)((uint8_t)(a) - (uint8_t)(b)) >= 0) != ((int8_t)(a) >= 0)))\n";
    auto mask = [](int bytes) -> std::string {
        char b[32];
        std::snprintf(b, sizeof b, "0x%sULL", std::string(static_cast<size_t>(bytes) * 2, 'F').c_str());
        return b;
    };
    for (int a = 1; a <= 7; ++a)
        for (int b = 1; a + b <= 8; ++b)
            o << "#define CONCAT" << a << b << "(a, b) ((((unsigned long long)(a) & " << mask(a) << ") << " << 8 * b
              << ") | ((unsigned long long)(b) & " << mask(b) << "))\n";
    for (int a = 2; a <= 8; ++a)
        for (int b = 1; b < a; ++b)
            o << "#define SUB" << a << b << "(v, o) ((((unsigned long long)(v)) >> ((o) * 8)) & " << mask(b) << ")\n";
    for (int a = 1; a <= 7; ++a)
        for (int b = a + 1; b <= 8; ++b)
        {
            o << "#define ZEXT" << a << b << "(v) ((unsigned long long)(v) & " << mask(a) << ")\n";
            o << "#define SEXT" << a << b << "(v) ((long long)(((unsigned long long)(v)) << " << 64 - 8 * a << ") >> " << 64 - 8 * a << ")\n";
        }
    // The control register writes: the user ops record the last value written, to compare with the registers the run ended with.
    o << "static unsigned int g_in[16];\n"
         "static unsigned int g_wsr, g_wgbr, g_wvbr;\n"
         "static void Write_SR(unsigned long long v) { g_wsr = (unsigned int)v; }\n"
         "static void Write_GBR(unsigned long long v) { g_wgbr = (unsigned int)v; }\n"
         "static void Write_VBR(unsigned long long v) { g_wvbr = (unsigned int)v; }\n";
    return o.str();
}

struct CVerdict
{
    std::string cls;      // "" agrees; else the class of the disagreement
    std::string detail;
    std::string text;
    bool compared = false;
    bool voidReturn = false;
    bool narrow = false;
    int mismatches = 0;
};

struct CCase
{
    std::string name;
    std::vector<uint16_t> words;
};

uint32_t CalleeSaved(int r) { return 0xA0000000u + static_cast<uint32_t>(r) * 0x01010101u; }

bool IsFailing(const CVerdict& v)
{
    return !v.cls.empty() && v.cls != "narrow-c" && v.cls != "void-return" && v.cls.compare(0, 8, "skipped:") != 0;
}

// Decompiles each case, runs it on the emulator for 'nIn' inputs, compiles every text together and runs the
// comparison. A text that does not compile alone is told apart by compiling the cases one by one.
std::vector<CVerdict> CheckPrintedC(Sh2Decompiler& d, const std::vector<CCase>& cases, int nIn, const std::string& cc, uint64_t inputSeed,
                                    int batchNo)
{
    struct Prepared
    {
        std::string name, cText, mainText;
        bool ready = false;
    };
    std::vector<CVerdict> out(cases.size());
    std::vector<Prepared> prep(cases.size());
    for (size_t ci = 0; ci < cases.size(); ++ci)
    {
        const CCase& c = cases[ci];
        CVerdict& v = out[ci];
        Prepared& pr = prep[ci];
        pr.name = "f" + std::to_string(ci);
        std::vector<uint8_t> hw;
        for (size_t i = 0; i < c.words.size(); ++i) dts::Put16(hw, kCode - 0x06000000u + 2 * i, c.words[i]);
        dts::Put16(hw, kSentinel - 0x06000000u, 0x0009);
        dts::Put16(hw, kSentinel - 0x06000000u + 2, 0x0009);
        auto snap = dts::MakeSnapshot(hw, 1000 + ci);
        DecompileRequest q;
        q.entry = kCode;
        const DecompileResult r = d.Decompile(snap, q);
        v.text = r.Text();
        if (r.status != DecompileStatus::Ok || r.unsupported || r.truncated)
        {
            v.cls = "undecompiled";
            v.detail = "status " + std::to_string(int(r.status)) + (r.unsupported ? " unsupported" : "") + (r.truncated ? " truncated" : "");
            continue;
        }
        std::string text = v.text;
        if (text.find("unaff_") != std::string::npos || text.find("in_") != std::string::npos)
        {
            // A name made of an address is a varnode outside every register: an input no register holds (A2-15).
            static const std::regex unique("\\b(unaff|in)_[0-9a-f]{6,8}\\b");
            std::smatch m;
            if (std::regex_search(text, m, unique))
            {
                v.cls = "input-outside-registers";
                v.detail = "the text reads '" + m.str() + "', which is no register";
                continue;
            }
        }
        const size_t firstName = text.find("func_0x");
        const bool callsSomething = firstName != std::string::npos && text.find("func_0x", firstName + 1) != std::string::npos;   // a second name: a call
        // The oracle models registers only: a function that reads or writes memory (a pointer parameter, a dereference)
        // is not run (the host's pointers are 64-bit and its memory is not the machine's).
        static const std::regex memoryRe("\\*[A-Za-z_(]");
        if (std::regex_search(text, memoryRe))
        {
            v.cls = "skipped:memory";
            continue;
        }
        if (text.find("halt_baddata") != std::string::npos || text.find("(code *)") != std::string::npos ||
            text.find("extraout_") != std::string::npos || text.find("in_stack") != std::string::npos ||
            callsSomething)
        {
            v.cls = "skipped:call-or-stack";
            continue;
        }
        static const std::regex decl("func_0x[0-9a-fA-F]+");
        text = std::regex_replace(text, decl, pr.name);
        // Every input the text names, as a local holding what the run started with.
        static const std::regex input("^(\\s*)((?:\\w+\\s+)+)((?:in|unaff)_\\w+);\\s*$");
        std::istringstream lines(text);
        std::string line, rebuilt;
        bool unknownInput = false;
        while (std::getline(lines, line))
        {
            std::smatch m;
            // A declaration, not `return in_r1;`.
            if (std::regex_match(line, m, input) && m[2].str().compare(0, 6, "return") != 0 && m[2].str().compare(0, 4, "goto") != 0)
            {
                const std::string name = m[3];
                std::string value;
                int num = -1;
                if (std::sscanf(name.c_str() + (name.compare(0, 3, "in_") == 0 ? 4 : 7), "%d", &num) == 1 && num >= 0 && num <= 15 &&
                    (name == "in_r" + std::to_string(num) || name == "unaff_r" + std::to_string(num)))
                    value = num <= 7 ? "g_in[" + std::to_string(num) + "]" : std::to_string(CalleeSaved(num)) + "u";
                else if (name == "in_sr" || name == "unaff_sr") value = "g_in[8]";
                else if (name == "in_mach" || name == "unaff_mach") value = "g_in[9]";
                else if (name == "in_macl" || name == "unaff_macl") value = "g_in[10]";
                else if (name == "unaff_gbr" || name == "unaff_vbr" || name == "in_gbr" || name == "in_vbr") value = "0";
                else
                {
                    unknownInput = true;
                    v.detail = "the text reads '" + name + "', an input the harness cannot give";
                    value = "0";
                }
                line = m[1].str() + m[2].str() + name + " = " + value + ";";
            }
            rebuilt += line + "\n";
        }
        if (unknownInput)
        {
            v.cls = "skipped:unknown-input";
            continue;
        }
        pr.cText = rebuilt;
        // The signature: the return type and how many parameters.
        const size_t at = pr.cText.find(pr.name + "(");
        if (at == std::string::npos)
        {
            v.cls = "skipped:no-declaration";
            continue;
        }
        const size_t close = pr.cText.find(')', at);
        const std::string plist = pr.cText.substr(at + pr.name.size() + 1, close - at - pr.name.size() - 1);
        int nParams = 0;
        if (plist.find_first_not_of(" \t") != std::string::npos && plist != "void") nParams = 1 + static_cast<int>(std::count(plist.begin(), plist.end(), ','));
        const size_t lineStart = pr.cText.rfind('\n', at);
        const std::string ret = pr.cText.substr(lineStart == std::string::npos ? 0 : lineStart + 1, at - (lineStart == std::string::npos ? 0 : lineStart + 1));
        v.voidReturn = ret.find("void") != std::string::npos;
        static const std::regex narrow("\\b(char|uchar|byte|short|ushort|uint1|int1|uint2|int2|xunknown1|xunknown2|undefined1|undefined2)\\s+\\w+\\s*[;,)=]");
        v.narrow = std::regex_search(pr.cText, narrow);
        // The run: the emulator from the entry for nIn inputs.
        Rng ir(inputSeed ^ (static_cast<uint64_t>(ci) * 0x9E3779B97F4A7C15ull) ^ c.words.size());
        const uint32_t pool[] = { 0, 1, 2, 0xFFFFFFFFu, 0x7FFFFFFFu, 0x80000000u, 0x80000001u, 0xFFFFFFFEu, 0x7FFF, 0x8000, 0xFFFF,
                                  0x10000, 0x7F, 0x80, 0xFF, 0x100, 0xFFFF8000u, 0xFFFFFF80u, 3, 31, 32, 33 };
        const uint32_t poolSize = sizeof pool / sizeof pool[0];
        int sweep = 0;   // the first poolSize inputs walk the edge values through every register (r4 takes pool[k], r5 pool[k + 5] ...)
        auto draw = [&]() -> uint32_t {
            const uint32_t k = ir.Below(4);
            if (k == 0) return pool[ir.Below(poolSize)];
            if (k == 1) return ir.Next() & 0xFFFF;
            return ir.Next();
        };
        Env env;
        env.ram = snap->regions[1].bytes;
        std::unique_ptr<Machine> machine(new Machine(snap, &env));
        std::ostringstream rows;
        int kept = 0;
        bool r0Changed = false;
        for (int k = 0; k < nIn; ++k)
        {
            uint32_t in[11];
            for (int i = 0; i < 8; ++i) in[i] = draw();
            in[8] = draw() & 0x3F3u;
            in[9] = draw();
            in[10] = draw();
            if (sweep < static_cast<int>(poolSize))
            {
                for (int i = 4; i < 8; ++i) in[i] = pool[(sweep + 5 * (i - 4) + (i - 4) * (i - 4)) % poolSize];
                in[0] = pool[(sweep + 11) % poolSize];
                in[1] = pool[(sweep * 3 + 1) % poolSize];
                in[8] = (sweep & 1) ? (in[8] | 1u) : (in[8] & ~1u);   // T both ways
                ++sweep;
            }
            for (int i = 0; i < 8; ++i) machine->Set(("r" + std::to_string(i)).c_str(), in[i]);
            for (int i = 8; i < 15; ++i) machine->Set(("r" + std::to_string(i)).c_str(), CalleeSaved(i));
            machine->Set("r15", kStackTop);
            machine->Set("pr", kSentinel);
            machine->Set("sr", in[8]);
            machine->Set("gbr", 0);
            machine->Set("vbr", 0);
            machine->Set("mach", in[9]);
            machine->Set("macl", in[10]);
            RunOut run;
            env.trace.clear();
            if (!machine->Run(kCode, kSentinel, kStepLimit, run, env)) continue;
            if (run.fin[0] != in[0]) r0Changed = true;
            char buf[256];
            std::snprintf(buf, sizeof buf, "      { 0x%08Xu,0x%08Xu,0x%08Xu,0x%08Xu,0x%08Xu,0x%08Xu,0x%08Xu,0x%08Xu,0x%08Xu,0x%08Xu,0x%08Xu,0x%08Xu,0x%08Xu,0x%08Xu,0x%08Xu,0x%08Xu },\n",
                          in[0], in[1], in[2], in[3], in[4], in[5], in[6], in[7], in[8], in[9], in[10], run.fin[0], run.fin[1], run.finSr,
                          machine->Get("gbr"), machine->Get("vbr"));
            rows << buf;
            ++kept;
        }
        if (!kept)
        {
            v.cls = "skipped:emulator-unfinished";
            continue;
        }
        std::ostringstream m;
        m << "  { /* " << pr.name << " */\n    static const unsigned int D[][16] = {\n" << rows.str() << "    };\n    for (unsigned k = 0; k < " << kept << "; ++k) {\n";
        m << "      memcpy(g_in, D[k], sizeof g_in[0] * 11);\n";
        std::ostringstream call;
        call << pr.name << "(";
        for (int i = 0; i < nParams && i < 4; ++i) call << (i ? ", " : "") << "(int)g_in[" << 4 + i << "]";
        call << ")";
        // The control registers: the last SR, GBR, VBR written through Write_*, or what they were, against the run's.
        // (SR is compared in its S and interrupt-mask bits, which only ldc changes.)
        const std::string ctl = "      if ((g_wsr & 0xF2u) != (D[k][13] & 0xF2u) || g_wgbr != D[k][14] || g_wvbr != D[k][15]) { printf(\"MISMATCH " + pr.name +
            " control registers: pseudo sr=%08X gbr=%08X vbr=%08X, run sr=%08X gbr=%08X vbr=%08X\\n\", g_wsr, g_wgbr, g_wvbr, D[k][13], D[k][14], D[k][15]); ++bad_" +
            pr.name + "; }\n";
        m << "      g_wsr = g_in[8]; g_wgbr = 0; g_wvbr = 0;\n";
        if (v.voidReturn) m << "      " << call.str() << ";\n" << ctl;
        else
        {
            m << "      unsigned long long g64 = (unsigned long long)" << call.str() << ";\n";
            m << "      int rsz = (int)sizeof(" << call.str() << ");\n";
            m << "      unsigned int rmask = rsz >= 4 ? 0xFFFFFFFFu : (1u << (8 * rsz)) - 1;\n";
            m << "      unsigned int got = rsz == 8 ? (unsigned int)(g64 >> 32) : (unsigned int)g64 & rmask;\n";
            m << "      unsigned int want = rsz == 8 ? D[k][11] : D[k][11] & rmask;\n";
            m << "      if (got != want || (rsz == 8 && (unsigned int)g64 != D[k][12])) { printf(\"MISMATCH " << pr.name
              << " r4=%08X r5=%08X r6=%08X r7=%08X size=%d%s run=%08X:%08X pseudo=%08X:%08X\\n\", g_in[4], g_in[5], g_in[6], g_in[7], rsz, "
                 "(rsz == 8 && (unsigned int)g64 == D[k][12]) ? \" high-word-only\" : \"\", D[k][11], D[k][12], "
                 "got, rsz == 8 ? (unsigned int)g64 : 0u); ++bad_" << pr.name << "; }\n";
            m << ctl;
        }
        m << "    }\n  }\n";
        pr.mainText = m.str();
        pr.ready = true;
        v.compared = !v.voidReturn;
        v.detail = std::to_string(kept) + " inputs";
        // A function that returns nothing but whose r0 ended as something other than it began: a return the
        // signature dropped (A3-5, another row's class: counted, not failed here).
        if (v.voidReturn && r0Changed) v.cls = "void-return";
    }
    // Compile and run.
    auto compileRun = [&](const std::vector<size_t>& which, const std::string& tag, std::string& log, bool& compiled) -> std::string {
        std::ostringstream src;
        src << CHeader();
        for (size_t i : which) src << "static int bad_" << prep[i].name << ";\n" << prep[i].cText << "\n";
        src << "int main(void) {\n";
        for (size_t i : which) src << prep[i].mainText;
        int total = 0;
        (void)total;
        src << "  int bad = 0;\n";
        for (size_t i : which) src << "  bad += bad_" << prep[i].name << ";\n";
        src << "  printf(\"BAD %d\\n\", bad);\n  return 0;\n}\n";
        const std::string base = dts::Join(gScratch, "printedc_" + std::to_string(batchNo) + "_" + tag);
        const std::string cfile = base + ".c", bin = base + ".bin", clog = base + ".log", rlog = base + ".out";
        {
            std::ofstream f(cfile);
            f << src.str();
        }
        const std::string compileCmd = cc + " -O0 -fwrapv -fsigned-char -w -std=gnu11 -o " + bin + " " + cfile + " > " + clog + " 2>&1";
        compiled = std::system(compileCmd.c_str()) == 0;
        if (!compiled)
        {
            std::ifstream f(clog);
            std::stringstream ss;
            ss << f.rdbuf();
            log = ss.str();
            return std::string();
        }
        std::system((bin + " > " + rlog + " 2>&1").c_str());
        std::ifstream f(rlog);
        std::stringstream ss;
        ss << f.rdbuf();
        // The program ends with "BAD <n>"; without it the run died (a signal) or never finished.
        if (ss.str().find("\nBAD ") == std::string::npos && ss.str().compare(0, 4, "BAD ") != 0) compiled = false, log = "the compiled program did not finish: " + ss.str().substr(0, 200);
        return ss.str();
    };
    std::vector<size_t> all;
    for (size_t i = 0; i < prep.size(); ++i)
        if (prep[i].ready) all.push_back(i);
    if (all.empty()) return out;
    auto apply = [&](const std::string& run) {
        std::istringstream in(run);
        std::string line;
        while (std::getline(in, line))
        {
            if (line.compare(0, 9, "MISMATCH ") != 0) continue;
            const size_t sp = line.find(' ', 9);
            const std::string name = line.substr(9, sp - 9);
            for (size_t i = 0; i < prep.size(); ++i)
                if (prep[i].ready && prep[i].name == name)
                {
                    CVerdict& v = out[i];
                    if (++v.mismatches == 1) v.detail = line.substr(9);
                    // Only the high word of an 8-byte value differs and the text widens with a C cast ((uint8)x of a signed
                    // x sign-extends in C, the engine means a zero extension): the reading of the cast, not the value.
                    const bool widenOnly = line.find("high-word-only") != std::string::npos &&
                                           (v.text.find("(uint8)") != std::string::npos || v.text.find("(int8)") != std::string::npos);
                    if (v.cls != "mismatch") v.cls = v.narrow || widenOnly ? "narrow-c" : "mismatch";
                }
        }
    };
    std::string log;
    bool compiled = false;
    const std::string run = compileRun(all, "all", log, compiled);
    if (compiled) apply(run);
    else
        for (size_t i : all)   // one at a time: which text is not C, which one dies
        {
            std::string one;
            bool ok = false;
            const std::string r = compileRun({ i }, prep[i].name, one, ok);
            if (ok) apply(r);
            else
            {
                const bool died = one.compare(0, 19, "the compiled progra") == 0;
                out[i].cls = died ? "crashed" : "not-compilable";
                std::istringstream l(one);
                std::string first;
                while (std::getline(l, first))
                    if (died || first.find("error") != std::string::npos) break;
                out[i].detail = first;
            }
        }
    return out;
}

std::string WordsText(const std::vector<uint16_t>& w)
{
    std::string s;
    for (size_t i = 0; i < w.size(); ++i) s += (i ? " " : "") + Hex(w[i], 4);
    return s;
}

// Drops one item at a time while the function still fails the same way.
RegFn ShrinkRegFn(Sh2Decompiler& d, const std::string& cc, RegFn f, const std::string& cls, int nIn)
{
    for (int round = 0; round < 40; ++round)
    {
        std::vector<CCase> cands;
        std::vector<RegFn> fns;
        for (size_t i = 0; i < f.items.size() && f.items.size() > 1; ++i)
        {
            RegFn g = f;
            g.items.erase(g.items.begin() + static_cast<long>(i));
            fns.push_back(g);
        }
        if (f.slot != 0x0009)
        {
            RegFn g = f;
            g.slot = 0x0009;
            fns.push_back(g);
        }
        for (size_t i = 0; i < fns.size(); ++i) cands.push_back({ "s" + std::to_string(i), fns[i].Words() });
        if (cands.empty()) break;
        const std::vector<CVerdict> v = CheckPrintedC(d, cands, nIn, cc, 0x51ED, 9000 + round);
        bool progressed = false;
        for (size_t i = 0; i < v.size(); ++i)
            if (v[i].cls == cls)
            {
                f = fns[i];
                progressed = true;
                break;
            }
        if (!progressed) break;
    }
    return f;
}

// The ledger's fixtures for the printed-C differential: always checked, ahead of the generated functions, so a
// regression of one of these fixes fails the default run whatever the seeds draw (REVIEW_LEDGER.md).
struct PrintedCFixture
{
    const char* name;
    std::vector<uint16_t> words;
};
const PrintedCFixture kPrintedCFixtures[] = {
    { "A2-12 r0 = 0, r1 = r5", { 0x6153, 0xE000, 0x000B, 0x0009 } },
    { "A2-12 r1 = r5 + 1", { 0x6153, 0x7101, 0xE000, 0x000B, 0x0009 } },
    { "A2-12 r0 = 0 on one path", { 0x6153, 0x4501, 0x8D01, 0x4129, 0x324D, 0xE200, 0x000B, 0x6023 } },
    { "A2-12 r0 = 0x11, r1 dirty", { 0xE011, 0x6153, 0x000B, 0x0009 } },
    { "A2-14 addv, known addend", { 0xE501, 0x345F, 0x000B, 0x0029 } },
    { "A2-14 addv, shlr", { 0x4501, 0x345F, 0x000B, 0x0029 } },
    { "A2-14 addv, extu.w", { 0x655D, 0x345F, 0x000B, 0x0029 } },
    { "A2-14 addv, swapped", { 0x4501, 0x354F, 0x000B, 0x0029 } },
    { "A2-15 shlr, rotcl, extu.b", { 0x4401, 0x4424, 0x000B, 0x604C } },
    { "A2-15 control, 32-bit", { 0x4401, 0x4424, 0x000B, 0x6043 } },
    { "A2-13 critical section, no store", { 0x0102, 0x6013, 0xCBF0, 0x400E, 0x6243, 0x410E, 0x000B, 0x0009 } },
    { "A2-13 di", { 0x0002, 0xCBF0, 0x400E, 0x000B, 0x0009 } },
    { "A2-13 ldc r4,vbr", { 0x442E, 0x000B, 0x0009 } },
    { "A2-13 ldc r4,gbr", { 0x441E, 0x000B, 0x0009 } },
    { "A2-13 ldc r4,sr", { 0x440E, 0x000B, 0x0009 } },
    { "dmuls.l ; sts mach,r0 ; sts macl,r1", { 0x354D, 0x000A, 0x011A, 0x000B, 0x0009 } },
    { "64-bit addc chain", { 0x0008, 0x346E, 0x357E, 0x6143, 0x000B, 0x6053 } },
};

void TestPrintedC()
{
    const std::string cc = FindCCompiler();
    if (cc.empty())
    {
        std::printf("SKIPPED: no C compiler found (tried SE_C_COMPILER, cc, gcc, clang). The printed-C differential did NOT run.\n");
        std::fflush(stdout);
        std::_Exit(77);
    }
    std::printf("printed-C differential with '%s'\n", cc.c_str());
    Sh2Decompiler d(gConfig);
    const int nIn = static_cast<int>(EnvNumber("SE_GENERATED_PRINTEDC_INPUTS", 32));
    std::vector<CCase> cases;
    std::vector<RegFn> fns;
    const bool hand = std::getenv("SE_GENERATED_PRINTEDC_WORDS") != nullptr;
    if (hand)
    {
        std::stringstream all(std::getenv("SE_GENERATED_PRINTEDC_WORDS"));
        std::string one;
        int k = 0;
        while (std::getline(all, one, ';'))
        {
            CCase c;
            c.name = "hand" + std::to_string(k++);
            std::istringstream in(one);
            std::string tok;
            while (in >> tok) c.words.push_back(static_cast<uint16_t>(std::strtoul(tok.c_str(), nullptr, 16)));
            cases.push_back(c);
        }
    }
    else
    {
        for (const PrintedCFixture& f : kPrintedCFixtures) cases.push_back({ f.name, f.words });
        fns.resize(cases.size());   // fixtures have no item list (not shrunk)
        const long single = EnvNumber("SE_GENERATED_SEED", 0);
        const long seeds = single ? 1 : EnvNumber("SE_GENERATED_SEEDS", 300);
        const long first = single ? single : EnvNumber("SE_GENERATED_FIRST", 1);
        const long per = EnvNumber("SE_GENERATED_PRINTEDC_PER", 1);
        for (long seed = first; seed < first + seeds; ++seed)
            for (long k = 0; k < per; ++k)
            {
                fns.push_back(GenRegFn(static_cast<uint64_t>(seed), static_cast<int>(k)));
                cases.push_back({ "seed" + std::to_string(seed) + "_" + std::to_string(k), fns.back().Words() });
            }
    }
    if (EnvNumber("SE_GENERATED_COVERAGE", 0) != 0)
    {
        // CountWords is declared below this function's grammar; the register-only words are all code.
        std::map<std::string, int> mn;
        for (const CCase& c : cases)
            for (uint16_t w : c.words) ++mn[sfe::Sh2Decode(kCode, w).Mnemonic];
        std::printf("coverage (register-only grammar): %zu distinct mnemonics over %zu functions:", mn.size(), cases.size());
        for (const auto& k : mn) std::printf(" %s=%d", k.first.c_str(), k.second);
        std::printf("\n");
    }
    const auto t0 = std::chrono::steady_clock::now();
    std::vector<CVerdict> verdicts;
    // Batches of 200, so one text that stalls the compiler costs one batch.
    for (size_t at = 0; at < cases.size(); at += 200)
    {
        const size_t end = std::min(cases.size(), at + 200);
        std::vector<CCase> part(cases.begin() + static_cast<long>(at), cases.begin() + static_cast<long>(end));
        std::vector<CVerdict> v = CheckPrintedC(d, part, nIn, cc, 0xC0DE, static_cast<int>(at / 200));
        verdicts.insert(verdicts.end(), v.begin(), v.end());
    }
    std::map<std::string, int> classes;
    int compared = 0, shrunk = 0;
    const long shrinkCount = EnvNumber("SE_GENERATED_SHRINK", 3);
    for (size_t i = 0; i < verdicts.size(); ++i)
    {
        const CVerdict& v = verdicts[i];
        compared += v.compared ? 1 : 0;
        ++classes[v.cls.empty() ? "agrees" : v.cls];
        if (hand || EnvNumber("SE_GENERATED_VERBOSE", 0) >= 2) std::printf("%s: %s%s\n%s\n", cases[i].name.c_str(), v.cls.empty() ? "agrees" : v.cls.c_str(), v.detail.empty() ? "" : (" (" + v.detail + ")").c_str(), v.text.c_str());
        if (!IsFailing(v))
        {
            // Said, not failed: the classes that are another row's or a limit of the instrument.
            if (!v.cls.empty() && EnvNumber("SE_GENERATED_VERBOSE", 0) != 0)
                std::printf("%s [%s] %s\n  words: %s\n%s\n", cases[i].name.c_str(), v.cls.c_str(), v.detail.c_str(),
                            WordsText(cases[i].words).c_str(), v.text.c_str());
            continue;
        }
        std::printf("FINDING [%s] %s\n  words: %s\n  detail: %s\n", v.cls.c_str(), cases[i].name.c_str(), WordsText(cases[i].words).c_str(), v.detail.c_str());
        std::printf("  replay: SE_GENERATED_PRINTEDC=only SE_GENERATED_PRINTEDC_WORDS=\"%s\"\n", WordsText(cases[i].words).c_str());
        if (!hand && shrunk < shrinkCount && !fns[i].items.empty())
        {
            ++shrunk;
            const RegFn small = ShrinkRegFn(d, cc, fns[i], v.cls, nIn);
            const std::vector<CVerdict> again = CheckPrintedC(d, { { "small", small.Words() } }, nIn, cc, 0x51ED, 8999);
            std::printf("  shrunk: SE_GENERATED_PRINTEDC_WORDS=\"%s\"\n%s\n  detail: %s\n", WordsText(small.Words()).c_str(), again[0].text.c_str(),
                        again[0].detail.c_str());
        }
        else if (!hand)
            std::printf("%s\n", v.text.c_str());
    }
    int failing = 0;
    for (const CVerdict& v : verdicts) failing += IsFailing(v) ? 1 : 0;
    std::printf("printed-C: functions=%zu compared=%d inputs each=%d (%.0f ms) classes:", verdicts.size(), compared, nIn, dts::MsSince(t0));
    for (const auto& c : classes) std::printf(" %s=%d", c.first.c_str(), c.second);
    std::printf("\n");
    CHECK(failing == 0);
}

// A version 2 program that fits: a re-draw when a pool or a branch is out of reach.
Program MakeV2(uint64_t seed)
{
    for (int attempt = 0; attempt < 16; ++attempt)
    {
        Program p = Generate2(seed, attempt);
        if (Assemble(p).ok) return p;
    }
    return Generate2(seed, 0);
}

// The domain of the oracle (ledger A2-8 (a), A11-2). A run that makes a misaligned word or longword access is an address
// error on the SH-2, which the model has no exceptions for: it is counted and left out of the trace-based checks, not run
// as a normal completion; and the generator keeps the programs it makes clear of them. Both are pinned here, in every run.
void TestDomainPins(Sh2Decompiler& d)
{
    struct Pin { const char* name; const char* words; int excludedRuns; bool atLeast; };
    static const Pin kPins[] = {
        // mov.w @r4+,r1 ; mov.l @r4,r2 ; rts ; nop: the longword read is at r4 + 2 in every ordinary run
        { "misaligned long after a post-incremented word", "6145 6242 000B 0009", 3, false },
        // ...and with the increment by a longword, the same function is an ordinary one
        { "control: the same with a longword increment", "6146 6242 000B 0009", 0, false },
        // seed 219488's shape on r1, which is 0 in the first input mode (r4 is a scratch pointer in every run):
        // tst r1,r1 ; bf/s L ; mov.w r2,@-r1 ; mov.l r3,@r1 ; L: rts ; nop. With r1 = 0 the longword store is at 0xFFFFFFFE, an
        // address error on the SH-2 (and a fold the engine used to fail on: A2-11, patch 0031); the other modes' word store is at
        // a random r1 - 2
        { "seed 219488's shape on r1: the run with r1 = 0", "2118 8F01 2125 2132 000B 0009", 1, true },
    };
    for (const Pin& p : kPins)
    {
        const Report rep = EvaluateSubject(d, WordsSubject(p.words), true);
        auto count = [&](const char* k) { auto it = rep.counts.find(k); return it == rep.counts.end() ? 0 : it->second; };
        const int runs = count("excluded.address-error.runs");
        std::printf("pin '%s': excluded runs %d (want %s%d), %zu finding(s)\n", p.name, runs, p.atLeast ? ">= " : "", p.excludedRuns, rep.findings.size());
        for (const Finding& f : rep.findings) std::printf("  [%s] %s\n", f.sig.c_str(), f.detail.c_str());
        CHECK(rep.findings.empty());
        CHECK(p.atLeast ? runs >= p.excludedRuns : runs == p.excludedRuns);
    }
    // Seed 219488 itself (A2-11, fixed by patch 0031; the failing seed of the review's 20,000-seed run), as it was generated
    // before r4/r5 alignment was tracked and as it is now: both decompile (no Failed status, no engine error), and the
    // generator that tracks alignment makes no address error of it.
    const bool tracking = gTrackAlignment;
    for (bool track : { false, true })
    {
        gTrackAlignment = track;
        const Report rep = Evaluate(d, MakeV2(219488), true);
        const int runs = rep.counts.count("excluded.address-error.runs") ? rep.counts.at("excluded.address-error.runs") : 0;
        std::printf("pin 'seed 219488, alignment %s': excluded runs %d, %zu finding(s)\n", track ? "tracked" : "not tracked", runs, rep.findings.size());
        for (const Finding& f : rep.findings) std::printf("  [%s] %s\n", f.sig.c_str(), f.detail.c_str());
        CHECK(rep.findings.empty());
        if (track) CHECK(runs == 0);
    }
    gTrackAlignment = tracking;
}

// A caller and a lent callee that changes a register the calling convention keeps (ledger A3-3, finding 1 of the external
// review at f0684b9): the caller sets the register to 5, calls the leaf that sets it to 7, reads it, and returns it. The text must
// not return the 5 (the oracle never checked the value, only that a return existed), and the oracle's own checks must pass with
// the callee's change declared. Pinned for GBR, VBR and r8 (the caller saving and restoring r8 around the call).
void TestCalleeClobberPins(Sh2Decompiler& d)
{
    struct Case { const char* name; const char* words; uint32_t end0, start1, end1; uint32_t clobbers1; };
    static const Case kCases[] = {
        { "GBR", "4F22 E005 401E B004 0009 0012 4F26 000B 0009 E107 411E 000B 0009", 0x12, 0x12, 0x1A, 1u << 17 },
        { "VBR", "4F22 E005 402E B004 0009 0022 4F26 000B 0009 E107 412E 000B 0009", 0x12, 0x12, 0x1A, 1u << 18 },
        { "r8",  "2F86 4F22 E805 B005 0009 6083 4F26 68F6 000B 0009 000B E807", 0x14, 0x14, 0x18, 1u << 8 },
    };
    for (const Case& c : kCases)
    {
        Subject s;
        std::istringstream in(c.words);
        std::string tok;
        while (in >> tok) s.words.push_back(static_cast<uint16_t>(std::strtoul(tok.c_str(), nullptr, 16)));
        s.codeWords = s.words.size();
        s.v2 = true;
        FnInfo f0, f1;
        f0.layout.start = f0.layout.entry = kCode;
        f0.layout.end = kCode + c.end0;
        f0.intent = f1.intent = true;
        f0.returns = true;
        f0.clobbers = c.clobbers1 & ~(1u << 8) ? c.clobbers1 : 0;   // the r8 caller restores r8; GBR and VBR it leaves changed
        f1.layout.start = f1.layout.entry = kCode + c.start1;
        f1.layout.end = kCode + c.end1;
        f1.clobbers = c.clobbers1;
        s.fns = { f0, f1 };
        const Report rep = EvaluateSubject(d, s, true);
        const bool constant = rep.text.find("return 5;") != std::string::npos;
        const bool unknown = rep.text.find("extraout_") != std::string::npos;
        std::printf("pin 'callee changes %s': %zu finding(s), the caller returns the stale 5: %s, reads the callee's value: %s\n", c.name,
                    rep.findings.size(), constant ? "yes" : "no", unknown ? "yes" : "no");
        for (const Finding& f : rep.findings) std::printf("  [%s] %s\n", f.sig.c_str(), f.detail.c_str());
        CHECK(rep.findings.empty());
        CHECK(!constant);
        CHECK(unknown);
    }
}

void TestGenerated()
{
    Sh2Decompiler d(gConfig);
    if (const char* mutate = std::getenv("SE_GENERATED_MUTATE"))
    {
        gMutateKind = std::strcmp(mutate, "1") == 0 ? "ledger" : mutate;
        gMutate = !gMutateKind.empty() && gMutateKind != "0";
        if (gMutateKind == "0") gMutateKind.clear();
    }
    {
        const char* alphabet = std::getenv("SE_GENERATED_ALPHABET");
        gNarrowAlphabet = alphabet && std::strcmp(alphabet, "narrow") == 0;
        gSpecialReads = alphabet && std::strcmp(alphabet, "specials") == 0;
    }
    if (const char* align = std::getenv("SE_GENERATED_ALIGN")) gTrackAlignment = std::strcmp(align, "off") != 0;
    if (const char* fence = std::getenv("SE_GENERATED_FENCE"))
    {
        gFenceFolds = std::strstr(fence, "folds") != nullptr;
        gFenceSelfOps = gFenceFolds || std::strstr(fence, "selfops") != nullptr;
    }
    if (const char* raw = std::getenv("SE_GENERATED_WORDS"))
    {
        const Subject s = RawSubject(raw);
        const Report rep = EvaluateSubject(d, s, true);
        std::printf("%s", rep.text.c_str());
        for (const Finding& f : rep.findings) std::printf("  [%s] %s\n", f.sig.c_str(), f.detail.c_str());
        std::printf("%zu finding(s)\n", rep.findings.size());
        CHECK(rep.findings.empty());
        return;
    }
    const long single = EnvNumber("SE_GENERATED_SEED", 0);
    if (!single && !gMutate)
    {
        RUN(TestDomainPins(d));
        RUN(TestCalleeClobberPins(d));
    }
    const long seeds = single ? 1 : EnvNumber("SE_GENERATED_SEEDS", 300);
    const long first = single ? single : EnvNumber("SE_GENERATED_FIRST", 1);
    const long shrinkCount = EnvNumber("SE_GENERATED_SHRINK", 3);
    const bool verbose = single || EnvNumber("SE_GENERATED_VERBOSE", 0) != 0;
    const int detailLimit = 3;   // failing seeds printed in full without SE_GENERATED_VERBOSE
    // Grammar 1 is the original (one function, no stack, no calls); grammar 2 is modules. By default
    // the seeds alternate, so the short run covers both inside its time.
    const long grammar = EnvNumber("SE_GENERATED_GRAMMAR", 0);

    std::map<std::string, int> shapes, classes, counts;
    std::map<std::string, int> mnemonics;
    const bool coverage = EnvNumber("SE_GENERATED_COVERAGE", 0) != 0;
    std::vector<long> excludedSeeds;   // seeds with a run left out as an address error (A2-8 (a), A11-2)
    int failing = 0, totalFindings = 0, unfinished = 0, volatileRuns = 0, shrunk = 0, v1seeds = 0, v2seeds = 0;
    size_t traced = 0;
    double decompileMs = 0;
    const auto t0 = std::chrono::steady_clock::now();
    for (long seed = first; seed < first + seeds; ++seed)
    {
        const bool v2 = grammar == 2 || (grammar == 0 && seed % 2 == 0);
        const Program p = v2 ? MakeV2(static_cast<uint64_t>(seed)) : Generate(static_cast<uint64_t>(seed));
        (v2 ? v2seeds : v1seeds) += 1;
        CountProgram(p, shapes);
        if (coverage) CountProgramMnemonics(p, mnemonics);
        const bool detail = verbose || failing < detailLimit;
        Report rep = Evaluate(d, p, detail);
        unfinished += rep.unfinishedRuns;
        volatileRuns += rep.volatileRuns;
        traced += rep.traced;
        decompileMs += rep.decompileMs;
        for (const auto& c : rep.counts) counts[c.first] += c.second;
        {
            auto ex = rep.counts.find("excluded.address-error.runs");
            if (ex != rep.counts.end()) excludedSeeds.push_back(seed);
        }
        if (single) std::printf("%s", rep.text.c_str());
        if (rep.findings.empty()) continue;

        ++failing;
        totalFindings += static_cast<int>(rep.findings.size());
        std::set<std::string> sigs;
        for (const Finding& f : rep.findings) sigs.insert(f.sig);
        for (const std::string& s : sigs) ++classes[s];
        std::printf("FINDING seed %ld (grammar %d):\n", seed, v2 ? 2 : 1);
        for (const Finding& f : rep.findings) std::printf("  [%s] %s\n", f.sig.c_str(), f.detail.c_str());
        if (detail && !single) std::printf("%s", rep.text.c_str());
        if (shrunk < shrinkCount)
        {
            ++shrunk;
            Shrinker s{ d, p, rep.findings[0].sig };
            const Program small = s.Run();
            Report again = Evaluate(d, small, true);
            std::printf("  shrunk from the first finding [%s] in %d evaluations:\n%s", rep.findings[0].sig.c_str(), s.evals,
                        again.text.c_str());
            for (const Finding& f : again.findings) std::printf("  [%s] %s\n", f.sig.c_str(), f.detail.c_str());
        }
    }

    auto n = [&](const char* k) { auto it = shapes.find(k); return it == shapes.end() ? 0 : it->second; };
    auto sum = [&](const char* prefix) {
        int total = 0;
        for (const auto& s : shapes)
            if (s.first.compare(0, std::strlen(prefix), prefix) == 0) total += s.second;
        return total;
    };
    auto c = [&](const char* k) { auto it = counts.find(k); return it == counts.end() ? 0 : it->second; };
    char covered[1536];
    std::snprintf(covered, sizeof covered,
                  "alu=%d loads=%d stores=%d (plain %d disp %d indexed %d inc/dec %d) tas=%d widths b/w/l=%d/%d/%d literal pointers hw/ram/mailbox=%d/%d/%d "
                  "skips=%d (delayed %d) loops=%d polls=%d (write first %d, until set %d; mailbox %d hw %d ram %d) rts slot insn=%d",
                  sum("alu."), sum("mem.ld."), sum("mem.st."), n("mem.ld.plain") + n("mem.st.plain"), n("mem.ld.disp") + n("mem.st.disp"),
                  n("mem.ld.indexed") + n("mem.st.indexed"), n("mem.ld.inc") + n("mem.st.dec"), n("mem.tas"), n("width.1"), n("width.2"),
                  n("width.4"), n("litptr.hw") + n("poll.hw"), n("litptr.ram") + n("poll.ram"), n("litptr.mbox") + n("poll.mbox"), sum("skip."),
                  n("skip.bt_s") + n("skip.bf_s"), n("loop"), sum("poll.h") + sum("poll.r") + sum("poll.m"), n("poll.write_first"),
                  n("poll.until_set"), n("poll.mbox"), n("poll.hw"), n("poll.ram"), n("rts.slot_insn"));
    if (coverage)
    {
        std::printf("coverage: %zu distinct mnemonics over %ld seeds:", mnemonics.size(), seeds);
        for (const auto& k : mnemonics) std::printf(" %s=%d", k.first.c_str(), k.second);
        std::printf("\n");
    }
    if (!excludedSeeds.empty())
    {
        std::printf("seeds with a run excluded as an address error (%zu):", excludedSeeds.size());
        for (size_t i = 0; i < excludedSeeds.size() && i < 400; ++i) std::printf(" %ld", excludedSeeds[i]);
        std::printf("%s\n", excludedSeeds.size() > 400 ? " ..." : "");
    }
    std::printf("finding classes (seeds):");
    for (const auto& cl : classes) std::printf(" %s=%d", cl.first.c_str(), cl.second);
    std::printf("\nshapes:");
    for (const auto& s : shapes) std::printf(" %s=%d", s.first.c_str(), s.second);
    std::printf("\nchecks:");
    for (const auto& k : counts) std::printf(" %s=%d", k.first.c_str(), k.second);
    std::printf("\n");
    const double ms = dts::MsSince(t0);
    std::printf("SUMMARY: seeds=%ld (grammar 1: %d, grammar 2: %d) first=%ld shapes covered: %s; second requests (mailbox marked)=%d; "
                "traced accesses=%zu; unfinished emulator runs=%d; failing seeds=%d; findings=%d (%.0f ms total, %.0f ms in the decompiler, %.1f ms per seed)\n",
                seeds, v1seeds, v2seeds, first, covered, volatileRuns, traced, unfinished, failing, totalFindings, ms, decompileMs, ms / double(seeds));
    if (v2seeds)
    {
        std::printf("SUMMARY2: modules=%d functions=%d (end rts/tail/fall=%d/%d/%d; returning %d, void %d) pushes=%d pr stack/reg=%d/%d frames=%d (frame pointer %d) "
                    "spills=%d frame walks=%d locals ld/st=%d/%d (stack accesses exempt %d) calls bsr/jsr=%d/%d (arg in slot %d, args restored %d) "
                    "tables=%d (entries %d, cases %d, shared entries %d, default entries %d; destinations traced %d, printed %d, inert %d, warned %d) "
                    "bra loops fwd/back=%d/%d data skips=%d shared exits=%d exits below the entry=%d slot pops=%d ret in slot=%d; "
                    "lent prototypes=%d (void %d, returning %d) withheld=%d; parameter checks=%d (candidates %d, perturbed runs %d, insensitive %d); "
                    "preserved checks=%d; flow: instructions executed=%d, into-function said/silent=%d/%d, outside body but named=%d (after a tail jump %d); runs=%d\n",
                    n("module.v2"), n("fn"), n("fn.end.rts"), n("fn.end.tail"), n("fn.end.fall"), n("fn.returns"), n("fn.void"), n("fn.pushed"),
                    n("fn.pr.stack"), n("fn.pr.reg"), n("fn.frame"), n("fn.fp"), n("spill"), n("frame.walk"), n("stack.ld.plain") + n("stack.ld.disp") + n("stack.ld.indexed"),
                    n("stack.st.plain") + n("stack.st.disp") + n("stack.st.indexed"), c("stack.exempt"), n("call.bsr"), n("call.jsr"), n("call.arg_in_slot"),
                    n("call.restores"), n("table"), n("table.entries"), n("table.cases"), n("table.entry_shared"), n("table.entry_default"),
                    c("table.destination"), c("table.printed"), c("table.inert"), c("table.warned"), n("loop.bra"), n("loop.back"), n("bra.data"), n("fn.shared_exit"),
                    n("fn.exit_below"), n("fn.slot_pop"), n("fn.ret_in_slot"), c("proto.lent"), c("ret.void"), c("ret.claimed"), c("proto.withheld"),
                    c("param.checked"), c("param.candidate"), c("param.perturbed-runs"), c("param.insensitive"), c("preserved.checked"),
                    c("flow.executed"), c("flow.into-said"), c("flow.into-silent"), c("flow.outside-but-named"), c("flow.outside-after-tail-jump"), c("runs"));
        std::printf("withheld why:");
        for (const auto& k : counts)
            if (k.first.compare(0, 14, "proto.withheld") == 0 && k.first.size() > 14) std::printf(" %s=%d", k.first.c_str() + 15, k.second);
        std::printf("\n");
    }
    if (gMutate)
    {
        std::printf("mutation check (%s): %d of %ld seeds reported the damage\n", gMutateKind.c_str(), failing, seeds);
        CHECK(failing > 0);
    }
    else CHECK(failing == 0);
}

}  // namespace

int main(int argc, char** argv)
{
    if (argc < 2)
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
    gScratch = scratch;
    try
    {
        const char* printedC = std::getenv("SE_GENERATED_PRINTEDC");
        const bool printedOnly = printedC && std::strcmp(printedC, "only") == 0;
        if (printedC && *printedC && std::strcmp(printedC, "0") != 0) RUN(TestPrintedC());
        if (!printedOnly) RUN(TestGenerated());
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
    if (!std::getenv("SE_GENERATED_KEEP")) dts::RemoveTree(scratch);   // SE_GENERATED_KEEP=1: keep the compiled C files to read
    return dts::Finish("Sh2GeneratedTests", start);
}
