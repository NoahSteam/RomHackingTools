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
//     address, or (three in ten of those, A2-20) bsrf Rn after a load of the distance from the bsrf + 4 to the callee, which is known only after
//     layout; arguments in r4-r7 (the last may be set in the delay slot), kept across calls in
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
// GBR pointing into scratch RAM). SE_GENERATED_ALPHABET=narrow switches that off. Not emitted: trapa and rte (A2-20, accepted: a handler
// returns through rte, not to its caller, so the per-function run from the entry to the sentinel, the depth count of calls and returns, the callee
// closure of check 4 -- the handler is reached through a vector table the decompiler cannot read, an unresolved indirect call -- and the signature
// checks have no meaning for it; the hand fixtures run both against the emulator, TestCallsAndTraps' A2-9 and A2-24/A2-36 in TestArea2Round4 and 5, and
// the decode oracle holds rte and its slot to Mednafen on 5.2 million cases), and, in the first two grammars, stc/sts (ledger A2-16 and A3-5; the
// register-only grammar has them), which SE_GENERATED_ALPHABET=specials lets in: A3-5 and A2-16 are fixed, and the fence stays only so that the
// programs of a seed do not change under the nightly.
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
//      `cache.read.pointer.said` or `...unsaid`. (A5-82) The registers the derivation names must also be where the emulator went, within
//      kPointerReach (60 bytes, the longest displacement of an instruction) of the register reached: a number that is a value (`*p = 0xffffff9d;`
//      is also an on-chip register's address), an assignment the run did not take (a parameter set to `&REG` further down, a callee's output merged
//      with one) or a table case's code the output does not hold are not the pointer, and the text cannot say what the code it lacks did. Counted
//      `cache.read.pointer.unrelated`, and `pointer.effect.unrelated` for rule 3c.
//   2b. (round 7, A5-52) A work-RAM word the traced function reaches under two addresses (the other partition, bits 28-27, the 1 MiB repeat, the
//      associative-purge name of its line), by the emulator's own memory model, and at constant addresses the engine folds, is listed like marked
//      memory: every access to it is in the ledger. The data flow keys memory by address, and would forward a store past a write to the other
//      name (counted `alias.words`; SE_GENERATED_MUTATE=alias checks the check). Grammar 1 also makes a partition mask on a pointer register
//      (`litptr.mask.and` / `.or`, A5-51), which rule 3b holds to the cache caution.
//   3c. (round 9, A5-70) A read or write the emulator traced at a register through a pointer the output derives from register addresses has, on its
//      instruction, the read effect of that register and the width cautions of the access, unless the notes say the pointer may hold other values,
//      is indexed, or is stepped (the registers after the first are then not said, by design). Counted `pointer.effect.derived` (the pointer is the register's), `.said` / `.unsaid` (the effects wanted).
//      Grammar 1 puts a mask (and / or / xor with 0x1FFFFFFF, 0xFFFFFFF0, 0xFFFFFFF9, 0x20000000 or 4) and a step of the pointer's alignment
//      inside a loop body, alone or under a skip (`loop.mask.*`, `loop.step`): the loop then moves the pointer between partitions on some turns,
//      by two operations in a turn, or by a step and a mask.
//   3d. (round 9, A5-73) A store the engine folds in the A-bus cartridge areas (0x02000000-0x04FFFFFF) has the caution that the cartridge decides
//      what a write does, on its instruction (`litptr.cart`; counted `cart.write.said`).
//   2c. (round 9, A5-71, A5-72) The cache data array (partitions 4 and 6, 4 KiB: `litptr.cache`) is one memory under all its names, like work RAM,
//      and the associative purge names the 16-byte line of its word: both are read off the emulator's memory model by rule 2b.
//   Area 4:
//   4. Every instruction a run executed lies in DecompileResult::body, or in the code of a function
//      this one calls (or that function's body), or a flow warning names it (a warning at a jump taken for
//      a tail call, or an indirect jump treated as a call, names what the run executes after it).
//   5. The entry of another function inside the body has a "flow runs into" warning; no data word skipped
//      by a bra is in the body.
//   6. Each index of a jump table that a run dispatched with reaches a case that holds the code the
//      index runs (its own label, or the default of a switch whose bound check was taken out, or what
//      follows the switch) when that code does something the ledger lists, or a warning says cases may
//      be missing. A switch the engine could not normalize (no guard in the finished function) is one case
//      per destination, labelled with the address the table holds for the index (A2-16, A4-10, A4-11); the
//      label looked for is then that address (counted `table.switch-on-address`).
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
//   9b. (round 7, A3-34) The canary family (not drawn from the grammar: enumerated, TestCanary): a function stores 0x5A in a local, calls a
//       writer that overwrites it through a pointer in r4 or r2, through r15 plus an index, with a computed value or through a callee that
//       gives a lent writer its own entry r15 + 4, and returns the local. No `return` of the text holds the canary (or a variable assigned it
//       with no call between that could have written it) when no run returned it; the hand-back kinds only when the signature is lent.
//       Round 8 (A3-44): the address in r5, r6 or r7, handed through a word of memory the writer loads it from, the result used (`add #1,r0`),
//       the call under `if (r4 != 0)` or in a loop that runs twice.
//   10. A register CallEffects::preserved names has the value it started with at every return: r0-r15, SR,
//       GBR, VBR, MACH, MACL and PR (the four after SR started at values of their own).
//   10b. (round 8, A3-54) The rest of CallEffects against each finished run of a lent function: a store at or above the entry r15 needs
//       kFrame in storesThrough and its word in frameWords, a store into the 4 KiB arena of r4-r7 or GBR needs that register's bit or
//       kMemory, a register that does not end as it began and holds a pointer into an arena or the stack needs that origin in outOrigins,
//       and r15 ends spDelta bytes from where it began (EffectClaimsContradicted; SE_GENERATED_MUTATE=claims).
//   11. A lent preserved set is honoured by the caller's text (patch 0028): when every function this one
//       calls was lent a signature that keeps r2-r7, MACH, MACL or SR, the text never reads that register
//       as a call's output (extraout_<reg>), whatever else the function does or is lent itself.
//   12. The mirror of 11 (round 3, A3-3): when a function this one calls was lent a signature that changes a
//       register the model keeps -- r8-r14, GBR, VBR -- the text never reads that register as the value it had
//       on entry (unaff_<reg>, in_<reg>), unless the function reads it on purpose.
//   Area 7, the pseudocode-to-assembly mapping (MappingOracle.h, shared with the game-code run):
//   13. Every token address of the text is even, lies in the body (or is named by a flow warning), and is an instruction that
//       decodes; a line's firstAddr is its lowest token address; a line from several instructions says so in its hover text
//       (LineOriginText) and lists them; every access the ledger lists has a token of its instruction on its first line; every
//       call in the final p-code has a token at its instruction; every executed instruction in the body is counted by what carries
//       it (`map.executed.*`: a token, or none because the instruction is a delay slot, a move or an operation folded into its user).
//       SE_GENERATED_MUTATE=tokodd|tokoutside|tokfirst|tokdrop|tokcall|tokunmark|tokhover damages a result as the defect would.
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
//   SE_GENERATED_CANARY=<n>  the canary family (TestCanary, A3-34): n programs, default 240, 0 off; SE_GENERATED_CANARY_ONLY=<k> prints variant k
//   SE_GENERATED_SEED=<k>    one seed, with everything printed
//   SE_GENERATED_GRAMMAR=1|2 only that grammar (default: the odd seeds are version 1, the even ones version 2)
//   SE_GENERATED_SHRINK=<n>  how many findings to reduce (default 3; 0 none; large: all)
//   SE_GENERATED_VERBOSE=1   print the full report of every failing seed, not just the first few
//   SE_GENERATED_TOKENS=1    print each output line with the addresses its tokens came from
//   SE_GENERATED_MUTATE=1|ledger|second|alias|params|ret|body|cases|warn|preserved|claims|lent|data|honour|kept|tokodd|tokoutside|tokfirst|tokdrop|tokcall|tokunmark|tokhover
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
//                            SE_GENERATED_RETURNS="1,0,2,..." (the generator's intent per function: 1 returns r0, 0 leaves it alone, 2 copies a parameter to r0 on one path only)
#include "DecompilerTestSupport.h"
#include "FrameLedger.h"
#include "MappingOracle.h"

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

// Cache: a cell of the cache's data array (partitions 4 and 6, 4 KiB, A5-71); Cart: an address of the A-bus cartridge areas (A5-73);
// FnRel: the callee's address minus the bsrf's + 4 (A2-20).
enum class LitKind : uint8_t { Hw, Ram, Mbox, Fn, Mask, Cache, Cart, FnRel };
const char* KindName(LitKind k)
{
    return k == LitKind::Hw ? "hardware" : k == LitKind::Ram ? "RAM" : k == LitKind::Mbox ? "mailbox" : k == LitKind::Mask ? "mask" :
           k == LitKind::Cache ? "cache array" : k == LitKind::Cart ? "cartridge" : k == LitKind::FnRel ? "function, relative" : "function";
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
    enum Type : uint8_t { Alu, Mem, LitPtr, Skip, Loop, Poll, Spill, Call, Table, BraLoop, BraBack, DataSkip, Mask, Choice } type = Alu;
    const char* shape = "";
    std::vector<uint16_t> words;   // Alu, Mem: the instruction words (an indexed access: mov #k,r0 first)
    int base = -1;                 // Mem: the pointer register
    int reg = 6, lit = 0;          // LitPtr, Poll: the pointer register and its literal; Mask: the pointer register and the mask's literal (loaded into jsrReg)
    uint16_t cond = 0;             // Skip: the tst or cmp that sets T
    bool onTrue = true;            // Skip: bt (else bf); Mask: and (else or, or xor with 'xorOp')
    bool xorOp = false;            // Mask: xor (A5-70)
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
    bool viaBsrf = false;          // Call (with viaJsr): bsrf Rn instead of jsr @Rn, the literal being the distance (A2-20)
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
    // Choice (A3-64): `words` (the chooser's test; empty: the T the caller left) ; bt/bf L ; `setup` ; bra M ; nop ; L: `after` ; M:. Each arm writes
    // r0 and uses it once more (a compare), so the engine drops the return (A3-55).
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
    bool retIsCall = false;        // ...or by no instruction: the function's last item is a call, and r0 is what its callee returned (`return g(p);`, A2-31)
    // ...or by no instruction after its first item, a Choice of two values by an input (A3-64): what chose them (Choice*), else -1.
    int choiceBy = -1;
    bool noR0 = false;             // never writes r0 itself (a function that returns nothing)
    bool partial = false;          // ...bar `tst ; bt ; mov Rs,r0` last: r0 is a parameter on one path and left alone on the other (A3-20)
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
    // ...and one for the second names of work-RAM words (A5-52): which literals are another address of an earlier literal's word
    // (the other partition, bits 28-27, the 1 MiB repeat), and the partition masks (A5-51). Neither moves a draw of the streams above.
    Rng aliasRng;
    Rng maskRng;
    Rng placeRng;   // cache array and cartridge literals (A5-71, A5-73)
    Rng loopMaskRng;   // a mask or a step inside a loop (A5-60, A5-70)
    // ...and one for the frame walk (a loop that moves a stack address, round 5 A3-16): whether a function has one and where.
    Rng frameRng;
    // ...and one for the partial result (a parameter copied to r0 on one path only, round 6 A3-20).
    Rng partialRng;
    Rng retRng;   // a stream of its own: whether a returning function returns its last call's result (A2-31)
    Rng bsrfRng;  // ...and one for which register-called calls are bsrf (A2-20)
    Rng choiceRng;   // ...and one for the return chosen by an input (A3-64)
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
    Gen(uint64_t seed) : rng(seed), macRng(seed ^ 0x6D61636D61636D61ull), guardRng(seed ^ 0x7367756172643A5Aull), convRng(seed ^ 0x636F6E7663727667ull), wideRng(seed ^ 0x77696465414C5521ull), aliasRng(seed ^ 0x616C696173526E67ull), maskRng(seed ^ 0x6D61736B52616E64ull), placeRng(seed ^ 0x706C616365526E67ull), loopMaskRng(seed ^ 0x6C6F6F704D61736Bull), frameRng(seed ^ 0x6672616D6557616Cull), partialRng(seed ^ 0x7061727469616C52ull), retRng(seed ^ 0x7265747263616C6Cull), bsrfRng(seed ^ 0x6273726652656C21ull), choiceRng(seed ^ 0x63686F6F73657221ull)
    {
        for (int i = 0; i < 8; ++i) align[i] = 4, pref[i] = 4;
        p.mbox = 0x26001000u + 0x10u * rng.Below(4);
    }

    // The registers r0-r3 an item may write on some path (A3-64's choosers: r0 or a scratch register is the caller's value only where no item wrote
    // it on any path; `Def` says only that one wrote it on every path, and a loop's write is on none). Read off the words as the decoder prints
    // them (the last operand when it is a register, @-Rn and @Rn+), conservatively, and the registers items write without a word of their own.
    static uint32_t MayWriteLow(const std::vector<Item>& items)
    {
        uint32_t m = 0;
        auto scan = [&m](uint16_t w) {
            const sfe::DisassembledInstruction ins = sfe::Sh2Decode(kCode, w);
            const std::string& o = ins.Operands;
            for (int k = 0; k < 4; ++k)
            {
                const std::string r = "r" + std::to_string(k);
                const bool last = o.size() >= r.size() && o.compare(o.size() - r.size(), r.size(), r) == 0 &&
                                  (o.size() == r.size() || o[o.size() - r.size() - 1] == ',');
                if (last || o.find("@-" + r) != std::string::npos || o.find("@" + r + "+") != std::string::npos) m |= 1u << k;
            }
        };
        for (const Item& it : items)
        {
            for (const std::vector<uint16_t>* ws : { &it.words, &it.setup, &it.after, &it.slotWords })
                for (uint16_t w : *ws) scan(w);
            if (it.type == Item::Skip) scan(it.cond);
            if (it.type == Item::Loop || it.type == Item::BraLoop || it.type == Item::BraBack) m |= 1u << it.counter;
            if (it.type == Item::LitPtr || it.type == Item::Poll || it.type == Item::Mask) m |= (1u << it.reg) | (1u << it.jsrReg) | 1u;
            if (it.type == Item::Table || it.type == Item::Call) m |= 0xFu;
            m |= MayWriteLow(it.slot) | MayWriteLow(it.kids);
            for (const std::vector<Item>& c : it.cases) m |= MayWriteLow(c);
        }
        return m & 0xFu;
    }
    // The registers an item loads a literal into (a pointer item, a poll): their value is no longer the caller's, on that path at least.
    static uint32_t ReloadedRegs(const std::vector<Item>& items)
    {
        uint32_t m = 0;
        for (const Item& it : items)
        {
            if (it.type == Item::LitPtr || it.type == Item::Poll) m |= 1u << it.reg;
            m |= ReloadedRegs(it.slot) | ReloadedRegs(it.kids);
            for (const std::vector<Item>& c : it.cases) m |= ReloadedRegs(c);
        }
        return m;
    }
    // Whether an item writes SR other than its flags (ldc to SR): the interrupt mask a chooser reads is then not the caller's.
    static bool WritesSr(const std::vector<Item>& items)
    {
        for (const Item& it : items)
        {
            for (const std::vector<uint16_t>* ws : { &it.words, &it.setup, &it.after, &it.slotWords })
                for (uint16_t w : *ws)
                    if ((w & 0xF0FF) == 0x400E || (w & 0xF0FF) == 0x4007) return true;   // ldc Rm,sr / ldc.l @Rm+,sr
            if (WritesSr(it.slot) || WritesSr(it.kids)) return true;
            for (const std::vector<Item>& c : it.cases)
                if (WritesSr(c)) return true;
        }
        return false;
    }

    // A return chosen by an input (A3-64): the shapes of A3-55 to A3-58 and A3-62. Two values, each written to r0 on one arm of a branch and
    // used once more (so the engine takes the return for none), and a chooser, read where the item stands (last in the function): r0 or a scratch
    // register r1-r3 that no item wrote on every path (the caller's value, on some path at least), SR's interrupt mask when no item writes SR, a
    // parameter (r4-r7, which the function may or may not use otherwise), the stack argument at the entry r15, or a constant. The values: two
    // constants, two sums of one register (`r4 + 1`, `r4 + 2`), two loads through r4 and r5, a register and a constant.
    // 'entryOff': the entry r15 from the function's r15 after its prologue. 'by' is the chooser as Func::choiceBy has it.
    static constexpr int kChoiceStack = 15, kChoiceSr = 16, kChoiceConst = 17;
    Item MakeChoice(int entryOff, int& by, bool srWritten, uint32_t reloaded, uint32_t lowWritten)
    {
        Item it;
        it.type = Item::Choice;
        int params[4], nparams = 0;   // the parameters that still hold the caller's value (stepped, perhaps), not a literal an item loaded
        for (int q = 4; q < 8; ++q)
            if (!((reloaded >> q) & 1u)) params[nparams++] = q;
        std::vector<int> kinds = { 5 };   // a constant
        if (nparams) kinds.insert(kinds.end(), { 4, 4 });   // a parameter, twice as often
        if (!(lowWritten & 1u)) kinds.push_back(0);
        if (!srWritten) kinds.push_back(1);
        if ((lowWritten & 0xEu) != 0xEu) kinds.push_back(2);
        if (entryOff <= 60) kinds.push_back(6);
        switch (kinds[choiceRng.Below(static_cast<uint32_t>(kinds.size()))])
        {
        case 0:
            by = 0;
            it.words = { choiceRng.Chance(50) ? uint16_t(0x2008) : uint16_t(0x8800 | choiceRng.Below(4)) };   // tst r0,r0 / cmp/eq #imm,r0
            it.shape = "choice.by_r0";
            break;
        case 1:
            by = kChoiceSr;
            it.words = { 0x0002, 0xC8F0 };   // stc sr,r0 ; tst #0xF0,r0
            it.shape = "choice.by_sr";
            break;
        case 2:
        {
            int pool[3], n = 0;
            for (int k = 1; k <= 3; ++k)
                if (!((lowWritten >> k) & 1u)) pool[n++] = k;
            by = pool[choiceRng.Below(static_cast<uint32_t>(n))];
            it.words = { uint16_t(0x2008 | by << 8 | by << 4) };   // tst Rn,Rn
            it.shape = "choice.by_scratch";
            break;
        }
        case 4:
        {
            by = params[choiceRng.Below(static_cast<uint32_t>(nparams))];
            it.words = { choiceRng.Chance(50) ? uint16_t(0x2008 | by << 8 | by << 4) : uint16_t(0x4015 | by << 8) };   // tst Rn,Rn / cmp/pl Rn
            it.shape = "choice.by_param";
            break;
        }
        case 5:
            by = kChoiceConst;
            it.words = { uint16_t(0xE000 | choiceRng.Below(2)), 0x2008 };   // mov #0 or #1,r0 ; tst r0,r0
            it.shape = "choice.by_const";
            break;
        default:
            by = kChoiceStack;
            it.words = { uint16_t(0x50F0 | (entryOff / 4)), 0x2008 };   // mov.l @(entry r15),r0 ; tst r0,r0
            it.shape = "choice.by_stack";
            break;
        }
        it.onTrue = choiceRng.Chance(50);
        // The second use of each value: `or r0,Rn` into a register that holds a value (it is the function's last write of Rn, dead after; the engine
        // counts the use and drops the return, A3-55), or a compare, which it does not count once its T is dead (that arm keeps its return).
        // (a scratch register: an `or` into a pointer register lands in another's arena by chance, r5 | (r6 + 0x63) is r7's + 0x63: seed 19860)
        int pool[3], np = 0;
        for (int q = 1; q <= 3; ++q)
            if (Def(q) && q != by) pool[np++] = q;
        const int un = np ? pool[choiceRng.Below(static_cast<uint32_t>(np))] : -1;
        auto useOf = [&]() { return un >= 0 && choiceRng.Chance(75) ? uint16_t(0x200B | un << 8) : uint16_t(0x8800 | choiceRng.Below(256)); };   // or r0,Rn / cmp/eq #imm,r0
        const uint32_t values = choiceRng.Below(4);
        const int s = 4 + static_cast<int>(choiceRng.Below(4));
        const uint16_t a = uint16_t(choiceRng.Below(128)), b = uint16_t((a + 1 + choiceRng.Below(100)) & 0xFF);
        switch (values)
        {
        case 0: it.setup = { uint16_t(0xE000 | a) }; it.after = { uint16_t(0xE000 | b) }; break;                                   // two constants
        case 1: it.setup = { uint16_t(0x6003 | s << 4), uint16_t(0x7000 | a) }; it.after = { uint16_t(0x6003 | s << 4), uint16_t(0x7000 | b) }; break;   // r + a, r + b
        case 2:
        {
            // mov.x @r4,r0 / @r5,r0, of the width both pointers are still aligned for (an increment earlier may have left one at 2 or 1: A11-2)
            const int al = std::min(align[4], align[5]);
            const uint16_t sz = !gTrackAlignment || al >= 4 ? 2 : al >= 2 ? 1 : 0;
            it.setup = { uint16_t(0x6040 | sz) };
            it.after = { uint16_t(0x6050 | sz) };
            break;
        }
        default: it.setup = { uint16_t(0x6003 | s << 4) }; it.after = { uint16_t(0xE000 | b) }; break;                               // a register, a constant
        }
        it.setup.push_back(useOf());
        it.after.push_back(useOf());
        return it;
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
        else if (kind == LitKind::Cache)
        {
            // A cell of the cache's data array under the partition 4 or 6, and once in two the same cell under another name of an earlier literal:
            // the other partition, or 4 KiB on (the array repeats every 4 KiB; sh7095.inc MemRead / MemWrite `case 4: case 6:`).
            l.value = (placeRng.Chance(50) ? 0xC0000000u : 0x80000000u) + 0x1000u * placeRng.Below(3) + 0x10u * placeRng.Below(3) + 4u * placeRng.Below(2);
            l.width = placeRng.Chance(50) ? 4 : 2;
            std::vector<int> prior;
            for (size_t i = 0; i < p.lits.size(); ++i)
                if (p.lits[i].kind == LitKind::Cache) prior.push_back(static_cast<int>(i));
            if (!prior.empty() && placeRng.Chance(50))
            {
                static const uint32_t kNames[] = { 0x40000000u, 0x00001000u, 0x00002000u, 0x40001000u };
                const Lit& q = p.lits[prior[placeRng.Below(static_cast<uint32_t>(prior.size()))]];
                l.value = q.value ^ kNames[placeRng.Below(4)];
                l.width = q.width;
            }
        }
        else if (kind == LitKind::Cart)
        {
            // The extended RAM cartridge's window or the backup RAM cartridge's, through either partition that reaches the bus.
            l.value = (placeRng.Chance(50) ? 0x02400000u : 0x04000000u) + (placeRng.Chance(25) ? 0x20000000u : 0u) + 4u * placeRng.Below(4);
            l.width = placeRng.Chance(50) ? 4 : 2;
        }
        else
        {
            l.value = (rng.Chance(75) ? p.mbox : (p.mbox & ~0x20000000u)) + 4u * rng.Below(3);
            l.width = rng.Chance(50) ? 2 : rng.Chance(50) ? 4 : 1;
        }
        if (kind == LitKind::Ram || kind == LitKind::Mbox) MaybeAlias(l);
        l.width = std::min(l.width, Alignment(l.value));
        return l;
    }

    // A literal of work RAM that is another address of the word an earlier one names, three times in ten: the other partition, bits
    // 28-27 flipped, or the next 1 MiB repeat. The data flow keys memory by address, and the decompiler must know these are one word.
    void MaybeAlias(Lit& l)
    {
        std::vector<int> prior;
        for (size_t i = 0; i < p.lits.size(); ++i)
            if (p.lits[i].kind == LitKind::Ram || p.lits[i].kind == LitKind::Mbox) prior.push_back(static_cast<int>(i));
        if (prior.empty() || !aliasRng.Chance(30)) return;
        const Lit& q = p.lits[prior[aliasRng.Below(static_cast<uint32_t>(prior.size()))]];
        static const uint32_t kFlips[] = { 0x20000000u, 0x08000000u, 0x10000000u, 0x00100000u };
        const uint32_t pick = aliasRng.Below(5);
        // ...or the name of its cache line in the associative-purge partition (0x4xxxxxxx), the barrier the slave's idle loop puts between
        // its store and its poll: not memory, but a word the function purges is one it expects another processor to write
        // ...through the partition 0xA too, which Mednafen sends to the same purge (sh7095.inc `case 2: case 5:`, A5-62), one time in three of them
        // ...the purge invalidates the 16-byte line of the word it names, so it may name another word of that line (A5-72), two times in five
        const uint32_t purged = pick == 4 && aliasRng.Chance(40) ? q.value ^ (4u << aliasRng.Below(2)) : q.value;
        l.value = pick == 4 ? ((purged & 0x07FFFFFFu) | (aliasRng.Chance(33) ? 0xA0000000u : 0x40000000u)) : q.value ^ kFlips[pick];
        l.width = q.width;
    }

    LitKind PickKind()
    {
        const uint32_t r = rng.Below(100);
        return r < 45 ? LitKind::Hw : r < 75 ? LitKind::Ram : LitKind::Mbox;
    }

    Item MakeLitPtr(int reg, LitKind kind, bool polled = false)
    {
        // One RAM pointer in seven is a cell of the cache's data array or an address of the cartridge areas instead (A5-71, A5-73), drawn from a stream
        // of its own so the other programs are the ones they were.
        // (Not a polled one: a poll waits for another processor to write the word, which only work RAM marked volatile is.)
        if (kind == LitKind::Ram && !polled && placeRng.Chance(15)) kind = placeRng.Chance(60) ? LitKind::Cache : LitKind::Cart;
        if (disc && reg == selReg) reg = 13 - reg;   // the jump table's integer stays one
        Item it;
        it.type = Item::LitPtr;
        it.reg = reg;
        it.lit = static_cast<int>(p.lits.size());
        it.shape = kind == LitKind::Hw ? "litptr.hw" : kind == LitKind::Ram ? "litptr.ram" : kind == LitKind::Cache ? "litptr.cache" :
                   kind == LitKind::Cart ? "litptr.cart" : "litptr.mbox";
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
        Item ptr = MakeLitPtr(7, kind, true);   // registers the literal; the poll item carries it
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
        // ...a bsrf for some of the calls through a register (A2-20): the literal is the distance to the callee from the bsrf + 4, known only
        // after layout. Drawn from a stream of its own, so no other draw of a program moves.
        it.viaBsrf = it.viaJsr && bsrfRng.Chance(30);
        it.shape = it.viaBsrf ? "call.bsrf" : it.viaJsr ? "call.jsr" : "call.bsr";
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
            l.kind = it.viaBsrf ? LitKind::FnRel : LitKind::Fn;
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
        // A return chosen by an input (A3-64), from a stream of its own and after every other draw, so no program a seed made before it changes but
        // for this item: in a function that returns, makes no call (a callee may leave anything in r0), and has no table (mova writes r0) and no
        // shared exit. Last, as the return value (no retWord follows it).
        if (f.returns && f.end == Func::Rts && nCalls == 0 && !table && f.sel < 0 && !f.shared && choiceRng.Chance(15))
        {
            const int entryOff = 4 * static_cast<int>(f.pushed.size()) + (f.savePr ? 4 : 0) + f.frame;
            f.items.push_back(MakeChoice(entryOff, f.choiceBy, WritesSr(f.items), ReloadedRegs(f.items), MayWriteLow(f.items)));
            if (f.choiceBy >= 1 && f.choiceBy <= 3) f.reads |= 1u << f.choiceBy;
        }
        // `if (b) return a;` with no return on the other path (A3-20): last, so every path passes it. Only in a function that makes no call
        // (a callee may leave anything in r0) and returns nothing otherwise.
        if (!f.returns && f.end == Func::Rts && nCalls == 0 && !table && f.sel < 0 && partialRng.Chance(10))
        {
            int pool[4], k = 0;
            for (int q = 4; q < 8; ++q)
                if (Def(q)) pool[k++] = q;
            if (k >= 1)
            {
                const int src = pool[partialRng.Below(static_cast<uint32_t>(k))], tst = pool[partialRng.Below(static_cast<uint32_t>(k))];
                Item sk;
                sk.type = Item::Skip;
                sk.shape = "skip.partial";
                sk.cond = uint16_t(0x2008 | tst << 8 | tst << 4);   // tst Rn,Rn
                sk.onTrue = partialRng.Chance(50);
                Item mv;
                mv.type = Item::Alu;
                mv.shape = "partial.mov";
                mv.words = { uint16_t(0x6003 | src << 4) };   // mov Rs,r0
                sk.kids.push_back(mv);
                f.items.push_back(sk);
                f.partial = true;
            }
        }
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
        if (f.choiceBy >= 0) f.retInSlot = false;   // the choice is the return value: no instruction writes r0 after it (EmitExit)
        // `return g(p);`: the value is what the last call's callee returned, with nothing written after it (the usual epilogue pops in the
        // slot, or a nop there). The draws above are those of the seeds found before this form existed; this one has a stream of its own.
        if (f.returns && f.end == Func::Rts && !table && !f.items.empty() && f.items.back().type == Item::Call && f.items.back().callee >= 0 &&
            fns[f.items.back().callee].returns && retRng.Chance(50))
        {
            f.retIsCall = true;
            f.retInSlot = false;
            f.slotPop = retRng.Chance(70);
        }
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
        // A pointer moved between bus partitions with a mask -- `and` 0x1FFFFFFF (to the cached partition) or `or` 0x20000000 (to the
        // cache-through one), the idiom SBL calls CACHE_THROUGH (A5-51) -- in one program of eight, after the pointer's literal. The pointer
        // is then a merge of addresses whenever a skipped reload gave it two values, which the engine does not fold to a constant.
        if (maskRng.Chance(13))
        {
            Item m;
            m.type = Item::Mask;
            m.reg = items.size() > 1 && p.lits.size() > 1 && maskRng.Chance(50) ? 7 : 6;
            m.onTrue = maskRng.Chance(50);                    // true: and (to cached); false: or (to cache-through)
            m.jsrReg = 1 + static_cast<int>(maskRng.Below(2));
            m.shape = m.onTrue ? "litptr.mask.and" : "litptr.mask.or";
            Lit l;
            l.kind = LitKind::Mask;
            l.value = m.onTrue ? 0x1FFFFFFFu : 0x20000000u;
            m.lit = static_cast<int>(p.lits.size());
            p.lits.push_back(l);
            // after the pointer register's literal (the first item is r6's; r7's is wherever it was made)
            size_t at = 1;
            for (size_t i = 0; i < items.size(); ++i)
                if (items[i].type == Item::LitPtr && items[i].reg == m.reg) at = i + 1;
            at += maskRng.Below(static_cast<uint32_t>(items.size() - at + 1));
            items.insert(items.begin() + at, m);
        }
        // ...and, in nearly one program in two, a mask, an `xor` or a step on the pointer a loop body reads through, in the body itself (A5-60, A5-70): after the
        // access, alone or under a skip, with a step of the pointer's alignment before it two times in five, so the loop moves the pointer between
        // partitions on some turns and not on others, by two operations in a turn, and by a step and a mask in one. A pointer of the partitions 0 and 1
        // only: an `xor 0x20000000` of a cache array cell would be an on-chip register. Rule 3b holds the reads to the cache caution, rule 3c to the
        // registers the notes name.
        if (loopMaskRng.Chance(45))
        {
            std::vector<size_t> loops;
            for (size_t i = 0; i < items.size(); ++i)
                if (items[i].type == Item::Loop) loops.push_back(i);
            if (!loops.empty())
            {
                const size_t at = loops[loopMaskRng.Below(static_cast<uint32_t>(loops.size()))];
                int reg = -1;
                for (const Item& k : items[at].kids)
                    if (k.type == Item::Mem && k.base >= 6) { reg = k.base; break; }
                uint32_t start = 0xFFFFFFFFu;
                for (size_t i = 0; i < at; ++i)
                    if (items[i].type == Item::LitPtr && items[i].reg == reg) start = p.lits[items[i].lit].value;
                if (reg >= 0 && start < 0x40000000u)
                {
                    Item m;
                    m.type = Item::Mask;
                    m.reg = reg;
                    m.jsrReg = 1 + static_cast<int>(loopMaskRng.Below(2));
                    const uint32_t op = loopMaskRng.Below(3);
                    uint32_t value;
                    if (op == 0)
                    {
                        static const uint32_t kAnd[] = { 0x1FFFFFFFu, 0xFFFFFFF0u, 0xFFFFFFF9u };
                        m.onTrue = true;
                        value = kAnd[loopMaskRng.Below(3)];
                        m.shape = "loop.mask.and";
                    }
                    else if (op == 1)
                    {
                        m.onTrue = false;
                        value = 0x20000000u;
                        m.shape = "loop.mask.or";
                    }
                    else
                    {
                        m.xorOp = true;
                        value = loopMaskRng.Chance(50) ? 0x20000000u : 0x4u;
                        m.shape = "loop.mask.xor";
                    }
                    Lit l;
                    l.kind = LitKind::Mask;
                    l.value = value;
                    m.lit = static_cast<int>(p.lits.size());
                    p.lits.push_back(l);
                    std::vector<Item> body;
                    if (loopMaskRng.Chance(40))
                    {
                        Item step;
                        step.type = Item::Alu;
                        step.shape = "loop.step";
                        step.words = { static_cast<uint16_t>(0x7000 | reg << 8 | std::min(4, align[reg])) };   // add #w,Rn
                        body.push_back(step);
                    }
                    body.push_back(m);
                    std::vector<Item>& kids = items[at].kids;
                    if (loopMaskRng.Chance(50))
                        kids.insert(kids.end(), body.begin(), body.end());
                    else
                    {
                        Item skip;
                        skip.type = Item::Skip;
                        skip.cond = 0x2008;   // tst r0,r0
                        skip.onTrue = loopMaskRng.Chance(50);
                        skip.shape = skip.onTrue ? "skip.bt" : "skip.bf";
                        skip.kids = body;
                        kids.push_back(skip);
                    }
                }
            }
        }
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
    std::vector<int> litFrom;         // per literal: the label of the bsrf its distance is measured from (FnRel), -1 otherwise
    std::vector<int> entryLabel, startLabel;
    std::vector<Table> tables;
    std::vector<std::vector<size_t>> tablesOf;           // per unit: indices into 'tables'
    std::vector<std::vector<std::pair<size_t, size_t>>> dataOf;   // per unit: embedded data, word indices
    int unit = 0;

    explicit Emitter(const Program& prog) : p(prog)
    {
        litWord.assign(p.lits.size(), -1);
        litFrom.assign(p.lits.size(), -1);
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
        if (f.returns && f.end == Func::Rts && !f.retInSlot && !f.retIsCall && f.choiceBy < 0) Put(f.retWord);
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
        case Item::Mask:
            PutLitLoad(it.jsrReg, it.lit);
            Put(static_cast<uint16_t>((it.xorOp ? 0x200A : it.onTrue ? 0x2009 : 0x200B) | it.reg << 8 | it.jsrReg << 4));   // and / xor / or Rm,Rn
            break;
        case Item::Choice:
        {
            const int taken = NewLabel(), join = NewLabel();
            for (uint16_t x : it.words) Put(x);
            PutBranch(it.onTrue ? 0x8900 : 0x8B00, taken);
            for (uint16_t x : it.setup) Put(x);
            PutBranch12(0xA000, join);   // bra
            Put(0x0009);
            Place(taken);
            for (uint16_t x : it.after) Put(x);
            Place(join);
            break;
        }
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
            if (it.viaBsrf)
            {
                PutLitLoad(it.jsrReg, it.fnLit);
                litFrom[it.fnLit] = NewLabel();
                Place(litFrom[it.fnLit]);
                Put(static_cast<uint16_t>(0x0003 | it.jsrReg << 8));      // bsrf Rn
            }
            else if (it.viaJsr)
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
        if (l.kind == LitKind::FnRel) return labelAddr(e.entryLabel[l.fn]) - (labelAddr(e.litFrom[lit]) + 4);
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

// The machine's memory, as the code sees it: work RAM is real -- high (0x06000000-0x07FFFFFF) and low
// (0x00200000-0x003FFFFF), through both partitions that reach memory, with bits 28-27 not decoded and a
// repeat every 1 MiB, as the bus decodes it (ss.c, sh7095.inc) -- so a word has as many addresses as the
// bus gives it (A5-52); every other address is "hardware", whose reads return a default and whose writes are
// dropped, all logged. A polled address reads busy a few times and then ready: another processor
// answering.
struct Env
{
    std::vector<uint8_t> ram;                                  // high work RAM (the snapshot's), 1 MiB
    std::vector<uint8_t> lowRam = std::vector<uint8_t>(0x100000u);   // low work RAM, 1 MiB
    std::vector<uint8_t> cacheData = std::vector<uint8_t>(0x1000u);  // the cache's data array, 4 KiB, under both partitions 4 and 6 (A5-71)
    int hwMode = 0;   // what a hardware read returns: 0 zeros, 1 ones, 2 pseudo-random
    uint64_t hwState = 0x12345;
    struct Poll { uint32_t addr; uint32_t busy, ready; int reads; uint32_t raw; };   // addr canonical, raw as the code has it
    std::vector<Poll> polls;
    std::vector<TraceRec> trace;
    int depth = 0;    // set by the machine before each instruction
    int step = 0;     // ...and the instruction's position in the run
    // The byte of work RAM an address reaches, or null: the same bytes under every address that names them.
    uint8_t* ByteAt(uint32_t a)
    {
        if ((a >> 29) == 4 || (a >> 29) == 6) return &cacheData[a & 0xFFFu];   // sh7095.inc `case 4: case 6:`: bits 31-12 are not decoded
        if ((a >> 29) > 1) return nullptr;
        const uint32_t p = a & 0x07FFFFFFu;
        if (p >= 0x06000000u) return &ram[p & 0xFFFFFu];
        if (p >= 0x00200000u && p <= 0x003FFFFFu) return &lowRam[p & 0xFFFFFu];
        return nullptr;
    }
    bool IsRam(uint32_t a) { return ByteAt(a) != nullptr; }
    // The bytes of work RAM [a, a + size) cover, as keys of the memory itself (MemKey); false when they are not work RAM.
    static bool RamRange(uint32_t a, int size, uint32_t& lo, uint32_t& hi)
    {
        if ((a >> 29) == 4 || (a >> 29) == 6)
        {
            lo = MemKey(a);
            hi = lo + static_cast<uint32_t>(size) - 1;
            return true;
        }
        if ((a >> 29) > 1) return false;
        const uint32_t p = a & 0x07FFFFFFu;
        if (!(p >= 0x06000000u || (p >= 0x00200000u && p <= 0x003FFFFFu))) return false;
        lo = MemKey(a);
        hi = lo + static_cast<uint32_t>(size) - 1;
        return true;
    }
    static uint32_t Canon(uint32_t a) { return a < 0xC0000000u ? (a & 0x0FFFFFFFu) : a; }
    // The word a polled address names, whatever it is called: a mailbox is one word under all its addresses.
    static uint32_t MemKey(uint32_t a)
    {
        if ((a >> 29) == 4 || (a >> 29) == 6) return 0x80000000u | (a & 0xFFFu);
        if ((a >> 29) > 1) return a;
        const uint32_t p = a & 0x07FFFFFFu;
        if (p >= 0x06000000u) return 0x06000000u | (p & 0xFFFFFu);
        if (p >= 0x00200000u && p <= 0x003FFFFFu) return 0x00200000u | (p & 0xFFFFFu);
        return p;
    }

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
            if (p.addr == MemKey(a))
            {
                const uint32_t v = ++p.reads <= kPollBusyReads ? p.busy : p.ready;
                if (IsRam(a)) Store(a, size, p.ready);   // what the other processor left behind
                return v & mask;
            }
        if (IsRam(a))
        {
            uint64_t v = 0;
            for (int i = 0; i < size; ++i) v = (v << 8) | *ByteAt(a + i);
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
        for (int i = size - 1; i >= 0; --i, v >>= 8)
            if (uint8_t* b = ByteAt(a + i)) *b = static_cast<uint8_t>(v);
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
        // ...nor are the words that write r0 with the value it has (A2-43, patch 0048: mov r0,r0, and r0,r0, or r0,r0, add #0,r0, xor #0,r0,
        // or #0,r0): the engine and the host take them as nop. Counted as writes, a path that runs one of them and leaves r0 alone on every
        // other looks like a function that writes r0 on every path, and r0's entry value then reaches its result (A3-46: a sample of runs that
        // all took the `and r0,r0` path, never the one that skips it).
        if (word == 0x6003u || word == 0x2009u || word == 0x200Bu || word == 0x7000u || word == 0xCA00u || word == 0xCB00u) return;
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
    void SetEnv(Env* env)
    {
        mEmu->SetEnv(env);
        mEnv = env;
    }
    // A word of memory written outside any run (no trace): the stack below the entry r15 refilled (A3-111). The memory a run reads is the
    // Env's (work RAM), not the p-code state's.
    void Poke(uint32_t a, uint32_t v)
    {
        if (mEnv != nullptr) mEnv->Store(a, 4, v);
    }
    // (A3-111) Not 0: every stack word the stack pointer rises past during the next run is refilled with this pattern (no trace), as an
    // interrupt may write anything below r15. A function that reads such a word reads what an earlier push or call left: a stale word.
    void SetScrub(uint32_t pattern) { mScrub = pattern; }

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
                const uint32_t spBefore = mScrub != 0 ? Get("r15") : 0;
                mEmu->executeInstruction();
                if (mScrub != 0)
                {
                    const uint32_t spAfter = Get("r15");
                    if (spAfter > spBefore && spAfter - spBefore <= 0x1000u)
                        for (uint32_t a = spBefore & ~3u; a < (spAfter & ~3u); a += 4) Poke(a, mScrub ^ a);
                }
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
    uint32_t mScrub = 0;
    Env* mEnv = nullptr;
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

// Where a run is handed to code the output does not follow: the index, in the run's instructions (a delay slot is not one of them), of the first
// indirect jump at depth 0 that the output says it treats as a call (an unrecovered table: the cases, and what they run, are the callee's;
// A4-7's signed bound, A4-17). What comes after it is not the output's to list or to order. SIZE_MAX when the run is never handed over.
size_t HandedOverAt(const DecompileResult& r, const RunOut& run)
{
    for (size_t k = 0; k < run.pcs.size(); ++k)
        if (run.depths[k] == 0)
            for (const FlowWarning& w : r.flowWarnings)
                if (w.address == run.pcs[k] && w.text.find("Treating indirect jump as call") != std::string::npos) return k;
    return SIZE_MAX;
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

// How far from a register address the pointer the output names it can be, for an access to be through that pointer (A5-82). A pointer is a
// register address plus the displacement of the instruction (at most 60 bytes, `mov.l @(disp,Rm)`) or an add of a small immediate; a register
// named further from the traced address than that is not the pointer of this access. The oracle cannot tell a pointer from a value by its
// text (a number is printed as a number whichever it is, `*extraout_r7 = 0xffffff9d;` stores a value that is also an on-chip register's address),
// nor a pointer's assignment on another path from the one the run took (a parameter set to `&REG` further down, a callee's output merged with
// one), so the trace decides: the named base must be where the emulator went, give or take the reach.
const uint32_t kPointerReach = 0x40;

// A register's symbol (`&NAME`) or a hexadecimal number that the bus decodes to a register, in the text, that is within kPointerReach of the
// canonical address 'reached' (the register the emulator's access came to). 'tooFar' is set when the text names some other register or number.
bool NamesHardwareBase(const std::string& text, uint32_t reached, bool& tooFar)
{
    static std::map<std::string, uint32_t> names;
    if (names.empty())
    {
        size_t n = 0;
        const HardwareRegister* regs = SaturnHardwareRegisters(n);
        for (size_t i = 0; i < n; ++i) names[regs[i].name] = regs[i].address;
        regs = SaturnHardwareWordViews(n);
        for (size_t i = 0; i < n; ++i) names[regs[i].name] = regs[i].address;
    }
    auto within = [&](uint32_t canon) {
        const uint32_t d = canon > reached ? canon - reached : reached - canon;
        if (d <= kPointerReach) return true;
        tooFar = true;
        return false;
    };
    bool found = false;
    for (size_t at = text.find('&'); at != std::string::npos; at = text.find('&', at + 1))
    {
        size_t j = at + 1;
        while (j < text.size() && (std::isalnum(static_cast<unsigned char>(text[j])) || text[j] == '_')) ++j;
        const auto it = names.find(text.substr(at + 1, j - at - 1));
        if (it != names.end()) found |= within(it->second);
    }
    for (size_t at = text.find("0x"); at != std::string::npos; at = text.find("0x", at + 2))
    {
        size_t j = at + 2;
        uint64_t v = 0;
        while (j < text.size() && std::isxdigit(static_cast<unsigned char>(text[j])) && j - at < 11)
            v = v * 16 + static_cast<uint64_t>(std::isdigit(static_cast<unsigned char>(text[j])) ? text[j] - '0' : std::tolower(text[j]) - 'a' + 10), ++j;
        uint32_t canon = 0;
        if (v < 0x00100000u || v > 0xFFFFFFFFu || !HardwareAlias(static_cast<uint32_t>(v), canon)) continue;
        if (HardwareRegistersCovering(static_cast<uint32_t>(v), 4).size() + HardwareRegistersCovering(static_cast<uint32_t>(v), 1).size() > 0)
            found |= within(canon);
        else if ((canon > reached ? canon - reached : reached - canon) <= kPointerReach)   // an address of hardware no register is named at (VDP1's
            found = true;                                                                   // 0x05D000D8): the pointer when it is where the run went
    }
    return found;
}

// The spans of the output's lines inside which control can go from a line to an earlier one: a `do { ... } while`, a `while` or `for` block, and
// a `goto` to a label above it. A statement assigns a variable that a line above it reads only if one of these holds both (A5-82).
std::vector<std::pair<size_t, size_t>> BackEdgeSpans(const std::vector<Line>& lines)
{
    std::vector<std::pair<size_t, size_t>> spans;
    auto trimmed = [&](size_t i) {
        const size_t b = lines[i].text.find_first_not_of(' ');
        return b == std::string::npos ? std::string() : lines[i].text.substr(b);
    };
    auto indentOf = [&](size_t i) {
        const size_t b = lines[i].text.find_first_not_of(' ');
        return b == std::string::npos ? std::string::npos : b;
    };
    for (size_t i = 0; i < lines.size(); ++i)
    {
        const std::string t = trimmed(i);
        if (t.empty()) continue;
        const bool opens = !t.empty() && t.back() == '{' && (t.compare(0, 4, "do {") == 0 || t.compare(0, 6, "while ") == 0 || t.compare(0, 4, "for ") == 0);
        if (opens)
            for (size_t j = i + 1; j < lines.size(); ++j)
                if (indentOf(j) == indentOf(i) && trimmed(j).compare(0, 1, "}") == 0)
                {
                    spans.emplace_back(i, j);
                    break;
                }
        if (t.compare(0, 5, "goto ") == 0 && t.back() == ';')
        {
            const std::string label = t.substr(5, t.size() - 6) + ":";
            for (size_t j = 0; j <= i; ++j)
                if (trimmed(j) == label)
                {
                    spans.emplace_back(j, i);
                    break;
                }
        }
    }
    return spans;
}

// The operands of the dereferences in a statement's text: what follows a unary `*` (up to the end of its operand: a `,`, a `;`, a `)` that closes
// nothing, ` = `), and the name before a `[` (A5-85). A `*` is unary at the start, after an operator or an opening parenthesis, or after a cast
// (`(int4)*p`, `(xunknown4 *)`); after a name, a number or another closing parenthesis it multiplies. Empty when the text dereferences nothing.
std::string DerefOperands(const std::string& text)
{
    static const std::regex castRe("^\\s*(?:[A-Za-z_]\\w*\\s*)+\\*+\\s*$|^\\s*(?:u?int\\d|xunknown\\d|undefined\\d?|char|uchar|short|ushort|bool|byte|word|dword|qword|long|ulong|float\\d*|code|void|unkbyte\\d+|unkuint\\d+|unkint\\d+)\\s*$");
    std::string out;
    for (size_t i = 0; i < text.size(); ++i)
    {
        if (text[i] == '[')
        {
            size_t e = i;
            while (e > 0 && text[e - 1] == ' ') --e;
            size_t b = e;
            while (b > 0 && (std::isalnum(static_cast<unsigned char>(text[b - 1])) || text[b - 1] == '_')) --b;
            if (b < e) out += text.substr(b, e - b) + " ; ";
            continue;
        }
        if (text[i] != '*') continue;
        size_t k = i;
        while (k > 0 && text[k - 1] == ' ') --k;
        bool unary = k == 0;
        if (!unary)
        {
            const char p = text[k - 1];
            if (p == ')')
            {
                int depth = 0;
                size_t open = std::string::npos;
                for (size_t q = k; q-- > 0;)
                {
                    if (text[q] == ')') ++depth;
                    else if (text[q] == '(' && --depth == 0) { open = q; break; }
                }
                unary = open != std::string::npos && std::regex_match(text.substr(open + 1, k - 1 - open - 1), castRe);
            }
            else unary = !(std::isalnum(static_cast<unsigned char>(p)) || p == '_' || p == ']');
        }
        if (!unary) continue;
        int depth = 0;
        size_t j = i + 1;
        for (; j < text.size(); ++j)
        {
            const char c = text[j];
            if (c == '(' || c == '[') ++depth;
            else if (c == ')' || c == ']') { if (depth == 0) break; --depth; }
            else if (depth == 0 && (c == ',' || c == ';' || text.compare(j, 3, " = ") == 0)) break;
        }
        const std::string operand = text.substr(i + 1, j - i - 1);
        if (operand.find_first_not_of(" *") != std::string::npos) out += operand + " ; ";
    }
    return out;
}

// The read at 'insn' goes through a pointer that the output derives from a hardware base: the statement holding it names one, or a
// variable in it is assigned one, through assignments of variables to variables that can reach it (A5-82: an assignment further down the text,
// outside every loop and backward goto the statement is in, is made after the read on every path, however it may name `&REG`). 'unrelated' is set when it does not but the output
// names the register somewhere else (the argument of a call, another statement): counted apart, not folded into anonymous reads.
bool PointerPrintsFromHardware(const DecompileResult& r, uint32_t insn, const std::vector<const HardwareRegister*>& covering, uint32_t reached, bool& unrelated)
{
    unrelated = false;
    bool tooFar = false;   // the derivation names register addresses, none within reach of the one the emulator came to (A5-82)
    // The read's own tokens on each line that has some: what the instruction contributes to the statement, not the whole statement (a
    // comparison's other operand, `... != &VDP2_CYCB0U`, comes from another instruction and is not the pointer, seed 347).
    std::vector<std::pair<std::string, size_t>> statements;   // (text, the line it is on)
    for (size_t li = 0; li < r.lines.size(); ++li)
    {
        const Line& l = r.lines[li];
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
        statements.emplace_back(names ? own : l.text, li);
    }
    bool elsewhere = false;
    for (const HardwareRegister* reg : covering)
        for (const Line& l : r.lines)
            elsewhere |= l.text.find(std::string("&") + reg->name) != std::string::npos;
    if (statements.empty()) return elsewhere;   // (the read's text was not found: the old premise, never narrower)
    // Assignments `name = value;` in the output.
    struct Assign { std::string name, rhs; size_t line; };
    std::vector<Assign> assigns;
    for (size_t li = 0; li < r.lines.size(); ++li)
    {
        const Line& l = r.lines[li];
        size_t b = l.text.find_first_not_of(' ');
        if (b == std::string::npos) continue;
        size_t e = b;
        while (e < l.text.size() && (std::isalnum(static_cast<unsigned char>(l.text[e])) || l.text[e] == '_')) ++e;
        if (e == b || l.text.compare(e, 3, " = ") != 0) continue;
        std::string rhs = l.text.substr(e + 3);
        while (!rhs.empty() && (rhs.back() == ';' || rhs.back() == ' ')) rhs.pop_back();
        assigns.push_back(Assign{ l.text.substr(b, e - b), rhs, li });
    }
    const std::vector<std::pair<size_t, size_t>> spans = BackEdgeSpans(r.lines);
    auto reaches = [&](size_t assignLine, size_t useLine) {
        if (assignLine <= useLine) return true;
        for (const auto& sp : spans)
            if (sp.first <= useLine && assignLine <= sp.second) return true;
        return false;
    };
    // (A5-85) When the statement dereferences something, the pointer is in the operand of the dereference: `return CONCAT44((int4)*param_1,xVar1);`
    // reads through param_1, and xVar1, the other half of the value returned, was assigned a number that is an on-chip address within reach of the
    // one the run read (0xffffffd3, 41 bytes from 0xFFFFFFFC). The derivation is followed from the operands first; one that names a base only
    // outside them is counted as not the pointer.
    auto derive = [&](bool operandsOnly, bool& narrowed) {
        std::set<std::pair<std::string, size_t>> seen;
        std::vector<std::pair<std::string, size_t>> todo;   // (variable, the line of the statement that reads it)
        for (const auto& statement : statements)
        {
            std::string text = statement.first;
            const size_t b = text.find_first_not_of(' ');
            size_t e = b == std::string::npos ? 0 : b;
            while (e < text.size() && (std::isalnum(static_cast<unsigned char>(text[e])) || text[e] == '_')) ++e;
            if (b != std::string::npos && e > b && text.compare(e, 3, " = ") == 0) text = text.substr(e + 3);   // (not the variable assigned to)
            text = WithoutCalls(text);
            if (operandsOnly)
            {
                // (the read's own tokens hold no parentheses or casts of other instructions, so the dereferences are found in the whole line,
                // and the operands kept are those that name something of the read's own tokens)
                const std::string all = DerefOperands(r.lines[statement.second].text);
                std::set<std::string> own;
                for (const std::string& id : Identifiers(text)) own.insert(id);
                std::string ops;
                size_t from = 0;
                while (from < all.size())
                {
                    const size_t to = all.find(" ; ", from);
                    const std::string one = all.substr(from, to == std::string::npos ? std::string::npos : to - from);
                    bool mine = false;
                    for (const std::string& id : Identifiers(one)) mine |= own.count(id) != 0;
                    if (mine) ops += one + " ; ";
                    if (to == std::string::npos) break;
                    from = to + 3;
                }
                if (!ops.empty())
                {
                    text = ops;
                    narrowed = true;
                }
            }
            if (NamesHardwareBase(text, reached, tooFar)) return true;
            for (const std::string& id : Identifiers(text)) todo.emplace_back(id, statement.second);
        }
        while (!todo.empty() && seen.size() < 256)
        {
            const std::pair<std::string, size_t> item = todo.back();
            todo.pop_back();
            if (!seen.insert(item).second) continue;
            for (const Assign& a : assigns)
            {
                if (a.name != item.first) continue;
                const std::string rhs = WithoutCalls(a.rhs);
                if (!reaches(a.line, item.second))
                {
                    if (NamesHardwareBase(rhs, reached, tooFar)) tooFar = true;   // (named, but made after the read: counted as not the pointer)
                    continue;
                }
                if (NamesHardwareBase(rhs, reached, tooFar)) return true;
                for (const std::string& q : Identifiers(rhs)) todo.emplace_back(q, a.line);
            }
        }
        return false;
    };
    bool narrowed = false;
    if (derive(true, narrowed)) return true;
    if (narrowed)
    {
        bool again = false;
        if (derive(false, again)) tooFar = true;   // (named outside the dereference: not the pointer)
    }
    unrelated = elsewhere || tooFar;
    return false;
}

// The text without the saves of a register's entry value into a stack local made before the first call (`xStack_c = unaff_r13;`, a push): the
// engine shows a save slot as a local when a pointer stepped down may reach it (patch 0056, A3-65: seed 901303 runs on after its pops and walks
// over the words they freed), and the save reads the register before any call can change it. A read of the local after the call is a read of the
// local; one of the register after a call still prints `unaff_r13` elsewhere and is found.
// A save the engine shows in part is the same save (A3-78, grammar 2 seed 1009895): function 0 pushes r10 and tail-jumps into function 2, whose
// `mov.w @r15` reads the half of the push slot that is read later, so the push prints as `iStack_c = (int2)((uint4)unaff_r10 >> 0x10);`. A cast,
// a right shift by a constant and the parentheses around them change which bits are kept, not when the register is read.
bool WholeWord(const std::string& text, const std::string& word);
std::string SavesBeforeCallsDropped(const std::string& text, const char* reg)
{
    static const std::string cast = "(?:\\(\\s*\\w+\\s*\\**\\s*\\)\\s*)";
    const std::regex save("^\\s*_?[A-Za-z]*Stack_[0-9a-f]+ = " + cast + "*\\(?\\s*" + cast + "*unaff_" + std::string(reg) +
                          "(?:\\s*>>\\s*(?:0x[0-9a-f]+|\\d+))?\\s*\\)?;\\s*$");
    static const std::regex call("\\bfunc_0x[0-9a-fA-F]+\\s*\\(");
    // A3-78, seed 1418819: the variable of the entry value is given the call's output after the call (`unaff_r10 = extraout_r10;` in the
    // branch that calls, the engine's join of the two values under one name), and a read of it after that is a read of what the call left.
    // Followed in the text's order, so only outside loops and in a text without gotos, where that order is the order the code runs in.
    const std::regex assign("^\\s*unaff_" + std::string(reg) + " = ([^;]*);\\s*$");
    const std::regex word("\\bunaff_" + std::string(reg) + "\\b");
    static const std::regex jump("\\bgoto\\b");
    static const std::regex loopOpen("^\\s*(do|while|for)\\b");
    const bool straight = !std::regex_search(text, jump);
    std::vector<bool> loops;   // the open blocks of the body, each a loop or not
    bool reassigned = false, staleAtCall = false, blockSinceCall = false;
    std::istringstream in(text);
    std::string out, line;
    bool body = false, called = false;
    while (std::getline(in, line))
    {
        if (!body)
        {
            body = line.compare(0, 1, "{") == 0;
            out += line + "\n";
            continue;
        }
        const bool callHere = std::regex_search(line, call);
        std::smatch am;
        // `unaff_r10 = extraout_r10;` right after the call, in its block (no brace between), when no earlier call had left the entry value
        // stale: every path that reaches a later read either ran the call and this assignment, or neither
        const bool inLoop = std::find(loops.begin(), loops.end(), true) != loops.end();
        if (straight && !inLoop && called && !callHere && !blockSinceCall && !staleAtCall && std::regex_match(line, am, assign) &&
            !std::regex_search(am[1].str(), word))
        {
            reassigned = true;
            continue;
        }
        if (called && reassigned && !inLoop) line = std::regex_replace(line, word, "reassigned_" + std::string(reg));   // read before this line's call
        if (callHere)
        {
            staleAtCall = called && (!reassigned || inLoop);
            called = true;
            reassigned = false;
            blockSinceCall = false;
        }
        // the blocks the line closes and opens (`} while (c);`, `} else {`, `do {`): inside a loop the text's order is not the code's
        for (char ch : line)
            if (ch == '}' && !loops.empty()) loops.pop_back();
        if (line.find('{') != std::string::npos)
            loops.push_back(std::regex_search(line, loopOpen));
        if (line.find('{') != std::string::npos || line.find('}') != std::string::npos)
            blockSinceCall = true;
        if (!called && std::regex_match(line, save)) continue;
        out += line + "\n";
    }
    // ...and then the register's declaration, if nothing else names it
    const std::regex decl("(^|\\n)\\s*\\w+ \\**unaff_" + std::string(reg) + ";\\s*(\\n|$)");
    const std::string withoutDecl = std::regex_replace(out, decl, "$1", std::regex_constants::format_first_only);
    return WholeWord(withoutDecl, std::string("unaff_") + reg) ? out : withoutDecl;
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
    bool partial = false;    // ...or on some paths only, from a parameter, and left alone on the rest (SE_GENERATED_RETURNS 2)
    int choiceBy = -1;       // ...or by two values a chooser decides between (Func::choiceBy; SE_GENERATED_RETURNS 30 + it, hex)
    bool choiceInherited = false;   // ...in a function it jumps to or runs on into
    int sel = -1;            // the register a jump table indexes with
    uint32_t clobbers = 0;   // registers it leaves changed (Func::effClobbers): bit n is Rn, 17 gbr, 18 vbr, 19 mach, 20 macl
    uint32_t reads = 0;      // registers whose entry value it reads on purpose (Func::reads)
    uint32_t canary = 0;     // a constant the function stores in a local that a callee overwrites: no `return` of its text may contain it (A3-34)
    uint32_t canary2 = 0;    // ...and what the function returns of it when it uses the result (canary + 1): no `return` may contain that either (A3-44)
    bool canaryIfLent = false;   // ...only when the signature is lent: the callee hands the value back through the caller's own frame, which the
                             // engine cannot see, so a caller withheld for it (A3-15, A3-21: the claim is the check) prints the stale local
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
        f.partial = p.fns[i].partial;
        // (a function that jumps to or runs on into a choice returns its value: Func::returns is the target's)
        f.choiceBy = p.fns[i].choiceBy;
        for (size_t k = i; f.choiceBy < 0 && k < p.fns.size() && p.fns[k].v2;)
        {
            const Func& g = p.fns[k];
            k = g.end == Func::TailJump ? static_cast<size_t>(g.tailTo) : g.end == Func::Fall ? k + 1 : p.fns.size();
            if (k < p.fns.size()) f.choiceBy = p.fns[k].choiceBy;
            f.choiceInherited = f.choiceBy >= 0;
        }
        f.sel = p.fns[i].sel;
        f.clobbers = p.fns[i].effClobbers;
        f.reads = p.fns[i].reads;
        s.fns.push_back(f);
    }
    for (const Func& fn : p.fns)
        for (const Item& it : fn.items)
            if (it.type == Item::Poll)
                s.polls.push_back({ Env::MemKey(p.lits[it.lit].value), it.untilSet ? 0u : 1u, it.untilSet ? 1u : 0u, 0, p.lits[it.lit].value });
    std::ostringstream o;
    for (size_t i = 0; i < p.lits.size(); ++i)
        if (a.litAddr[i])
        {
            const uint32_t v = p.lits[i].kind == LitKind::Fn || p.lits[i].kind == LitKind::FnRel ? a.units[p.lits[i].fn].entry : p.lits[i].value;
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
        for (size_t i = 0; i < s.fns.size(); ++i)
            o << (i ? "," : "") << std::hex << (s.fns[i].choiceBy >= 0 ? 0x30 + s.fns[i].choiceBy : s.fns[i].returns ? 1 : s.fns[i].partial ? 2 : 0) << std::dec;
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
    uint32_t stackFill = 0;    // (A3-111) not 0: the 512 bytes below the entry r15 are refilled with this pattern before the run, and every
                               // word the stack pointer rises past during it (Machine::SetScrub)
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
        if (rs.stackFill != 0)
            for (uint32_t a = kStackTop - 512u; a < kStackTop; a += 4) mM->Poke(a, rs.stackFill ^ a);
        mM->SetScrub(rs.stackFill);
        mM->Run(f.layout.entry, kSentinel, kStepLimit, out, env);
        mM->SetScrub(0);
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
std::string gMutateKind;   // "", "ledger", "second", "params", "ret", "body", "cases", "warn", "preserved", "claims", "lent", "data", "honour", "kept"
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
    else if (gMutateKind == "alias")
    {
        // The host that did not make a word it reaches under two names volatile: the accesses to ordinary memory that fold are not listed.
        std::vector<MemoryAccess> keep;
        for (const MemoryAccess& m : r.accesses)
        {
            bool aliased = false;
            for (const auto& h : r.aliasedMemory) aliased |= m.direct && h.first <= m.address && m.address <= h.second;
            if (!aliased) keep.push_back(m);
            else
            {
                // ...and the engine's record says the access folded into a variable, as it did before the host made the word volatile
                AccessFate f;
                f.insn = m.insn;
                f.write = m.write;
                f.size = m.size;
                f.fate = AccessFate::FoldedOrdinary;
                f.address = m.address;
                f.constantAddress = true;
                r.accessFates.push_back(f);
            }
        }
        r.accesses = keep;
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
    else if (gMutateKind == "claims")
    {
        // A summary that says its function stores nowhere, returns no pointer and moves r15 by an amount it did not move it by.
        if (r.prototype.known)
        {
            r.prototype.effects.storesThrough = 0;
            r.prototype.effects.outOrigins = 0;
            r.prototype.effects.frameWords = 0;
            if (r.prototype.effects.spDelta != CallEffects::kSpUnknown) r.prototype.effects.spDelta += 4;
        }
    }
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
    else if (dts::MutateMapping(r, gMutateKind)) {}
    else if (gMutateKind == "data")
        for (const FnInfo& f : s.fns)
            if (!f.layout.data.empty()) r.body.push_back(f.layout.data[0]);
    (void)fi;
}

std::string OriginTextUnderTest(const Line& l) { return dts::MutatedOriginText(gMutateKind, l); }

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
// Whether a text returns a canary: a `return` line that holds it (0x5a, or in decimal), or names a variable it was assigned to, with no
// call between that could have written the variable. A call whose line takes an address (`&xStack_4 + p`) or carries the note of a frame
// address in r2 may write any stack local, so the stack locals are clean after it; the others (xVar1) are written by no call
// that is not passed their address. Straight-line texts only, which is what the canary programs are.
bool CanaryReturned(const std::string& text, uint32_t canary, std::string& offending, uint32_t canary2 = 0)
{
    char hex[16], dec[16], hex2[16], dec2[16];
    std::snprintf(hex, sizeof hex, "0x%x", canary);
    std::snprintf(dec, sizeof dec, "%u", canary);
    std::snprintf(hex2, sizeof hex2, "0x%x", canary2);
    std::snprintf(dec2, sizeof dec2, "%u", canary2);
    auto isWord = [](char c) { return std::isalnum(static_cast<unsigned char>(c)) || c == '_'; };
    auto has = [&](const std::string& line, const std::string& needle) {
        for (size_t p = line.find(needle); p != std::string::npos; p = line.find(needle, p + 1))
            if ((p == 0 || !isWord(line[p - 1])) && (p + needle.size() >= line.size() || !isWord(line[p + needle.size()]))) return true;
        return false;
    };
    std::set<std::string> tainted, exposed;
    auto taintedIn = [&](const std::string& line) {
        if (has(line, hex) || has(line, dec)) return true;
        for (const std::string& name : tainted)
            if (has(line, name)) return true;
        return false;
    };
    std::istringstream lines(text);
    std::string raw;
    while (std::getline(lines, raw))
    {
        const size_t at = raw.find_first_not_of(" \t");
        if (at == std::string::npos) continue;
        const std::string line = raw.substr(at);
        if (line.compare(0, 6, "return") == 0)
        {
            if (taintedIn(line) || (canary2 != 0 && (has(line, hex2) || has(line, dec2))))
            {
                offending = line;
                return true;
            }
            continue;
        }
        // A name whose address the text takes anywhere (`pxRam0600910c = &xStack_c;`) may be written by any later call: the stack locals are clean
        // after one (A3-44: the address handed through a word of memory). On the line itself too, as a call that is passed it.
        for (size_t amp = line.find('&'); amp != std::string::npos; amp = line.find('&', amp + 1))
        {
            size_t end = amp + 1;
            while (end < line.size() && isWord(line[end])) ++end;
            if (end > amp + 1) exposed.insert(line.substr(amp + 1, end - amp - 1));
        }
        const bool callLine = line.find("func_0x") != std::string::npos || line.find("(*") != std::string::npos;
        if (line.find('&') != std::string::npos || line.find("a frame address is in ") != std::string::npos)
            for (auto it = tainted.begin(); it != tainted.end();)
                it = it->find("Stack") != std::string::npos ? tainted.erase(it) : std::next(it);
        else if (callLine)
            for (auto it = tainted.begin(); it != tainted.end();)
                it = exposed.count(*it) ? tainted.erase(it) : std::next(it);
        const size_t eq = line.find(" = ");
        if (eq != std::string::npos && line.back() == ';' && line.find_first_of("(){}[]*") > eq)
        {
            const std::string name = line.substr(0, eq), rhs = line.substr(eq + 3);
            if (name.find(' ') != std::string::npos) continue;
            if (taintedIn(rhs)) tainted.insert(name);
            else tainted.erase(name);
        }
    }
    return false;
}

// A2-49 (accepted, P3): a function that calls a lent callee and returns its value, and compares that value (`tst r0,r0 ; bt`, or another
// compare of r0) with a branch whose arms do nothing the function needs. The engine decides what a function returns before it removes such a
// branch, so the compare is a use of r0 while the return is decided (the lent callee's 1-byte result reaches the RETURN through a zero-extension
// of a concatenation, not as the call's output, and the one thing that is accepted as a use-free return is the output itself), and the function
// is printed `void` and lent so: the same order of the passes as A2-45's dead store. A caller that reads r0 afterwards is withheld, so nothing is
// lost silently. The arms can hold dead writes (seed 62956's do) which are removed later, so the shape is the compare after the last call, not
// empty arms. The check does not count such a function's `void` as `return-dropped`; it is counted (`excluded.call-result-compared.return-dropped`)
// so that a run shows how often the grammar draws it.
bool ReadsR0ForFlag(uint16_t w)
{
    if (w == 0x2008 || w == 0x4015 || w == 0x4011) return true;        // tst r0,r0 ; cmp/pl r0 ; cmp/pz r0
    if ((w & 0xFF00) == 0x8800) return true;                           // cmp/eq #imm,r0
    switch (w & 0xF00F)
    {
    case 0x2008: case 0x3000: case 0x3002: case 0x3003: case 0x3006: case 0x3007: case 0x300C:   // tst, cmp/eq, hs, ge, hi, gt, str Rm,Rn
        return ((w >> 8) & 0xF) == 0 || ((w >> 4) & 0xF) == 0;
    default:
        return false;
    }
}

bool ComparesCallResult(const Subject& subject, const FnInfo& fn)
{
    uint32_t lastCall = 0;
    for (uint32_t a = fn.layout.start; a < fn.layout.end; a += 2)
    {
        const uint16_t w = subject.WordAt(a);
        if ((w & 0xF000) == 0xB000 || (w & 0xF0FF) == 0x400B || (w & 0xF0FF) == 0x0003) lastCall = a;   // bsr, jsr, bsrf
    }
    if (!lastCall) return false;
    for (uint32_t a = lastCall + 4; a < fn.layout.end; a += 2)
    {
        const uint16_t w = subject.WordAt(a);
        const bool branch = (w & 0xFF00) == 0x8900 || (w & 0xFF00) == 0x8B00 || (w & 0xFF00) == 0x8D00 || (w & 0xFF00) == 0x8F00;   // bt, bf, bt/s, bf/s
        if (branch && ReadsR0ForFlag(subject.WordAt(a - 2))) return true;
    }
    return false;
}

// (area 3 round 13 fix, A3-112) ...and the wider shape of A3-19, A3-41 and A3-53, accepted: a value a call returned that the function then
// uses for anything -- a store of it, a compare, a move to another register -- before the return, and leaves in r0 as scratch. Both arms of
// grammar 2 seed 2317870 store the call's r0 (`mov.b r0,@(1,r5)`; `mov.l r0,@-r4 ; mov.b r0,@r5`) before the `rts`; the lent `void` is
// right. Only when nothing after the last call writes r0, so the r0 at the return is the call's. Read from the disassembly's operands: r0 in
// any operand but a last one that is r0 itself is a read; a last operand r0 is a write (a compare or a test writes none).
bool UsesCallResult(const Subject& subject, const FnInfo& fn)
{
    uint32_t lastCall = 0;
    for (uint32_t a = fn.layout.start; a < fn.layout.end; a += 2)
    {
        const uint16_t w = subject.WordAt(a);
        if ((w & 0xF000) == 0xB000 || (w & 0xF0FF) == 0x400B || (w & 0xF0FF) == 0x0003) lastCall = a;   // bsr, jsr, bsrf
    }
    if (!lastCall) return false;
    bool read = false;
    for (uint32_t a = lastCall + 4; a < fn.layout.end; a += 2)
    {
        const sfe::DisassembledInstruction d = sfe::Sh2Decode(a, subject.WordAt(a));
        if (!d.IsValid) return false;
        std::vector<std::string> ops;
        int depth = 0;
        std::string cur;
        for (char ch : d.Operands)
        {
            if (ch == '(') ++depth;
            if (ch == ')') --depth;
            if (ch == ',' && depth == 0) { ops.push_back(cur); cur.clear(); continue; }
            cur += ch;
        }
        if (!cur.empty()) ops.push_back(cur);
        auto namesR0 = [](const std::string& o) {
            for (size_t k = o.find("r0"); k != std::string::npos; k = o.find("r0", k + 1))
                if (k + 2 >= o.size() || !std::isdigit(static_cast<unsigned char>(o[k + 2]))) return true;
            return false;
        };
        const bool compare = d.Mnemonic.compare(0, 3, "cmp") == 0 || d.Mnemonic == "tst";
        for (size_t k = 0; k < ops.size(); ++k)
        {
            const bool last = k + 1 == ops.size();
            if (last && ops[k] == "r0" && !compare) return false;   // r0 written after the last call: not the call's at the return
            if (ops[k] == "@r0+" || ops[k] == "@-r0") return false;   // ...or moved
            if (namesR0(ops[k])) read = true;
        }
    }
    return read;
}

// ---- Check 10b (A3-54): the rest of CallEffects against a finished run -----------------------------------------------------------------------
// A caller's analysis, the probe and the rules of A3-21, A3-31 and A3-39 rest on more than `preserved`: where the stores of a function go
// (`storesThrough`, `frameWords`), what its non-preserved registers can hold at the return (`outOrigins`) and where r15 ends (`spDelta`). A
// run shows each as a fact, so a claim that the run contradicts is a defect of the summary: a store at or above the entry r15 needs
// origin::kFrame and its word in `frameWords` (or kFrameAny), a store into the arena of an argument register (or of GBR) needs that
// register's bit or origin::kMemory (a pointer loaded from somewhere), a register that does not end as it began and holds a pointer into an
// arena or into the stack needs the origin that pointer comes from. The arenas are the 4 KiB pages the runner starts r4-r7 and GBR in (kScratch4 up);
// everything else a store reaches (hardware, mailboxes, a literal pointer's target) is not asked about. Stores of callees count: the caller's
// summary is built from theirs. Returns "" or what the run contradicts.
constexpr uint32_t kArenaBase = 0x06020000u;   // r4's page; r5, r6, r7 and GBR follow at 0x1000 each
int ArenaOf(uint32_t addr)
{
    addr &= 0x0FFFFFFFu;
    return addr >= kArenaBase && addr < kArenaBase + 5 * 0x1000u ? static_cast<int>((addr - kArenaBase) >> 12) : -1;
}
uint32_t ArenaBit(int arena) { return arena < 4 ? 1u << (4 + arena) : 1u << 17; }

// (A3-63) The register whose start value 'v' is, give or take a small constant: a pointer the code stepped (the grammar's displacements and
// increments stay within 0x80 bytes, a frame within 0x200). r4-r7 and GBR in their own arenas, and only one the run read (an OR of two arena
// pointers is a third's start by chance: r5 | r6 is r7's, seed 2031); r15 in the stack; -1 for anything else (a value that lands in an arena by
// arithmetic on another register, `extu.w r5,r1 ; xor r5,r1`, is 0x100 below r4's start, and is not asked about).
// (area 3 round 12 fix, A3-100) ...and not one an AND, OR or XOR of two other start values the run read lands on: 0x06024100 & 0x06021100 (GBR and
// r5) is r4's start 0x06020100 exactly, seed 2007995, where the callee computed `stc gbr,r2 ; and r5,r2` and r2 was taken for a pointer from r4.
bool BitwiseOfOthers(const RunOut& run, uint32_t v, int k)
{
    std::vector<uint32_t> others;
    for (int j = 4; j < 8; ++j)
        if (j != k && ((run.use.readFirst >> j) & 1u)) others.push_back(run.init[j]);
    if (k != 17 && ((run.use.readFirst >> 17) & 1u)) others.push_back(run.initState[0]);
    for (size_t a = 0; a < others.size(); ++a)
        for (size_t b = a + 1; b < others.size(); ++b)
            for (uint32_t w : { others[a] & others[b], others[a] | others[b], others[a] ^ others[b] })
            {
                const int32_t c = static_cast<int32_t>(v - w);
                if (c > -0x80 && c < 0x80) return true;
            }
    // (area 3 round 13 fix, A3-115) ...or a byte or halfword swap of another, give or take a step: `mov.b @r5+,r0 ; swap.b r5,r1` makes
    // r5's start 0x06021100 plus 1, swapped, 0x06020111, which lies 0x11 past r4's start (grammar 2 seed 18759, `claim-wrong:outOrigins`).
    // The value is computed from r5, which outOrigins names. Undone, the swap lands within a step of the other start value.
    for (uint32_t w : { (v & 0xFFFF0000u) | ((v & 0xFFu) << 8) | ((v >> 8) & 0xFFu), (v << 16) | (v >> 16) })
        for (uint32_t o : others)
        {
            const int32_t c = static_cast<int32_t>(w - o);
            if (c > -0x80 && c < 0x80) return true;
        }
    return false;
}
int PointerRegister(const RunOut& run, uint32_t v)
{
    for (int k = 4; k < 8; ++k)
    {
        const int32_t c = static_cast<int32_t>(v - run.init[k]);
        if (c > -0x80 && c < 0x80 && ArenaOf(v) == k - 4) return (run.use.readFirst >> k) & 1u && !BitwiseOfOthers(run, v, k) ? k : -1;
    }
    {
        const int32_t c = static_cast<int32_t>(v - run.initState[0]);
        if (c > -0x80 && c < 0x80 && ArenaOf(v) == 4) return (run.use.readFirst >> 17) & 1u && !BitwiseOfOthers(run, v, 17) ? 17 : -1;
    }
    const int32_t c = static_cast<int32_t>(v - run.init[15]);
    const uint32_t m = v & 0x0FFFFFFFu;
    return c > -0x200 && c < 0x200 && m >= kStackLo && m < kStackHi ? 15 : -1;
}
uint32_t PointerOrigin(int k) { return k == 15 ? origin::kStack : 1u << k; }
std::string PointerName(int k) { return k == 15 ? std::string("r15 (the stack)") : k == 17 ? std::string("gbr") : "r" + std::to_string(k); }

std::string EffectClaimsContradicted(const CallEffects& e, const RunOut& run, std::string* sigOut = nullptr)
{
    auto fail = [&](const char* sig, const std::string& what) {
        if (sigOut) *sigOut = sig;
        return what;
    };
    if (!((e.preserved >> 15) & 1u) && e.spDelta != CallEffects::kSpUnknown &&
        run.fin[15] - run.init[15] != static_cast<uint32_t>(e.spDelta))
        return fail("claim-wrong:spDelta", "the prototype says r15 ends " + std::to_string(e.spDelta) + " bytes from where it began and a run ends " +
                                               std::to_string(static_cast<int32_t>(run.fin[15] - run.init[15])) + " from it");
    for (const TraceRec& t : run.trace)
    {
        if (!t.write) continue;
        const uint32_t a = t.addr & 0x0FFFFFFFu;
        if (a >= kStackTop && a < kStackHi)
        {
            const uint32_t word = (a - kStackTop) / 4;
            if (!(e.storesThrough & origin::kFrame))
                return fail("claim-wrong:storesThrough", "a run stores at " + Hex(a) + ", at or above the entry r15, and storesThrough does not name the caller's frame");
            if (!(e.frameWords & CallEffects::kFrameAny) && (word >= 31 || !((e.frameWords >> word) & 1u)))
                return fail("claim-wrong:frameWords", "a run stores at " + Hex(a) + ", word " + std::to_string(word) + " from the entry r15, and frameWords does not name it");
            continue;
        }
        int arena = ArenaOf(a);
        // (area 3 round 14 fix, A3-123) ...but not an address that is a byte/halfword swap (or bitwise combination) of other start values the
        // run read, which lands in a register's arena by chance (the byte swap of r5's start 0x06021100 is 0x06020011, in r4's arena: grammar 2
        // seed 2616394, the same oracle false positive as A3-100 and A3-115, here in the storesThrough path). The store is through the register
        // the swapped value came from, which storesThrough names, not r4.
        if (arena >= 0 && arena < 4 && BitwiseOfOthers(run, a, 4 + arena)) arena = -1;
        if (arena >= 0 && !(e.storesThrough & (ArenaBit(arena) | origin::kMemory)))
            return fail("claim-wrong:storesThrough", "a run stores at " + Hex(a) + ", in the arena of " + (arena < 4 ? "r" + std::to_string(4 + arena) : std::string("gbr")) +
                                                         ", and storesThrough names neither that register nor memory");
        // (area 3 round 11 fix) ...and a store below the start value of r4-r7 in that register's arena (the runner starts each 0x100 into its
        // page, so `mov.l r1,@-r4`, a negative displacement or a laundered pointer lands there) needs that register in storesBelow: a caller
        // that hands such a function an exact frame address exposes the locals below it only on that claim (A3-71, A3-82, A3-83). Asked when
        // the run read the register and storesThrough names it (a store through memory it loaded is the storesThrough check's).
        if (arena >= 0 && arena < 4 && ((run.use.readFirst >> (4 + arena)) & 1u) && (e.storesThrough & ArenaBit(arena)) &&
            a < (run.init[4 + arena] & 0x0FFFFFFFu) && !(e.storesBelow & ArenaBit(arena)))
            return fail("claim-wrong:storesBelow", "a run stores at " + Hex(a) + ", below r" + std::to_string(4 + arena) + "'s start " +
                                                       Hex(run.init[4 + arena]) + ", and storesBelow does not name r" + std::to_string(4 + arena));
    }
    // (A3-63) What it stores: a pointer (a register's start value stepped, or a stack address) stored at or above the entry r15 needs that origin,
    // or memory, in `frameValues`; stored anywhere else outside the stack (memory the function does not track: an arena, a literal's target,
    // hardware), in `leaks`. A caller's `escaped` flag, the notes at its calls and A3-21's hand-back rest on both.
    for (const TraceRec& t : run.trace)
    {
        if (!t.write || t.size != 4) continue;
        const int k = PointerRegister(run, static_cast<uint32_t>(t.value));
        if (k < 0) continue;
        const uint32_t a = t.addr & 0x0FFFFFFFu;
        const uint32_t want = PointerOrigin(k) | origin::kMemory;
        if (a >= kStackTop && a < kStackHi)
        {
            if (!(e.frameValues & want))
                return fail("claim-wrong:frameValues", "a run stores " + Hex(static_cast<uint32_t>(t.value)) + ", a pointer from " + PointerName(k) + ", at " + Hex(a) +
                                                           ", at or above the entry r15, and frameValues names neither it nor memory");
        }
        else if ((a < kStackLo || a >= kStackHi) && !(e.leaks & want))
            return fail("claim-wrong:leaks", "a run stores " + Hex(static_cast<uint32_t>(t.value)) + ", a pointer from " + PointerName(k) + ", at " + Hex(a) +
                                                 ", and leaks names neither it nor memory");
    }
    for (int r = 0; r < 15; ++r)
    {
        if ((e.preserved >> r) & 1u) continue;
        const uint32_t v = run.fin[r];
        const int arena = ArenaOf(v);
        // (Arithmetic on other registers can land in an arena by chance: `extu.w r5,r1 ; xor r5,r1` is 0x06020000 for r5 = 0x06021100, seed 12548. A value
        // there needs some origin, the register's own, memory or any register's; not an empty set.)
        if (arena >= 0 && !(e.outOrigins & (origin::kRegisters | origin::kMemory)))
            return fail("claim-wrong:outOrigins", "r" + std::to_string(r) + " ends as " + Hex(v) + ", a pointer into the arena of " +
                                                      (arena < 4 ? "r" + std::to_string(4 + arena) : std::string("gbr")) + ", and outOrigins names no register and not memory");
        const uint32_t m = v & 0x0FFFFFFFu;
        if (m >= kStackLo && m < kStackHi && !(e.outOrigins & (origin::kStack | origin::kRegisters | origin::kMemory)))
            return fail("claim-wrong:outOrigins", "r" + std::to_string(r) + " ends as " + Hex(v) + ", a stack address, and outOrigins names nothing it can come from");
        // (A3-63) ...and when it is a register's start value stepped, or the entry r15's, that register (the stack), or memory: not some register.
        const int k = PointerRegister(run, v);
        if (k >= 0 && !(e.outOrigins & (PointerOrigin(k) | origin::kMemory)))
            return fail("claim-wrong:outOrigins", "r" + std::to_string(r) + " ends as " + Hex(v) + ", a pointer from " + PointerName(k) + ", and outOrigins names neither it nor memory");
    }
    return std::string();
}

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
        // The engine's simplification did not settle (patch 0055 bounds the passes of a repeating action; two rules that undid each other, A2-59,
        // ran until the host cancelled): a finding whatever the text, since a cycle is an engine defect.
        for (const std::string& w : r.warnings)
            if (w.find("Exceeded maximum repeats") != std::string::npos) add("unsettled", w);
        // The frame half of the conservation law (A3-124, patch 0068): a write folded into the frame that went unread while the
        // code reads a byte of it later. The trace cannot show it (a frame access is no bus access the ledger pairs), the record can.
        for (const AccessFate& f : r.accessFates)
            if (f.fate == AccessFate::MalformedPiece)
                add("malformed-piece", "a piece of " + std::to_string(f.size) + " byte(s) at " + Hex(f.insn) + " has sizes that do not add up (A3-127)");
        for (const dts::FrameLoss& l : dts::FrameLosses(r, dts::SnapshotWords(snap)))
            add("frame-lost:W" + std::to_string(l.size), "the frame write of " + std::to_string(l.size) + " byte(s) at " + Hex(l.write) +
                    " (frame offset " + Hex(l.offset) + ") went unread, and the read at " + Hex(l.read) + " reads it");
        // The canary (A3-34): the function stored fn.canary in a local that a callee overwrote, and no run returned it. A `return` that
        // contains it (as 0x5a, or in decimal) is the local folded across the call, the value from before the callee wrote it.
        if (fn.canary != 0)
        {
            bool returned = false;
            for (const RunOut& run : runs) returned = returned || (run.finished && (run.fin[0] == fn.canary || (fn.canary2 != 0 && run.fin[0] == fn.canary2)));
            count("canary.checked");
            std::string line;
            if (!returned && (!fn.canaryIfLent || r.prototype.known) && CanaryReturned(r.Text(), fn.canary, line, fn.canary2))
                add("canary:return", "the function stores " + Hex(fn.canary, 2) + " in a local that a callee overwrites and the run returns what the callee wrote, "
                                      "but the text returns the value from before the call: `" + line + "`");
        }

        auto inMarked = [&](uint32_t addr) {
            for (const HardwareRange& h : marked)
                if (h.first <= addr && addr <= h.last) return true;
            return false;
        };
        // ---- Area 5, round 7 (A5-52): a word of work RAM the function reaches under two addresses is one memory ----
        // The bus gives a work-RAM word as many addresses as it has partitions, undecoded bits (28-27) and 1 MiB repeats; the emulator's
        // memory is the same bytes under all of them. The engine's data flow keys memory by address, and forwards a store to a load past
        // a write to the other name -- `xRam26001000 = 5; xRam06001000 = 7; return 5;` -- so the load is folded away with a wrong value,
        // and the ledger does not list it (an access of ordinary memory at a constant address is folded by design). So an access of the
        // traced function to a word it also reaches at an address that does not overlap its own is listed like marked memory:
        // present in the ledger, whatever the request marks. The aliasing is read off the trace, by the emulator's memory model, not off
        // the decompiler's output. (Only accesses the engine takes at a constant address count: one through a pointer it cannot resolve
        // is a plain load or store, which the data flow treats as possibly reaching any word, and which the ledger lists.)
        std::set<uint32_t> aliased;
        {
            auto constantToEngine = [&](const TraceRec& t) {
                for (const AccessFate& f : r.accessFates)
                    if (f.insn == t.insn && f.write == t.write && f.size == t.size && f.constantAddress && f.address == t.addr && f.fate == AccessFate::FoldedOrdinary)
                        return true;
                for (const MemoryAccess& m : r.accesses)
                    if (m.insn == t.insn && m.write == t.write && m.size == t.size && m.direct && m.address == t.addr) return true;
                return false;
            };
            struct Acc { uint32_t addr; int size; uint32_t lo, hi; };
            std::vector<Acc> accs;
            std::set<std::pair<uint32_t, int>> once;
            for (const TraceRec& t : trace)
            {
                uint32_t lo = 0, hi = 0;
                // (an access in an associative-purge partition, 0x4 or 0xA, names the line of the word it purges)
                // ...the whole 16-byte line (sh7095.inc SH7095_Cache_AssocPurge compares the line's tag, A5-72)
                const bool purge = (t.addr >> 29) == 2 || (t.addr >> 29) == 5;
                if (InScratch(t.addr) || InStack(t.addr) || !Env::RamRange(purge ? (t.addr & 0x07FFFFF0u) : t.addr, purge ? 16 : t.size, lo, hi) || !constantToEngine(t) || !once.insert(std::make_pair(t.addr, t.size)).second) continue;
                accs.push_back({ t.addr, t.size, lo, hi });
            }
            auto overlap = [](uint32_t a0, uint32_t a1, uint32_t b0, uint32_t b1) { return a0 <= b1 && b0 <= a1; };
            for (size_t i = 0; i < accs.size() && accs.size() <= 4000; ++i)
                for (size_t j = i + 1; j < accs.size(); ++j)
                    if (overlap(accs[i].lo, accs[i].hi, accs[j].lo, accs[j].hi) &&
                        !overlap(accs[i].addr, accs[i].addr + accs[i].size - 1, accs[j].addr, accs[j].addr + accs[j].size - 1))
                    {
                        aliased.insert(accs[i].addr);
                        aliased.insert(accs[j].addr);
                    }
            count("alias.words", static_cast<int>(aliased.size()));
        }
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
            const size_t handed = HandedOverAt(r, run);
            const size_t handedOver = handed == SIZE_MAX ? SIZE_MAX : handed + 2;
            std::map<std::tuple<int, uint32_t, bool, int>, std::vector<uint32_t>> steps;
            for (const TraceRec& t : run.trace)
                if (t.depth == 0 && static_cast<size_t>(t.step) < handedOver) steps[std::make_tuple(t.step, t.insn, t.write, t.size)].push_back(t.addr);
            for (const auto& st : steps)
            {
                std::vector<const MemoryAccess*> entries;
                for (const MemoryAccess& m : r.accesses)
                    if (m.insn == std::get<1>(st.first) && m.write == std::get<2>(st.first) && m.size == std::get<3>(st.first))
                        entries.push_back(&m);
                std::vector<uint32_t>& out = lost[std::make_tuple(std::get<1>(st.first), std::get<2>(st.first), std::get<3>(st.first))];
                for (uint32_t a : UnaccountedAddresses(entries, st.second, [&](uint32_t x) { return InScratch(x) || InHardwareRange(x) || inMarked(x) || aliased.count(x) != 0; }))
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
                else if (aliased.count(addr)) from.insert("alias");
                else from.insert("RAM");
                if (InScratch(addr) || InHardwareRange(addr) || inMarked(addr) || aliased.count(addr)) exempt = false;
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
                    fate = f.fate == AccessFate::Unreachable ? "removed-unsaid" : dts::FateLabel(f.fate);
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
                if (!PointerPrintsFromHardware(r, t.insn, HardwareRegistersCovering(t.addr, t.size), canon, unrelated))
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
        // ---- Area 5, round 9 (A5-70): what a register's access does is said for the register the pointer reached ----
        // A read or a write through a pointer that the output derives from some register address is said, for each address the pointer can hold,
        // as a direct access to it would be ("if the pointer holds 0x...: ..."): the read effect of the register and the cautions of the access's
        // width (the cache caution is rule 3b's). So the effect of the register the emulator traced must be among the notes of the instruction,
        // unless they say the pointer may hold other values or is stepped in a loop (the registers after the first are then not said, by design).
        // Found by A5-70 (b) and (c): `p = (p and 0xFFFFFFF0) xor 4` named the first register, or a register never read, and left out the one
        // read for ever after.
        {
            size_t nregs = 0;
            const HardwareRegister* every = SaturnHardwareRegisters(nregs);
            std::vector<const HardwareRegister*> allRegs;
            for (size_t i = 0; i < nregs; ++i) allRegs.push_back(&every[i]);
            std::set<uint32_t> excused;   // instructions whose notes leave the other values of the pointer unsaid
            for (const HardwareNote& n : r.hardwareNotes)
                if (n.text.find("it may hold other values too") != std::string::npos || n.text.find("is stepped in a loop") != std::string::npos ||
                    n.text.find("if the index is 0") != std::string::npos)
                    excused.insert(n.address);
            std::set<std::tuple<uint32_t, uint32_t, int, bool>> checked;
            for (const TraceRec& t : trace)
            {
                uint32_t canon = 0;
                if (t.depth != 0 || !r.InBody(t.insn) || excused.count(t.insn) || SaidMissing(r, t.insn) || !HardwareAlias(t.addr, canon)) continue;
                const uint16_t word = subject.WordAt(t.insn);
                if ((word & 0xF000) == 0xD000 || (word & 0xF000) == 0x9000) continue;
                bool shown = false;
                for (const MemoryAccess& m : r.accesses) shown |= m.insn == t.insn && m.direct && m.address == t.addr && m.write == t.write;
                bool unrelated = false;
                if (shown || !checked.insert(std::make_tuple(t.insn, t.addr, t.size, t.write)).second) continue;
                if (!PointerPrintsFromHardware(r, t.insn, allRegs, canon, unrelated))
                {
                    if (unrelated) count("pointer.effect.unrelated");   // (A5-82) the registers the output names are not the one reached
                    continue;
                }
                count("pointer.effect.derived");
                std::set<std::string> want;
                const std::vector<CoveredRegister> covered = CoveredRegisters(t.addr, t.size, t.write);
                for (const HardwareRegister* reg : HardwareRegistersReached(AccessAddress(t.addr, t.size), t.size, t.write))
                {
                    bool effect = true;
                    for (const CoveredRegister& c : covered)
                        if (c.reg == reg) effect = (c.cell.flags & kWidthEffect) != 0;
                    if (!t.write && reg->onRead && effect) want.insert(std::string(reg->name) + ": " + reg->onRead);
                }
                for (const NarrowEffect& e : NarrowAccessEffects(t.addr, t.size, t.write))
                    if (e.text.find("cache read") == std::string::npos) want.insert(e.text);
                for (const std::string& w : want)
                {
                    bool said = false;
                    for (const HardwareNote& n : r.hardwareNotes) said |= n.address == t.insn && n.text.find(w) != std::string::npos;
                    count(said ? "pointer.effect.said" : "pointer.effect.unsaid");
                    if (!said)
                        add("pointer-effect-unsaid", "the " + std::string(t.write ? "write" : "read") + " of " + std::to_string(t.size) + " byte(s) at " + Hex(t.insn) + " reached " +
                                                         Hex(t.addr) + " through a pointer the output derives from register addresses, and no note says: " + w);
                }
            }
        }
        // ---- Area 5, round 9 (A5-73): a store to the A-bus cartridge areas that the engine folds is cautioned ----
        // The cartridge decides what a write does (none: nowhere; a ROM cartridge: dropped; backup RAM: a byte at an odd address only), and a later read
        // of the address in the function is shown as the value written. The decompiler cannot know the cartridge, and says so on the store.
        for (const TraceRec& t : trace)
        {
            const uint32_t bus = t.addr & 0x07FFFFFFu;
            if (!t.write || t.depth != 0 || (t.addr >> 29) > 1 || bus < 0x02000000u || bus > 0x04FFFFFFu || !r.InBody(t.insn) || SaidMissing(r, t.insn)) continue;
            bool folded = false;
            for (const AccessFate& f : r.accessFates)
                folded |= f.insn == t.insn && f.write && f.fate == AccessFate::FoldedOrdinary && f.constantAddress && f.address == t.addr;
            if (!folded) continue;
            bool said = false;
            for (const HardwareNote& n : r.hardwareNotes)
                said |= n.address == t.insn && n.caution && n.text.find("a write to the cartridge area") != std::string::npos;
            count(said ? "cart.write.said" : "cart.write.unsaid");
            if (!said)
                add("cart-write-unsaid", "the write of " + std::to_string(t.size) + " byte(s) at " + Hex(t.insn) + " to " + Hex(t.addr) +
                                             ", a cartridge area, is folded and not cautioned");
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
        // ...and only what the run does before it is handed to code the output does not follow (A4-17): an indirect jump the output
        // treats as a call (a table it could not recover, a signed bound) runs the cases in the callee, and a case that comes back to an
        // instruction the text holds below the jump makes that instruction without the ones the text puts before it. The text says so
        // ("Treating indirect jump as call"); it is not a join across a branch.
        auto Ran = [&](const RunOut& run, uint32_t insn) {
            const size_t handed = HandedOverAt(r, run);
            for (size_t k = 0; k < run.pcs.size() && (handed == SIZE_MAX || k <= handed); ++k)
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
        // ---- Area 7: the mapping from the text to the instructions (MappingOracle.h) ----
        {
            const dts::MappingAudit audit = dts::AuditMapping(
                r, [&](uint32_t pc, uint16_t& w) {
                    if (subject.UnitOf(pc) < 0) return false;
                    w = subject.WordAt(pc);
                    return true;
                }, "", OriginTextUnderTest);
            auto flag = [&](const char* sig, int n) {
                if (n) add(sig, std::to_string(n) + " time(s); first: " + (audit.problems.empty() ? std::string("-") : audit.problems[0]));
            };
            flag("map-odd", audit.odd);
            flag("map-outside", audit.outside);
            flag("map-not-instruction", audit.notInsn);
            flag("map-first-addr", audit.firstAddrWrong);
            flag("map-range", audit.badRange);
            flag("map-call-token", audit.callWithoutCall);
            flag("map-unmarked", audit.unmarked);
            flag("map-hover", audit.misnamed);
            flag("map-access-unmapped", audit.accessUnmapped);
            flag("map-call-unmapped", audit.callUnmapped);
            flag("map-call-removed", audit.callGoneUnsaid);
            flag("map-round-trip", audit.roundTrip);
            count("map.lines", audit.lines);
            count("map.lines.one", audit.single);
            count("map.lines.several", audit.multi);
            count("map.lines.several.apart", audit.multiNonAdjacent);
            // The executed instructions of the body, and what carries each: a token of its own, or none. A call in the final
            // p-code must have one (the line `f(x)` is the call's), and so must an access the ledger lists (the audit above).
            std::set<uint32_t> carried;
            for (const Line& l : r.lines)
                for (const Token& t : l.tokens)
                    if (t.addr) carried.insert(t.addr);
            std::set<uint32_t> callInsns;
            for (const CallSite& c : r.calls) callInsns.insert(c.insn);
            std::set<uint32_t> fated;   // an access whose fate the engine recorded (folded into a variable, dead, removed)
            for (const AccessFate& f : r.accessFates) fated.insert(f.insn);
            for (uint32_t pc : executed)
            {
                if (!r.InBody(pc)) continue;
                count("map.executed");
                const sfe::DisassembledInstruction d = sfe::Sh2Decode(pc, subject.WordAt(pc));
                if (callInsns.count(pc) && !carried.count(pc))
                    add("map-call-unmapped", "the call at " + Hex(pc) + " was executed and no token of the text came from it");
                if (carried.count(pc))
                {
                    count("map.executed.token");
                    continue;
                }
                uint16_t prev = 0;
                const bool slot = r.InBody(pc - 2) && subject.UnitOf(pc - 2) >= 0 && (prev = subject.WordAt(pc - 2), sfe::Sh2Decode(pc - 2, prev).HasDelaySlot);
                const bool mem = d.Operands.find('@') != std::string::npos && d.PcRel == sfe::Sh2PcRel::None && !d.IsBranch;
                count(slot ? "map.executed.bare.slot" : d.IsCall || d.IsBranch ? "map.executed.bare.branch" : mem ? "map.executed.bare.memory" :
                      "map.executed.bare.operation");
                if (mem && !slot)
                {
                    bool listed = false;
                    for (const MemoryAccess& m : r.accesses) listed = listed || m.insn == pc;
                    // Listed in the ledger it has a token (the audit above). Not listed: folded into a variable by design,
                    // or the stack's own; the engine's record says which, or the output says it was removed.
                    if (listed) add("map-access-bare", "the access at " + Hex(pc) + " is in the ledger and carries no token");
                    else if (!fated.count(pc) && !SaidMissing(r, pc)) count("map.executed.bare.memory.unaccounted");
                }
            }
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
                    // A switch on the loaded address, not on the index (A2-16, A4-10, A4-11): the engine recovered the table when the function
                    // was still partial, and in the finished one a bound check whose T bit is part of a whole SR word that something else also
                    // uses (`stc sr`, `div1`, an SR returned) is not a compare any more, so the model that matched it finds no guard and falls
                    // back to the trivial one -- one case for each destination, labelled with its address, the bound check an `if` before
                    // it. The text is right; the label of the index is the address the table holds for it.
                    bool byAddress = false;
                    for (size_t k = swBegin; k < swEnd && !byAddress; ++k)
                        for (long label : labels[k].cases)
                            for (uint32_t e : tl.entry)
                                if (static_cast<uint32_t>(label) == e || (tl.hasPre && static_cast<uint32_t>(label) == tl.preEntry)) byAddress = true;
                    if (byAddress) count("table.switch-on-address");
                    if (GroupAddrs(r.lines, labels, swBegin, swEnd, byAddress ? static_cast<long>(dest.second) : dest.first, group))
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
            // (area 3 round 13 fix, A3-111) A function that reads a stack word nothing in the run wrote first (an uninitialised local, the free
            // stack below its r15) reads what an earlier run or an earlier call left there: a register stored there by one call and read by the
            // next makes the output depend on that register through memory no signature passes (grammar 2 seed 2310198: function 1 pushes and
            // pops r1, and function 2, called after, reads that freed word as an uninitialised byte of its frame, `mov.b @r15,r2`). An
            // indeterminate value in C, which the text shows as an uninitialised local. Each run is repeated with the 512 bytes below the entry
            // r15 refilled and every word refilled as the stack pointer rises past it (Machine::SetScrub); a function whose behaviour changes
            // reads a stale word, and its dependences are not counted (`excluded.stale-stack-read.*`).
            bool staleStack = false;
            for (const RunOut& base : runs)
            {
                if (!base.finished || staleStack) continue;
                RunSpec rs;
                rs.mode = base.mode;
                rs.volatileRun = volatileRun;
                if (base.selValue != RunOut::kNoSel && fn.sel >= 4)
                {
                    rs.selReg = fn.sel;
                    rs.selValue = static_cast<uint32_t>(base.selValue);
                }
                rs.stackFill = 0x5AA5C33Cu;
                const RunOut other = runner.One(fn, rs);
                if (!other.addressError && !SameBehaviour(base, other, true, marked)) staleStack = true;
            }
            if (staleStack) count("stale-stack-read.functions");
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
                if (matters && staleStack) count("excluded.stale-stack-read.param-dropped");
                else if (matters)
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
                            // (area 3 round 12 fix, A3-101) r0 that ends as it began in both runs was passed through, not computed: `and r0,r0`
                            // in the slot of the rts, `mov r0,r0` (the identities the host and the engine take for nops, A2-43) write it with
                            // itself, so "written on every path" held and the perturbed start value came back out (seed 2126605).
                            const bool r0Through = base.fin[0] == base.init[0] && other.fin[0] == other.init[0];
                            if (!SameBehaviour(base, other, (compareR0 || (anyFinished && r0Always)) && !r0Through, marked))
                            {
                                matters = true;
                                break;
                            }
                        }
                    }
                    const std::string name = k < 16 ? "r" + std::to_string(k) : kStateNames[k - 17];
                    if (matters && staleStack) count("excluded.stale-stack-read.input-dropped");
                    else if (matters)
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
            // ...on every run that finished: a function the generator declared to return that does not write r0 on some path (a shrink step
            // that took out the instruction that did, A2-50) is not a counterexample, whatever its prototype says.
            if (fn.intent && fn.returns && pr.ret.kind == TypeKind::Void && !everUnwritten)
            {
                // A return chosen by a parameter the signature holds, or by a constant, stays lent void (A3-62, accepted: no input is dropped, and a
                // caller that reads r0 afterwards is withheld); chosen by any other input it is withheld (the return rule, A3-56 to A3-62).
                const bool heldChooser = (fn.choiceBy >= 4 && fn.choiceBy <= 7 && static_cast<size_t>(fn.choiceBy - 3) <= pr.params.size()) ||
                                         (fn.choiceBy == Gen::kChoiceStack && pr.params.size() >= 5) || fn.choiceBy == Gen::kChoiceConst;
                if (ComparesCallResult(subject, fn)) count("excluded.call-result-compared.return-dropped");   // A2-49, accepted
                else if (UsesCallResult(subject, fn)) count("excluded.call-result-used.return-dropped");   // A3-112: A3-19's shape, accepted
                else if (fn.choiceBy >= 0 && heldChooser) count("excluded.choice-held.return-dropped");
                // A function that runs on or jumps into a choice returns its value, but chose its chooser's value itself (seed 706074): the choice is
                // checked where it is drawn.
                else if (fn.choiceBy >= 0 && fn.choiceInherited) count("excluded.choice-inherited.return-dropped");
                else if (fn.choiceBy >= 0) add("return-dropped:choice", "r0 is one of two values an input of the function chooses between and the prototype returns nothing");
                else add("return-dropped", "the function writes r0 as its result on every path and the prototype returns nothing");
            }
            // A parameter copied to r0 on one path and r0 left alone on the other (A3-20): the engine takes the join for no return, and
            // a signature that drops the parameter drops the value with it. One it holds is the accepted scratch of compiled code.
            if (fn.intent && fn.partial && pr.ret.kind == TypeKind::Void)
            {
                count("partial.void");
                for (const RunOut& run : runs)
                {
                    if (!run.finished || run.fin[0] == run.init[0]) continue;
                    int k = -1;
                    for (int j = 4; j < 8; ++j)
                        if (run.fin[0] == run.init[j]) k = j;
                    if (k < 0) continue;
                    if (static_cast<size_t>(k - 3) <= pr.params.size())
                    {
                        count("partial.void.held");
                        continue;
                    }
                    add("return-dropped:partial", "r0 ends as r" + std::to_string(k) + " on a path of the function and the lent prototype returns nothing and has " +
                                                      std::to_string(pr.params.size()) + " parameter(s)");
                    break;
                }
            }
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
            // ...and the rest of the summary (A3-54): where the stores go, what the outputs can hold, where r15 ends.
            for (const RunOut& run : runs)
            {
                if (!run.finished) continue;
                count("claims.checked");
                std::string sig;
                const std::string what = EffectClaimsContradicted(pr.effects, run, &sig);
                if (!what.empty())
                {
                    add(sig, what);
                    break;
                }
            }
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
            // The registers the function reads on purpose: its own, and those of every function its body runs on into (a function without an
            // `rts` falls into the next one, the engine shows that code as part of it, and a read there comes after the call: the text's
            // `unaff_r14` before the call is right, A3-52).
            uint32_t readsOnPurpose = fn.reads;
            for (size_t j = 0; j < subject.fns.size(); ++j)
                if (static_cast<int>(j) != fi && r.InBody(subject.fns[j].layout.entry)) readsOnPurpose |= subject.fns[j].reads;
            static const struct { int bit; const char* name; } kKept[] = { { 8, "r8" }, { 9, "r9" }, { 10, "r10" }, { 11, "r11" }, { 12, "r12" },
                                                                           { 13, "r13" }, { 14, "r14" }, { 17, "gbr" }, { 18, "vbr" } };
            const std::string text = r.Text();
            for (const auto& k : kKept)
            {
                if ((readsOnPurpose >> k.bit) & 1u) continue;
                bool changed = false;
                for (uint32_t c : r.calledAt)
                {
                    const auto l = lent.find(c);
                    if (l != lent.end() && c != fn.layout.entry && l->second.known && !((l->second.effects.preserved >> k.bit) & 1u)) changed = true;
                }
                if (!changed) continue;
                count("lent.changed.checked");
                if (WholeWord(SavesBeforeCallsDropped(text, k.name), std::string("unaff_") + k.name) || WholeWord(text, std::string("in_") + k.name))
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
                  << dts::FateLabel(f.fate);
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
                if (std::strcmp(std::getenv("SE_GENERATED_TOKENS"), "2") == 0)   // =2: each token with its address, to see which text an instruction owns
                {
                    o << "\n      tokens:";
                    for (const Token& t : l.tokens) o << " [" << Hex(t.addr, 4) << " '" << l.text.substr(t.begin, t.end - t.begin) << "']";
                }
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
    std::function<bool(const Program&)> accept;   // a test's stand-in for "the finding is still there" (empty: evaluate the program)

    // What the generator declared of a function must still be true of the program a shrink step leaves (A2-50). The one declaration an
    // item can carry is `return g(p);` (Func::retIsCall): r0 is what the last call's callee returned, and no instruction of the function
    // writes it. Take that call out, or put another item after it, and the function writes r0 nowhere and `returns` is false, so a finding
    // that reads `returns` (`return-dropped`) would be a false one in the shrunk replay. The other declarations are words of the function
    // itself (retWord, which no step removes), or are checked against the run (`partial`).
    static bool IntentHolds(const Program& p)
    {
        for (const Func& f : p.fns)
        {
            if (!f.v2 || !f.returns || f.end != Func::Rts || !f.retIsCall) continue;
            if (f.items.empty()) return false;
            const Item& last = f.items.back();
            if (last.type != Item::Call || last.callee < 0 || last.callee >= static_cast<int>(p.fns.size()) || !p.fns[last.callee].returns) return false;
        }
        return true;
    }

    bool Still()
    {
        if (evals >= budget) return false;
        if (!IntentHolds(prog)) return false;
        ++evals;
        if (accept) return accept(prog);
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
                const bool jsr = cur.viaJsr, bsrf = cur.viaBsrf;
                cur.viaJsr = cur.viaBsrf = false;
                if (Still()) changed = true;
                else
                {
                    cur.viaJsr = jsr;
                    cur.viaBsrf = bsrf;
                }
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
            if ((l.kind == LitKind::Fn || l.kind == LitKind::FnRel) && l.fn > k) --l.fn;
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
                    if (Try(f.retIsCall, false)) changed = true;
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
        case Item::Call: m[it.viaBsrf ? "bsrf" : it.viaJsr ? "jsr" : "bsr"] += 1; break;
        case Item::Table: m["mova"] += 1; m["shll2"] += 1; m["jmp"] += 1; m["cmp/hi"] += 1; break;
        case Item::BraLoop: case Item::BraBack: m["bra"] += 1; break;
        case Item::DataSkip: m["bra"] += 1; break;
        case Item::Choice: m[it.onTrue ? "bt" : "bf"] += 1; m["bra"] += 1; break;
        case Item::Mask: m["mov.l"] += 1; m[it.xorOp ? "xor" : it.onTrue ? "and" : "or"] += 1; break;
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
        if (f.retWord && !f.retIsCall) CountWords({ f.retWord }, m);
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
        if (f.retIsCall) ++shapes["fn.ret_call"];
        if (f.choiceBy >= 0) ++shapes["fn.ret_choice"];
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
                s.fns[i].returns = flags[i] == 1 || flags[i] >= 0x30;
                s.fns[i].partial = flags[i] == 2;
                if (flags[i] >= 0x30) s.fns[i].choiceBy = static_cast<int>(flags[i] - 0x30);
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
        s.polls.push_back({ Env::MemKey(at), untilSet ? 0u : 1u, untilSet ? 1u : 0u, 0, at });
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
//   SE_GENERATED_PRINTEDC_INPUTS=n inputs per function (default 32: the first 26 walk the edge values through r4-r7)
//   SE_GENERATED_PRINTEDC_PER=n    functions per seed (default 1)
//   SE_GENERATED_PRINTEDC_WORDS="6153 E000 000B 0009;..."  check hand-written functions, text printed
//   SE_C_COMPILER=<cc>             the compiler (default: cc, gcc, clang found on PATH)
//   SE_CXX_COMPILER=<c++>          the C++ compiler of the engine reading of a text that disagrees as C (default: the one that goes with the C compiler)
// Without a C compiler the run says so loudly and exits 77, which ctest reports as skipped.
// A text that disagrees with the run as C is read once more as the engine means it (CxxHeader, C++ classes that do the
// arithmetic at their own width: `(int4)-param_1` with `char param_1` negates a byte, a comparison is a four-byte unsigned
// in the arithmetic around it, xunknown has no sign) and counted apart ('narrow-c') only if it agrees there: the difference is
// then in the reading, not the value (15_DECOMPILER_STEP5.md, "Area 2, round 3", question 1, and "Area 2, round 5", A2-33).
// No declaration or cast in the text excuses a mismatch by its looks. The generated functions are register-only: a text that
// calls something or reads a stack argument is the grammar leaking an instruction it cannot run ('grammar-leak', A2-38).

std::string gScratch;

struct RegItem
{
    std::vector<uint16_t> w;
    std::string shape;
    // PC-relative loads (mov.l @(disp,PC),Rn) in this item: the word index in 'w' and the 32-bit constant, which the
    // function's literal pool (after the rts and its slot) holds. The displacement is filled in by RegFn::Words.
    std::vector<std::pair<size_t, uint32_t>> lits;
};

struct RegFn
{
    std::vector<RegItem> items;
    uint16_t slot = 0x0009;   // rts's delay slot
    std::vector<uint16_t> Words() const
    {
        std::vector<uint16_t> out;
        std::vector<std::pair<size_t, uint32_t>> lits;
        for (const RegItem& i : items)
        {
            for (const auto& l : i.lits) lits.push_back(std::make_pair(out.size() + l.first, l.second));
            out.insert(out.end(), i.w.begin(), i.w.end());
        }
        out.push_back(0x000B);
        out.push_back(slot);
        if (lits.empty()) return out;
        // The pool follows the rts and its slot, longword aligned (kCode is). mov.l @(disp,PC),Rn reads the longword at
        // ((address of the load + 4) & ~3) + 4 * disp.
        if (out.size() % 2) out.push_back(0x0009);
        const size_t pool = out.size();
        for (size_t k = 0; k < lits.size(); ++k)
        {
            const size_t at = lits[k].first;
            const size_t pc = (2 * at + 4) & ~static_cast<size_t>(3);
            out[at] = static_cast<uint16_t>((out[at] & 0xFF00) | ((2 * (pool + 2 * k) - pc) / 4));
        }
        for (const auto& l : lits)
        {
            out.push_back(static_cast<uint16_t>(l.second >> 16));
            out.push_back(static_cast<uint16_t>(l.second & 0xFFFF));
        }
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

// ---- The literal-pool and stack items of the register-only grammar (area 2, round 4: A2-21, A2-22, A2-26) ----------
// The grammar had no constant wider than mov #imm and no memory, so it could reach neither the compiler's division by a
// constant (a 32-bit magic multiplier from the literal pool, dmulu.l, the shifts) nor a byte put into a stacked longword.
// Both are drawn from a stream of their own (the other draws of a seed are unchanged).

// Hacker's Delight, magicu and magic: the multiplier and shift of x / d for a 32-bit x (d >= 2 unsigned; |d| >= 2 signed).
struct MagicU { uint32_t m; int s; bool add; };
MagicU MakeMagicU(uint32_t d)
{
    MagicU r = { 0, 0, false };
    const uint32_t nc = 0xFFFFFFFFu - (0u - d) % d;
    int p = 31;
    uint32_t q1 = 0x80000000u / nc, r1 = 0x80000000u - q1 * nc, q2 = 0x7FFFFFFFu / d, r2 = 0x7FFFFFFFu - q2 * d, delta;
    do
    {
        ++p;
        if (r1 >= nc - r1) { q1 = 2 * q1 + 1; r1 = 2 * r1 - nc; }
        else { q1 = 2 * q1; r1 = 2 * r1; }
        if (r2 + 1 >= d - r2)
        {
            if (q2 >= 0x7FFFFFFFu) r.add = true;
            q2 = 2 * q2 + 1;
            r2 = 2 * r2 + 1 - d;
        }
        else
        {
            if (q2 >= 0x80000000u) r.add = true;
            q2 = 2 * q2;
            r2 = 2 * r2 + 1;
        }
        delta = d - 1 - r2;
    } while (p < 64 && (q1 < delta || (q1 == delta && r1 == 0)));
    r.m = q2 + 1;
    r.s = p - 32;
    return r;
}

struct MagicS { int32_t m; int s; };
MagicS MakeMagicS(int32_t d)
{
    const uint32_t two31 = 0x80000000u;
    const uint32_t ad = d < 0 ? 0u - static_cast<uint32_t>(d) : static_cast<uint32_t>(d);
    const uint32_t t = two31 + (static_cast<uint32_t>(d) >> 31);
    const uint32_t anc = t - 1 - t % ad;
    int p = 31;
    uint32_t q1 = two31 / anc, r1 = two31 - q1 * anc, q2 = two31 / ad, r2 = two31 - q2 * ad, delta;
    do
    {
        ++p;
        q1 = 2 * q1; r1 = 2 * r1;
        if (r1 >= anc) { ++q1; r1 -= anc; }
        q2 = 2 * q2; r2 = 2 * r2;
        if (r2 >= ad) { ++q2; r2 -= ad; }
        delta = ad - r2;
    } while (q1 < delta || (q1 == delta && r1 == 0));
    MagicS r;
    r.m = static_cast<int32_t>(q2 + 1);
    if (d < 0) r.m = -r.m;
    r.s = p - 32;
    return r;
}

// Shift register 'n' right (logical or arithmetic) or left by 'count' (0..31) with the SH-2's shlr / shlr2 / shlr8 / shlr16.
void ShiftWords(std::vector<uint16_t>& w, int n, int count, bool left, bool arithmetic)
{
    const uint16_t base1 = left ? 0x4000 : arithmetic ? 0x4021 : 0x4001;   // shll / shar / shlr
    const uint16_t base2 = left ? 0x4008 : 0x4009, base8 = left ? 0x4018 : 0x4019, base16 = left ? 0x4028 : 0x4029;
    if (arithmetic)   // no shar2 / shar8 on the SH-2: one at a time
    {
        for (int i = 0; i < count; ++i) w.push_back(static_cast<uint16_t>(base1 | n << 8));
        return;
    }
    while (count >= 16) { w.push_back(static_cast<uint16_t>(base16 | n << 8)); count -= 16; }
    while (count >= 8) { w.push_back(static_cast<uint16_t>(base8 | n << 8)); count -= 8; }
    while (count >= 2) { w.push_back(static_cast<uint16_t>(base2 | n << 8)); count -= 2; }
    while (count >= 1) { w.push_back(static_cast<uint16_t>(base1 | n << 8)); count -= 1; }
}

// x / d scaled by a power of two, as a compiler emits it: mov.l @(lit),L ; dmulu.l L,X ; sts mach,T ; the shift (or the
// "add" form: sub T,X ; shlr X ; add T,X ; shlr (s-1)) ; the scale. The scale is often the power of two the last shift
// divided by (an index into a table of 4- or 16-byte entries), which merges the pair into a mask. 'scale' -1 is that one.
RegItem DivisionItem(uint32_t d, bool isSigned, int scale, uint32_t x, uint32_t l, uint32_t t, uint32_t u, bool toR0)
{
    RegItem it;
    int32_t sd = static_cast<int32_t>(d);
    int result = -1, lastShift = 0;
    it.w.push_back(static_cast<uint16_t>(0xD000 | l << 8));   // mov.l @(lit),L
    if (!isSigned)
    {
        const MagicU mg = MakeMagicU(d);
        it.lits.push_back(std::make_pair(size_t(0), mg.m));
        it.w.push_back(static_cast<uint16_t>(0x3005 | x << 8 | l << 4));    // dmulu.l L,X
        it.w.push_back(static_cast<uint16_t>(0x000A | t << 8));             // sts mach,T
        if (!mg.add)
        {
            ShiftWords(it.w, static_cast<int>(t), mg.s, false, false);
            result = static_cast<int>(t);
            lastShift = mg.s;
        }
        else
        {
            it.w.push_back(static_cast<uint16_t>(0x3008 | x << 8 | t << 4));   // sub T,X
            it.w.push_back(static_cast<uint16_t>(0x4001 | x << 8));            // shlr X
            it.w.push_back(static_cast<uint16_t>(0x300C | x << 8 | t << 4));   // add T,X
            ShiftWords(it.w, static_cast<int>(x), mg.s - 1, false, false);
            result = static_cast<int>(x);
            lastShift = mg.s - 1;
        }
        it.shape = mg.add ? "u/c.add" : "u/c";
    }
    else
    {
        const MagicS mg = MakeMagicS(sd);
        it.lits.push_back(std::make_pair(size_t(0), static_cast<uint32_t>(mg.m)));
        it.w.push_back(static_cast<uint16_t>(0x300D | x << 8 | l << 4));    // dmuls.l L,X
        it.w.push_back(static_cast<uint16_t>(0x000A | t << 8));             // sts mach,T
        if (sd > 0 && mg.m < 0) it.w.push_back(static_cast<uint16_t>(0x300C | t << 8 | x << 4));   // add X,T
        if (sd < 0 && mg.m > 0) it.w.push_back(static_cast<uint16_t>(0x3008 | t << 8 | x << 4));   // sub X,T
        ShiftWords(it.w, static_cast<int>(t), mg.s, false, true);
        it.w.push_back(static_cast<uint16_t>(0x6003 | u << 8 | t << 4));    // mov T,U
        it.w.push_back(static_cast<uint16_t>(0x4000 | u << 8));             // shll U   (T = the sign)
        it.w.push_back(static_cast<uint16_t>(0xE000 | u << 8));             // mov #0,U
        it.w.push_back(static_cast<uint16_t>(0x300E | t << 8 | u << 4));    // addc U,T
        result = static_cast<int>(t);
        lastShift = mg.s;
        it.shape = "s/c";
    }
    if (scale < 0) scale = lastShift;
    if (scale > 12) scale = 8;
    if (scale > 0)
    {
        ShiftWords(it.w, result, scale, true, false);
        it.shape += "*2^k";
    }
    if (toR0) it.w.push_back(static_cast<uint16_t>(0x6003 | 0 << 8 | static_cast<uint32_t>(result) << 4));   // mov R,r0
    return it;
}

RegItem MakeDivision(Rng& rng)
{
    static const uint32_t kDivisors[] = { 3, 5, 6, 7, 9, 10, 11, 12, 13, 14, 15, 17, 19, 21, 22, 25, 27, 28, 31, 33, 37, 41, 55, 63, 100, 127, 641,
                                          1000, 6700417, 0x7FFFFFFF, 0x12345, 0xFFFFFFFE };
    const bool isSigned = rng.Chance(35);
    uint32_t d = rng.Chance(75) ? kDivisors[rng.Below(sizeof kDivisors / sizeof kDivisors[0])] : 2 + rng.Below(200);
    if ((d & (d - 1)) == 0) d += 1;   // a power of two is a shift, not a multiply
    if (isSigned)
    {
        if (d >= 0x80000000u) d >>= 1;
        if (rng.Chance(20)) d = 0u - d;
    }
    const uint32_t x = rng.Below(8), l = (x + 1 + rng.Below(7)) % 8;
    uint32_t t = rng.Below(8);
    while (t == x || t == l) t = (t + 1) % 8;
    uint32_t u = rng.Below(8);
    while (u == x || u == l || u == t) u = (u + 1) % 8;
    // The scale: the power of two the last shift divided by (a mask after the merge), or another, or none.
    const uint32_t pick = rng.Below(10);
    const int scale = pick < 5 ? -1 : pick < 8 ? static_cast<int>(rng.Below(5)) : 0;
    return DivisionItem(d, isSigned, scale, x, l, t, u, rng.Chance(70));
}

// A stack item: allocate a frame, store registers into its longwords, overwrite a byte or a word of one with r0 or
// another register, load back a longword, a word or a byte, release the frame. Every longword is written whole before it
// is overwritten in part or read, so the run reads nothing it did not store.
RegItem MakeStackItem(Rng& rng)
{
    RegItem it;
    const uint32_t longs = 1 + rng.Below(2);   // a frame of one or two longwords
    it.shape = "stack";
    it.w.push_back(static_cast<uint16_t>(0x7F00 | ((0x100 - 4 * longs) & 0xFF)));   // add #-4n,r15
    // each longword: store a register whole
    for (uint32_t k = 0; k < longs; ++k)
    {
        const uint32_t m = rng.Below(8);
        if (k == 0 && rng.Chance(50)) it.w.push_back(static_cast<uint16_t>(0x2002 | 15 << 8 | m << 4));          // mov.l Rm,@r15
        else it.w.push_back(static_cast<uint16_t>(0x1000 | 15 << 8 | m << 4 | k));                              // mov.l Rm,@(k*4,r15)
    }
    // one to three overwrites
    const uint32_t overwrites = 1 + rng.Below(3);
    for (uint32_t i = 0; i < overwrites; ++i)
    {
        const uint32_t kind = rng.Below(100);
        const uint32_t m = rng.Below(8);
        if (kind < 45)   // mov.b r0,@(d,r15): through r0 (mov Rm,r0 first)
        {
            it.w.push_back(static_cast<uint16_t>(0x6003 | 0 << 8 | m << 4));
            it.w.push_back(static_cast<uint16_t>(0x8000 | 15 << 4 | rng.Below(4 * longs)));
        }
        else if (kind < 80)   // mov.w r0,@(d,r15)
        {
            it.w.push_back(static_cast<uint16_t>(0x6003 | 0 << 8 | m << 4));
            it.w.push_back(static_cast<uint16_t>(0x8100 | 15 << 4 | rng.Below(2 * longs)));
        }
        else if (kind < 90 && longs == 1)   // mov.b Rm,@r15 (byte 0, the most significant)
            it.w.push_back(static_cast<uint16_t>(0x2000 | 15 << 8 | m << 4));
        else if (kind < 100 && longs == 1)   // mov.w Rm,@r15
            it.w.push_back(static_cast<uint16_t>(0x2001 | 15 << 8 | m << 4));
        else   // a whole longword again
            it.w.push_back(static_cast<uint16_t>(0x1000 | 15 << 8 | m << 4 | rng.Below(longs)));
    }
    // load back: the longword, or a part of it
    const uint32_t d = rng.Below(8);
    const uint32_t how = rng.Below(100);
    if (how < 60) it.w.push_back(static_cast<uint16_t>(0x5000 | d << 8 | 15 << 4 | rng.Below(longs)));   // mov.l @(k*4,r15),Rd
    else if (how < 70 && longs == 1) it.w.push_back(static_cast<uint16_t>(0x6002 | d << 8 | 15 << 4));   // mov.l @r15,Rd
    else if (how < 85) it.w.push_back(static_cast<uint16_t>(0x8500 | 15 << 4 | rng.Below(2 * longs)));    // mov.w @(d,r15),r0
    else it.w.push_back(static_cast<uint16_t>(0x8400 | 15 << 4 | rng.Below(4 * longs)));                 // mov.b @(d,r15),r0
    if (rng.Chance(30))   // something to the loaded register before the frame goes
    {
        std::string shape;
        it.w.push_back(RegOpWord(rng, shape));
    }
    it.w.push_back(static_cast<uint16_t>(0x7F00 | 4 * longs));   // add #4n,r15
    return it;
}

// A literal on its own: mov.l @(lit),Rn, the constant an edge value, a magic multiplier or random.
RegItem MakeConstant(Rng& rng)
{
    static const uint32_t kEdges[] = { 0, 1, 0x7FFFFFFF, 0x80000000u, 0xFFFFFFFFu, 0x24924925u, 0xAAAAAAABu, 0x55555556u, 0x0000FFFFu, 0xFFFF0000u,
                                       0x80000001u, 0x12345678u, 0xFFFFFF00u, 0x000000FFu, 0x01010101u };
    RegItem it;
    const uint32_t n = rng.Below(8);
    const uint32_t value = rng.Chance(60) ? kEdges[rng.Below(sizeof kEdges / sizeof kEdges[0])] : rng.Next();
    it.w.push_back(static_cast<uint16_t>(0xD000 | n << 8));
    it.lits.push_back(std::make_pair(size_t(0), value));
    it.shape = "lit";
    return it;
}

// A sum of scaled terms over shared variables, read through a mask or an extension (area 2, round 8: A2-52): `c + 8 * x + 4 * y + 14 * x`
// then `shlr2 ; shll2`. Each term is `mov #c,r3` (or a pool constant) ; `mul.l Rv,r3` ; `sts macl,r3` ; `add r3,r0` (or `sub`), the variables
// are two of r4-r7 so that one is likely to come twice, the constants are multiples of 2 and 4 and the like of an address (not one the engine would name a function or a global after: that is a text the harness cannot run), and what reads the sum
// is a mask of its low bits, a shift pair or an extension: the shape in which RuleCollectTerms combines two terms of one variable by changing
// the inputs of two ADDs of the tree, which leaves the partial sums between them with the non-zero mask they had (A2-47, A2-52). The
// grammar drew no sum of one variable scaled twice, so the printed-C differential could not see the class (8,072 functions with patch 0046
// reverted, 0 findings).
RegItem MakeTerms(Rng& rng)
{
    // mostly even coefficients, which keep the low bits of a sum clear (the mask the rewrite then has to forget), a few odd ones and a few negative
    static const uint32_t kEven[] = { 2, 4, 6, 8, 0xC, 0xE, 0x10, 0x12, 0x14, 0x16, 0x1C, 0x24, 0x48, 0xB8, 0xFFFFFFFCu, 0xFFFFFFFEu, 0xFFFFFFFAu, 0xFFFFFFF8u,
                                      0x30100008u, 0x3010000Eu, 0x3010000Au };
    static const uint32_t kOther[] = { 1, 3, 5, 0xFFFFFFFFu };
    static const uint32_t kStarts[] = { 0, 0, 0x30100008u, 0x30100000u, 4, 8, 0x10, 0x100, 0xFFFFFFFCu };
    static const uint32_t kMasks[] = { 0xFFFFFFFCu, 0xFFFFFFF8u, 0xFFFFFFF0u, 0xFFFFFFFEu };
    RegItem it;
    it.shape = "terms";
    auto constant = [&](uint32_t reg, uint32_t value) {
        const int32_t sv = static_cast<int32_t>(value);
        if (sv >= -128 && sv <= 127 && rng.Chance(50)) it.w.push_back(static_cast<uint16_t>(0xE000 | reg << 8 | (value & 0xFF)));   // mov #imm,Rn
        else
        {
            it.lits.push_back(std::make_pair(it.w.size(), value));
            it.w.push_back(static_cast<uint16_t>(0xD000 | reg << 8));                                                                 // mov.l @(d,pc),Rn
        }
    };
    const uint32_t vars[2] = { 4 + rng.Below(4), 4 + rng.Below(4) };
    constant(0, kStarts[rng.Below(sizeof kStarts / sizeof kStarts[0])]);
    const uint32_t terms = 3 + rng.Below(2);
    for (uint32_t t = 0; t < terms; ++t)
    {
        constant(3, rng.Chance(85) ? kEven[rng.Below(sizeof kEven / sizeof kEven[0])] : kOther[rng.Below(sizeof kOther / sizeof kOther[0])]);
        it.w.push_back(static_cast<uint16_t>(0x0307 | vars[rng.Below(2)] << 4));   // mul.l Rv,r3
        it.w.push_back(0x031A);                                                  // sts macl,r3
        it.w.push_back(rng.Chance(75) ? 0x303C : 0x3038);                         // add r3,r0 / sub r3,r0
    }
    const uint32_t tail = rng.Below(100);
    if (tail < 30) { it.w.push_back(0x4009); it.w.push_back(0x4008); }                                  // shlr2 ; shll2
    else if (tail < 45) it.w.push_back(0xC9FC);                                                         // and #0xfc,r0
    else if (tail < 70) { constant(3, kMasks[rng.Below(4)]); it.w.push_back(0x2039); }                  // and r3,r0 (a mask of the low bits)
    else if (tail < 75) { it.w.push_back(0x4009); it.w.push_back(0x4009); it.w.push_back(0x4008); it.w.push_back(0x4008); }
    else if (tail < 80) { it.w.push_back(0xC903); it.w.push_back(0x4000); }                             // and #3,r0 ; shll r0
    else if (tail < 85) { it.w.push_back(0x4019); it.w.push_back(0x4018); }                             // shlr8 ; shll8
    else if (tail < 92) it.w.push_back(0x600D);                                                         // extu.w r0,r0
    else it.w.push_back(0x600C);                                                                        // extu.b r0,r0
    return it;
}

// A join whose two predecessors compute the same thing from a source register, read at two widths (area 2, round 9: A2-58): each arm makes
// `r1 = src & M` (M a mask of high bits) and `r2 = G(r1)` (shifts, extensions, adds, a test), and the join reads r1 through a narrow H (a byte,
// a shift pair) and r2 whole. RulePushMulti merges the two computations of r2 into one op in the join that reads the join's phi of r1, whose
// consume mask was worked out from H alone; with that mask left as it was, RuleAndMask took `src & M` for unused and the text returned a
// constant. The register grammar drew no join of equal computations (the nightly's three ranges, 45,000 functions, had none); the families of
// tools/a2_round9/dagen.py (7 to 9) are its model.
RegItem MakeJoin(Rng& rng)
{
    static const std::vector<uint16_t> kMasks[] = { { 0xE301, 0x4305 }, { 0xE301, 0x4305, 0x4309 }, { 0xE3FF, 0x4328 }, { 0xE3FF, 0x4328, 0x4318 },
                                                    { 0xE3FF, 0x4318 }, { 0xE3FF, 0x4308, 0x4318 }, { 0xE30F, 0x4318 }, { 0xE3F0, 0x4318, 0x4308 },
                                                    { 0xE301, 0x4305, 0x4309, 0x4309 } };
    static const uint16_t kTests[] = { 0x4415, 0x4411, 0x2448, 0x3447, 0x3443 };   // cmp/pl r4, cmp/pz r4, tst r4,r4, cmp/gt r4,r4, cmp/ge r4,r4
    static const uint16_t kG[] = { 0x4219, 0x4209, 0x4201, 0x4200, 0x4208, 0x4218, 0x4228, 0x4229, 0x7201, 0x7204, 0x7280, 0x6228, 0x6229,
                                   0x622C, 0x622D, 0x622E, 0x622F, 0x6227, 0x622B, 0x4204, 0x4205 };   // on r2: shifts, add #k, swaps, extensions, not, neg, rotates
    static const uint16_t kArmTests[] = { 0x2118, 0x2118, 0x4115, 0x4111 };   // tst r1,r1, cmp/pl r1, cmp/pz r1 (the join reads T)
    static const std::vector<uint16_t> kH[] = { { 0x600C }, { 0x600D }, { 0x4019, 0x600C }, { 0x4009, 0x600C }, { 0x4029, 0x600D }, { 0x4018, 0x4019 },
                                                { 0x4000 }, { 0x4008 }, { 0x6008 }, { 0xC97F }, { 0xC9F0 }, { 0x4021 }, { 0x4001, 0x600C }, { 0x600F }, { 0x600E } };
    static const uint16_t kCombine[] = { 0x302C, 0x3028, 0x202B, 0x202A, 0x2029 };   // add, sub, or, xor, and r2,r0
    RegItem it;
    it.shape = "join";
    const std::vector<uint16_t>& mask = kMasks[rng.Below(sizeof kMasks / sizeof kMasks[0])];
    it.w.insert(it.w.end(), mask.begin(), mask.end());
    it.w.push_back(kTests[rng.Below(sizeof kTests / sizeof kTests[0])]);
    const bool viaT = rng.Chance(30);
    std::vector<uint16_t> g;
    if (viaT)
    {
        if (rng.Chance(40)) g.push_back(static_cast<uint16_t>(0x7100 | rng.Below(3)));   // add #k,r1 (on r1, before the test)
        g.push_back(kArmTests[rng.Below(sizeof kArmTests / sizeof kArmTests[0])]);
    }
    else
    {
        g.push_back(0x6213);   // mov r1,r2
        const uint32_t n = 1 + rng.Below(3);
        for (uint32_t i = 0; i < n; ++i) g.push_back(kG[rng.Below(sizeof kG / sizeof kG[0])]);
    }
    auto arm = [&](uint32_t src) {
        std::vector<uint16_t> a = { static_cast<uint16_t>(0x6103 | src << 4), 0x2139 };   // mov src,r1 ; and r3,r1
        a.insert(a.end(), g.begin(), g.end());
        return a;
    };
    const std::vector<uint16_t> a = arm(5), b = arm(6);
    it.w.push_back(static_cast<uint16_t>((rng.Chance(50) ? 0x8900 : 0x8B00) | (a.size() + 1)));   // bt/bf B
    it.w.insert(it.w.end(), a.begin(), a.end());
    it.w.push_back(static_cast<uint16_t>(0xA000 | b.size()));   // bra J
    it.w.push_back(0x0009);
    it.w.insert(it.w.end(), b.begin(), b.end());
    it.w.push_back(0x6013);   // J: mov r1,r0
    const std::vector<uint16_t>& h = kH[rng.Below(sizeof kH / sizeof kH[0])];
    it.w.insert(it.w.end(), h.begin(), h.end());
    if (viaT) it.w.push_back(0x0229);   // movt r2
    it.w.push_back(kCombine[rng.Below(sizeof kCombine / sizeof kCombine[0])]);
    return it;
}

// A counted loop that carries `r1 &= f(src)` (or |=, ^=), r1 seeded with a small constant and f a value assembled from the pieces of a
// register (swap.b, swap.w, xtrct, a shift and a swap) (area 2, round 9: A2-59). RuleAndDistribute distributes the AND over the OR of the
// pieces when the non-zero mask of the carried value (the loop's phi) misses a piece; with that mask forgotten after the first distribution
// (patch 0049) the second did not fire and RuleHumptyOr merged the first back, for ever. The register grammar drew no loop at all.
RegItem MakeLoopAnd(Rng& rng)
{
    static const uint16_t kSeeds[] = { 1, 2, 4, 0x0F, 0x1C, 0x7F, 0xFC, 0x03, 0x10 };
    static const uint32_t kSources[] = { 2, 4, 5, 7 };
    RegItem it;
    it.shape = "loopand";
    it.w.push_back(static_cast<uint16_t>(0xE100 | kSeeds[rng.Below(sizeof kSeeds / sizeof kSeeds[0])]));   // mov #c,r1
    it.w.push_back(static_cast<uint16_t>(0xE600 | (1 + rng.Below(3))));                                    // mov #n,r6
    const size_t top = it.w.size();
    const uint32_t src = kSources[rng.Below(4)];
    it.w.push_back(static_cast<uint16_t>(0x6303 | src << 4));   // mov src,r3
    switch (rng.Below(6))
    {
    case 0: it.w.push_back(0x6338); break;                                                  // swap.b r3,r3
    case 1: it.w.push_back(0x6339); break;                                                  // swap.w r3,r3
    case 2: it.w.push_back(0x6338); it.w.push_back(0x6339); break;
    case 3: it.w.push_back(static_cast<uint16_t>(0x230D | kSources[rng.Below(4)] << 4)); break;   // xtrct rm,r3
    case 4: it.w.push_back(0x4319); it.w.push_back(0x6338); break;                         // shlr8 r3 ; swap.b r3,r3
    default: it.w.push_back(0x6338); it.w.push_back(static_cast<uint16_t>(0x230B | src << 4)); break;   // swap.b ; or src,r3
    }
    static const uint16_t kAcc[] = { 0x2139, 0x2139, 0x213B, 0x213A };   // and, and, or, xor r3,r1
    it.w.push_back(kAcc[rng.Below(4)]);
    it.w.push_back(0x4610);   // dt r6
    const int disp = static_cast<int>(top) - static_cast<int>(it.w.size() + 2);
    it.w.push_back(static_cast<uint16_t>(0x8B00 | (disp & 0xFF)));   // bf top
    it.w.push_back(0x6013);   // mov r1,r0
    return it;
}

// A flag test against a literal-pool constant (area 2, round 10: A2-64): `mov.l @(lit),A ; OP B,A` with OP one of addv, subv, addc, subc,
// cmp/eq, cmp/hs, cmp/ge, cmp/hi, cmp/gt (the literal an edge of the signed and unsigned ranges: 0xAAAAAAAB, 0x80000001, 0xF0000000 ...), then
// T tested: a short branch over `mov #k,Rt` (bt or bf) that the function returns, or `movt r0`. addv and subv with a literal fold to
// `(b != 0xff) && !b || ...` with `b = r0 s< 0`, whose parts RuleRangeMeld pulls back through the comparison; CircleRange::pullBackBinary answered a
// boolean range holding both 0 and 1 with [0,2) in the wide space instead of the full range, so the test printed `1 < param_1 && ...` where
// the right one is `0x7fffffff < param_1 && ...`, and the register grammar drew no flag producer against a literal that a branch tests
// (the nightly's three ranges, 45,000 functions, none). The item is drawn from a stream of its own (SE_GENERATED_PRINTEDC_FLAGS=0 gives the
// functions of round 9).
RegItem MakeFlagLit(Rng& rng)
{
    static const uint32_t kLits[] = { 0xAAAAAAABu, 0x55555555u, 0x55555556u, 0x80000001u, 0xF0000000u, 0x7FFFFFFFu, 0x80000000u, 0xFFFFFFFFu, 1u, 0x12345678u,
                                      0xFFFFFFFDu, 0xC0000000u, 0x40000000u, 0xFFFF0000u, 0x0000FFFFu, 0x7FFFFFFEu, 0x24924925u };
    static const uint16_t kOps[] = { 0x300F, 0x300F, 0x300B, 0x300B, 0x300E, 0x300A, 0x3000, 0x3002, 0x3003, 0x3006, 0x3007 };   // addv addv subv subv addc subc cmp/eq hs ge hi gt
    RegItem it;
    it.shape = "flaglit";
    const uint32_t a = 1 + rng.Below(7);                 // holds the literal (r1-r7)
    uint32_t b = rng.Below(8);                           // the other operand, an input or a copy of one
    if (b == a) b = (b + 1) % 8;
    const uint32_t value = rng.Chance(85) ? kLits[rng.Below(sizeof kLits / sizeof kLits[0])] : rng.Next();
    if (b == 0 && rng.Chance(50)) it.w.push_back(static_cast<uint16_t>(0x6003 | 0 << 8 | (4 + rng.Below(4)) << 4));   // mov r4-r7,r0
    it.lits.push_back(std::make_pair(it.w.size(), value));
    it.w.push_back(static_cast<uint16_t>(0xD000 | a << 8));   // mov.l @(lit),Ra
    const uint16_t op = kOps[rng.Below(sizeof kOps / sizeof kOps[0])];
    if (rng.Chance(70)) it.w.push_back(static_cast<uint16_t>(op | a << 8 | b << 4));   // OP Rb,Ra
    else it.w.push_back(static_cast<uint16_t>(op | b << 8 | a << 4));                   // OP Ra,Rb
    if (rng.Chance(65))   // mov #k,Rt ; bf/bt +0 ; mov #j,Rt ; mov Rt,r0
    {
        uint32_t t = 1 + rng.Below(7);
        while (t == a || t == b) t = 1 + t % 7;
        static const uint32_t kK[] = { 5, 7, 0, 1, 0xFF, 0x80 };
        it.w.push_back(static_cast<uint16_t>(0xE000 | t << 8 | kK[rng.Below(6)]));
        it.w.push_back(static_cast<uint16_t>(rng.Chance(50) ? 0x8B00 : 0x8900));   // bf / bt  +0 (skips the next instruction)
        it.w.push_back(static_cast<uint16_t>(0xE000 | t << 8 | kK[rng.Below(6)]));
        it.w.push_back(static_cast<uint16_t>(0x6003 | t << 4));                  // mov Rt,r0
    }
    else
        it.w.push_back(0x0029);   // movt r0
    return it;
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
        else if (r < 16)   // bra over one to three instructions: mov #2n,Rx ; bra +n ; slot ; n skipped
        {
            // This was braf Rx (round 3 to 5), which the engine reads as a tail call to its one target (documented: a jump table is what
            // braf is for), so the text calls the code after it and the oracle could not run it: all 268 `skipped:call-or-stack` of 3,060
            // functions were that item (A2-38). The draws are the same, and so is the number of words; the mov stays (it sets Rx as before).
            const uint32_t x = rng.Below(8), skipped = 1 + rng.Below(3);
            it.w.push_back(static_cast<uint16_t>(0xE000 | x << 8 | 2 * skipped));
            it.w.push_back(static_cast<uint16_t>(0xA000 | skipped));
            it.w.push_back(RegOpWord(rng, shape));
            for (uint32_t i = 0; i < skipped; ++i) it.w.push_back(RegOpWord(rng, shape));
            it.shape = "bra";
        }
        else
        {
            it.w.push_back(RegOpWord(rng, shape));
            it.shape = shape;
        }
        words += static_cast<int>(it.w.size());
        f.items.push_back(it);
    }
    // The literal-pool and stack items (A2-21, A2-22), from a stream of their own: the draws above are those of earlier rounds.
    Rng wide(seed * 104729u + static_cast<uint64_t>(k) + 0xA2F4u);
    if (EnvNumber("SE_GENERATED_PRINTEDC_WIDE", 1) != 0 && wide.Chance(35))
    {
        const int count = wide.Chance(25) ? 2 : 1;
        for (int c = 0; c < count; ++c)
        {
            const uint32_t kind = wide.Below(100);
            const RegItem item = kind < 40 ? MakeDivision(wide) : kind < 75 ? MakeStackItem(wide) : MakeConstant(wide);
            // At the end, where the rts returns r0, or anywhere.
            const size_t at = wide.Chance(50) ? f.items.size() : wide.Below(static_cast<uint32_t>(f.items.size()) + 1);
            f.items.insert(f.items.begin() + static_cast<long>(at), item);
        }
    }
    // The sums of scaled terms (A2-52), from a stream of their own again: a seed's other items are those of the rounds before.
    Rng sums(seed * 15485863u + static_cast<uint64_t>(k) + 0xA252u);
    if (EnvNumber("SE_GENERATED_PRINTEDC_TERMS", 1) != 0 && sums.Chance(30))
    {
        const RegItem item = MakeTerms(sums);
        const size_t at = sums.Chance(70) ? f.items.size() : sums.Below(static_cast<uint32_t>(f.items.size()) + 1);
        f.items.insert(f.items.begin() + static_cast<long>(at), item);
    }
    // The joins of equal computations and the loops that AND an accumulator with the pieces of a register (A2-58, A2-59), from a stream
    // of their own once more: SE_GENERATED_PRINTEDC_JOINS=0 gives the functions of round 8.
    Rng joins(seed * 32452843u + static_cast<uint64_t>(k) + 0xA258u);
    if (EnvNumber("SE_GENERATED_PRINTEDC_JOINS", 1) != 0)
    {
        const uint32_t pick = joins.Below(100);
        if (pick < 25)
        {
            const RegItem item = pick < 15 ? MakeJoin(joins) : MakeLoopAnd(joins);
            const size_t at = joins.Chance(70) ? f.items.size() : joins.Below(static_cast<uint32_t>(f.items.size()) + 1);
            f.items.insert(f.items.begin() + static_cast<long>(at), item);
        }
    }
    // The flag tests against a literal (A2-64), from a stream of their own: SE_GENERATED_PRINTEDC_FLAGS=0 gives the functions of round 9.
    Rng flags(seed * 49979687u + static_cast<uint64_t>(k) + 0xA264u);
    if (EnvNumber("SE_GENERATED_PRINTEDC_FLAGS", 1) != 0 && flags.Chance(20))
    {
        const RegItem item = MakeFlagLit(flags);
        const size_t at = flags.Chance(60) ? f.items.size() : flags.Below(static_cast<uint32_t>(f.items.size()) + 1);
        f.items.insert(f.items.begin() + static_cast<long>(at), item);
    }
    const uint32_t s = rng.Below(100);
    std::string shape;
    f.slot = s < 70 ? static_cast<uint16_t>(0x6003 | rng.Below(8) << 4) : s < 90 ? RegOpWord(rng, shape) : uint16_t(0x0009);
    return f;
}

// A local array filled from the top down (A3-65): `*--p = v` over a frame, which compiles to a copy of r15 walked down by `mov.l Rm,@-Rn`.
// The engine took a pointer into the frame to reach only what lies at or above its base, so a local the walk wrote below it kept the value it
// had before the loop (`return 5;` where the code returns 1), and a walk from the entry r15 itself printed as the stack register
// (`register0x0000003c`, `BADSPACEBASE *`). The item: allocate a frame of F bytes, store into some of its slots at the width W of the walk,
// copy r15 (or r15 + N) into a pointer, walk it down a counted number of slots storing a value (the counter, or a register stepped each pass),
// sometimes read the slots back up with `mov @Rp+` and sum them, read one slot back into r0, release the frame. A quarter of the walks go up
// instead (`mov Rm,@Rp ; add #W,Rp`, the control the engine always had right). Every slot read was written first, at the width it is read, so
// the run and the text agree on a little-endian host as well (the printed-C harness maps the frame onto memory: frame mode, CheckPrintedC).
RegItem MakeWalkItem(Rng& rng)
{
    RegItem it;
    static const int kWidths[3] = { 1, 2, 4 };
    const int w = kWidths[rng.Below(3)];
    const int sz = w == 1 ? 0 : w == 2 ? 1 : 2;
    const int frame = w == 4 ? 8 + 4 * static_cast<int>(rng.Below(5)) : 8 + 4 * static_cast<int>(rng.Below(3));   // 8-24 / 8-16 bytes
    const int slots = frame / w;
    const bool up = rng.Chance(25);
    // three distinct registers of r1-r7: the pointer, the counter, the value stored (the counter itself a third of the time)
    uint32_t regs[7] = { 1, 2, 3, 4, 5, 6, 7 };
    for (int i = 6; i > 0; --i) std::swap(regs[i], regs[rng.Below(static_cast<uint32_t>(i + 1))]);
    const uint32_t p = regs[0], c = regs[1];
    const uint32_t v = rng.Chance(33) ? c : regs[2];
    const bool stepValue = v != c && rng.Chance(50);
    int start, count;   // the slot the pointer starts at (in units of w from r15), how many slots the loop writes
    if (up)
    {
        start = static_cast<int>(rng.Below(static_cast<uint32_t>(slots)));
        count = 1 + static_cast<int>(rng.Below(static_cast<uint32_t>(slots - start)));
    }
    else
    {
        start = 1 + static_cast<int>(rng.Below(static_cast<uint32_t>(slots)));   // up to the frame's top: the entry r15
        count = 1 + static_cast<int>(rng.Below(static_cast<uint32_t>(start)));
    }
    const int lowest = up ? start : start - count, highest = up ? start + count - 1 : start - 1;   // the slots the loop writes
    const int back = static_cast<int>(rng.Below(static_cast<uint32_t>(slots)));                    // the slot read back into r0
    it.shape = std::string("walk.") + (up ? "up." : "down.") + (w == 1 ? "b" : w == 2 ? "w" : "l");
    it.w.push_back(static_cast<uint16_t>(0x7F00 | ((0x100 - frame) & 0xFF)));   // add #-F,r15
    auto store = [&](int slot, uint32_t m) {
        if (w == 4) it.w.push_back(static_cast<uint16_t>(0x1F00 | m << 4 | slot));   // mov.l Rm,@(4*slot,r15)
        else
        {
            if (m != 0) it.w.push_back(static_cast<uint16_t>(0x6003 | m << 4));          // mov Rm,r0
            it.w.push_back(static_cast<uint16_t>((w == 2 ? 0x81F0 : 0x80F0) | slot));      // mov.w / mov.b r0,@(disp,r15)
        }
    };
    // the slots first written before the loop: the one read back if the loop does not write it, and some others
    for (int s = 0; s < slots; ++s)
        if ((s == back && (s < lowest || s > highest)) || rng.Chance(30)) store(s, rng.Below(8));
    it.w.push_back(static_cast<uint16_t>(0x6003 | p << 8 | 15 << 4));                     // mov r15,Rp
    if (start) it.w.push_back(static_cast<uint16_t>(0x7000 | p << 8 | (start * w)));      // add #N,Rp
    it.w.push_back(static_cast<uint16_t>(0xE000 | c << 8 | count));                       // mov #count,Rc
    const size_t loop = it.w.size();
    if (up)
    {
        it.w.push_back(static_cast<uint16_t>(0x2000 | p << 8 | v << 4 | sz));             // L: mov.X Rv,@Rp
        it.w.push_back(static_cast<uint16_t>(0x7000 | p << 8 | w));                       //    add #W,Rp
    }
    else
        it.w.push_back(static_cast<uint16_t>(0x2004 | p << 8 | v << 4 | sz));             // L: mov.X Rv,@-Rp
    if (stepValue) it.w.push_back(static_cast<uint16_t>(0x7000 | v << 8 | 0x03));         //    add #3,Rv
    it.w.push_back(static_cast<uint16_t>(0x4010 | c << 8));                               //    dt Rc
    it.w.push_back(static_cast<uint16_t>(0x8B00 | ((static_cast<int>(loop) - static_cast<int>(it.w.size()) - 2) & 0xFF)));   // bf L
    // a walk down read back up: the pointer is at the lowest slot written; sum what the loop wrote into the counter's register
    const bool readBack = !up && rng.Chance(35);
    if (readBack)
    {
        const uint32_t t = v == c ? regs[2] : regs[3], a = regs[4];
        it.w.push_back(static_cast<uint16_t>(0xE000 | c << 8 | count));                   // mov #count,Rc
        it.w.push_back(static_cast<uint16_t>(0xE000 | a << 8));                           // mov #0,Ra
        const size_t loop2 = it.w.size();
        it.w.push_back(static_cast<uint16_t>(0x6004 | t << 8 | p << 4 | sz));             // L2: mov.X @Rp+,Rt
        it.w.push_back(static_cast<uint16_t>(0x300C | a << 8 | t << 4));                  //     add Rt,Ra
        it.w.push_back(static_cast<uint16_t>(0x4010 | c << 8));                           //     dt Rc
        it.w.push_back(static_cast<uint16_t>(0x8B00 | ((static_cast<int>(loop2) - static_cast<int>(it.w.size()) - 2) & 0xFF)));
        it.shape += ".readback";
        if (w == 4) it.w.push_back(static_cast<uint16_t>(0x50F0 | back));                 // mov.l @(4*back,r15),r0
        else it.w.push_back(static_cast<uint16_t>((w == 2 ? 0x85F0 : 0x84F0) | back));     // mov.w / mov.b @(disp,r15),r0
        it.w.push_back(static_cast<uint16_t>(0x300C | a << 4));                           // add Ra,r0
    }
    else if (w == 4) it.w.push_back(static_cast<uint16_t>(0x50F0 | back));                // mov.l @(4*back,r15),r0
    else it.w.push_back(static_cast<uint16_t>((w == 2 ? 0x85F0 : 0x84F0) | back));        // mov.w / mov.b @(disp,r15),r0
    it.w.push_back(static_cast<uint16_t>(0x7F00 | frame));                                // add #F,r15
    return it;
}

// A function of its own for the walk (A3-65), from a stream of its own: the seeds' other functions, and the inputs each is run with, are the
// ones of the rounds before (the walk functions are checked after every other case). A few register-only items, the walk, sometimes one more
// operation on r0. Empty when the seed draws none.
bool GenWalkFn(uint64_t seed, RegFn& f)
{
    Rng rng(seed * 2654435761u + 0xA365u);
    if (!rng.Chance(20)) return false;
    f = RegFn();
    if (rng.Chance(60))
    {
        RegItem copies;
        copies.w = { 0x6043, 0x6153, 0x6263, 0x6373 };
        copies.shape = "mov";
        f.items.push_back(copies);
    }
    const int before = static_cast<int>(rng.Below(3));
    for (int i = 0; i < before; ++i)
    {
        RegItem it;
        it.w.push_back(RegOpWord(rng, it.shape));
        f.items.push_back(it);
    }
    f.items.push_back(MakeWalkItem(rng));
    if (rng.Chance(25))
    {
        RegItem it;
        static const uint16_t kAfter[] = { 0x600C, 0x600D, 0x600E, 0x600F, 0x7001, 0x4000, 0x4001, 0x600B };   // extu.b/.w, exts.b/.w, add #1, shll, shlr, neg
        it.w.push_back(kAfter[rng.Below(8)]);
        it.shape = "after";
        f.items.push_back(it);
    }
    f.slot = 0x0009;
    return true;
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

// The frame of a function in frame mode (A3-65, CheckPrintedC): 64 KiB of memory for the stack locals, the entry r15 at its middle, mapped below
// 2 GiB where the host allows it, so that a text that casts a frame address to a four-byte integer and back (`(int4 *)((int4)p + 4)`) keeps it.
static const char kFrameHeader[] = R"FRAME(
#if defined(__linux__)
#include <sys/mman.h>
#endif
static unsigned long long g_frame_base;
static unsigned char g_frame_static[0x10000];
static void FrameInit(void)
{
#if defined(__linux__) && defined(MAP_32BIT)
  void *m = mmap(0, 0x10000, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0);
  if (m != MAP_FAILED) { g_frame_base = (unsigned long long)(uintptr_t)m; return; }
#endif
  g_frame_base = (unsigned long long)(uintptr_t)g_frame_static;
}
)FRAME";

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
         // xunknown is the engine's type with no sign: unsigned in this reading, signed in the engine's (CxxHeader)
         "typedef unsigned char xunknown1; typedef unsigned short xunknown2; typedef unsigned int xunknown4; typedef unsigned long long xunknown8;\n"
         "typedef unsigned int unkbyte3; typedef unsigned int unkuint3; typedef int unkint3; typedef unsigned long long unkbyte5; typedef unsigned long long unkbyte6;\n"
         "typedef unsigned long long unkbyte7; typedef unsigned long long unkuint5; typedef unsigned long long unkuint6; typedef unsigned long long unkuint7;\n"
         "typedef long long unkint5; typedef long long unkint6; typedef long long unkint7;\n"
         "#define true 1\n#define false 0\n"
         // The engine's field notation for a piece of a variable (`xStack_4._2_2_`: bytes 2 and 3, the first byte being the most
         // significant on this machine), as the C it means.
         "#define FLDMASK(n) ((n) >= 8 ? ~0ULL : ((1ULL << (8 * (n))) - 1))\n"
         // ...with the type of its size, so that (int4) of a two-byte field is the extension the engine's type means (see the second reading)
         "#define FLDRAW(v, o, n) (((unsigned long long)(v) >> (8 * (sizeof(v) - (o) - (n)))) & FLDMASK(n))\n"
         "#define FLDT_1(x) ((xunknown1)(x))\n#define FLDT_2(x) ((xunknown2)(x))\n#define FLDT_3(x) ((unkbyte3)(x))\n#define FLDT_4(x) ((xunknown4)(x))\n"
         "#define FLDT_5(x) ((unsigned long long)(x))\n#define FLDT_6(x) ((unsigned long long)(x))\n#define FLDT_7(x) ((unsigned long long)(x))\n#define FLDT_8(x) ((unsigned long long)(x))\n"
         "#define FLD(v, o, n) FLDT_##n(FLDRAW(v, o, n))\n"
         "#define SETFLD(v, o, n, e) ((v) = (__typeof__(v))(((unsigned long long)(v) & ~(FLDMASK(n) << (8 * (sizeof(v) - (o) - (n))))) | "
         "(((unsigned long long)(e) & FLDMASK(n)) << (8 * (sizeof(v) - (o) - (n))))))\n"
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
    o << kFrameHeader;
    return o.str();
}

static const char kCxxHeaderBase[] = R"CXX(// The engine's reading of its own pseudocode (A2-19, A2-33), as C++: every integer type is a class that does arithmetic at its own width, as the
// p-code does and as DECOMPILER_GUIDE.md says to read the text ("read the types, not C's promotion rules"). Two operands are brought to the
// wider one (each extended by its own sign), the result is unsigned if either is, at one width; a comparison or a logical operator gives
// a four-byte unsigned (the extension of the engine's bool, which the text does not show); `(uint8)x` extends by the target's sign, as ZEXT and SEXT do
// (an implicit conversion, invisible in the text, by the source's); xunknown has no sign and is read as signed.
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <type_traits>
typedef unsigned long long u64;
typedef long long s64;
template <int N> struct Rep { typedef typename std::conditional<(N <= 1), uint8_t, typename std::conditional<(N <= 2), uint16_t, typename std::conditional<(N <= 4), uint32_t, uint64_t>::type>::type>::type type; };
template <int N, bool S> struct T;
template <int N> struct Piece;
template <class I> struct IsInt { static const bool value = std::is_integral<I>::value; };
static inline u64 B2U(bool x) { return x ? 1 : 0; }
template <class I> static inline typename std::enable_if<std::is_signed<I>::value && !std::is_same<I, bool>::value, u64>::type B2U(I x) { return (u64)(s64)x; }
template <class I> static inline typename std::enable_if<!std::is_signed<I>::value && !std::is_same<I, bool>::value, u64>::type B2U(I x) { return (u64)x; }
template <int N, bool S> struct T {
  typedef typename Rep<N>::type R;
  R v;
  static const int size = N;
  static const bool sign = S;
  static u64 Mask() { return N >= 8 ? ~0ULL : ((1ULL << (8 * N)) - 1); }
  T() : v(0) {}
  template <class I, class = typename std::enable_if<IsInt<I>::value>::type> T(I x) : v((R)(B2U(x) & Mask())) {}
  // a cast (explicit): a wider target extends by the source's sign, as C does, except the unsigned 8-byte cast of a 4-byte value, which is the
  // zero extension the engine builds a 64-bit product with (`(uint8)x` of a signed x: A2-19)
  template <int M, bool U> explicit T(const T<M, U>& o) : v((R)((N > M ? ((N == 8 && M == 4 && !S) ? o.Zext() : o.Ext()) : o.Zext()) & Mask())) {}
  // the cast of a piece (CONCATab, the engine's untyped value of a + b bytes): for 5 to 7 bytes, which have no type of their own in C, a cast to an unsigned
  // 8-byte type is the ZEXT the engine builds (`(uint8)CONCAT14((char)in_r1,in_r1)`: the high word of the run is 0, not the sign: A2-79). The engine prints a
  // cast to the type of the variable it assigns to, so for sizes with a type (a 2-byte piece read by `mov.w` into a uint4) the extension is by the piece's
  // sign, as for any other cast
  template <int M> explicit T(const Piece<M>& o) : v((R)((N > M ? ((!S && M >= 5) ? o.Zext() : ((N == 8 && M == 4 && !S) ? o.Zext() : o.Ext())) : o.Zext()) & Mask())) {}
  // an implicit conversion, the one C does: by the source's sign
  template <int M, bool U> operator T<M, U>() const { return T<M, U>(B2U(Ext())); }
  u64 Zext() const { return (u64)v & Mask(); }
  u64 Sext() const { return N >= 8 ? (u64)v : (u64)(((s64)((u64)v << (64 - 8 * N))) >> (64 - 8 * N)); }
  u64 Ext() const { return S ? Sext() : Zext(); }
  explicit operator bool() const { return Zext() != 0; }
  template <class I, class = typename std::enable_if<std::is_arithmetic<I>::value && !std::is_same<I, bool>::value>::type> explicit operator I() const { return (I)Ext(); }
};
template <int N, bool S, int M, bool U> struct Common {
  static const int size = N > M ? N : M;
  static const bool sign = N == M ? (S && U) : (N > M ? S : U);
  typedef T<size, sign> type;
};
// The value of a CONCATab: signed as an xunknown is in this reading, with the cast above for the extension
template <int N> struct Piece : T<N, true> {
  Piece() {}
  template <class I, class = typename std::enable_if<IsInt<I>::value>::type> Piece(I x) : T<N, true>(x) {}
};
typedef T<4, false> Bool4;
// A value narrower than int meeting a plain number (the text's literal has no width of its own) is brought to four bytes first, keeping its sign: the
// engine extends it where C's promotion would, invisibly, so `(in_sr & 1) - 1 >> 2` of a uint1 is a four-byte unsigned subtraction and a logical shift
template <int N, bool S> struct Promoted { typedef T<(N < 4 ? 4 : N), S> type; };
#define BINARY(op, body) \
  template <int N, bool S, int M, bool U> typename Common<N, S, M, U>::type operator op(const T<N, S>& a, const T<M, U>& b) { \
    typedef typename Common<N, S, M, U>::type C; \
    C x, y; x.v = (typename C::R)(a.Ext() & C::Mask()); y.v = (typename C::R)(b.Ext() & C::Mask()); \
    const bool sg = C::sign; (void)sg; \
    body } \
  template <int N, bool S, class I, class = typename std::enable_if<IsInt<I>::value>::type> typename Promoted<N, S>::type operator op(const T<N, S>& a, I b) { typedef typename Promoted<N, S>::type P; return P(a) op P(b); } \
  template <int N, bool S, class I, class = typename std::enable_if<IsInt<I>::value>::type> typename Promoted<N, S>::type operator op(I a, const T<N, S>& b) { typedef typename Promoted<N, S>::type P; return P(a) op P(b); }
BINARY(+, return C((x.Zext() + y.Zext()) & C::Mask());)
BINARY(-, return C((x.Zext() - y.Zext()) & C::Mask());)
BINARY(*, return C((x.Zext() * y.Zext()) & C::Mask());)
BINARY(&, return C(x.Zext() & y.Zext());)
BINARY(|, return C(x.Zext() | y.Zext());)
BINARY(^, return C(x.Zext() ^ y.Zext());)
BINARY(/, if (y.Zext() == 0) return C(0); if (sg && (s64)y.Sext() == -1) return C((0 - x.Zext()) & C::Mask()); return sg ? C((u64)((s64)x.Sext() / (s64)y.Sext())) : C(x.Zext() / y.Zext());)
BINARY(%, if (y.Zext() == 0) return C(0); if (sg && (s64)y.Sext() == -1) return C(0); return sg ? C((u64)((s64)x.Sext() % (s64)y.Sext())) : C(x.Zext() % y.Zext());)
#define CMP(op) \
  template <int N, bool S, int M, bool U> Bool4 operator op(const T<N, S>& a, const T<M, U>& b) { \
    typedef typename Common<N, S, M, U>::type C; \
    C x, y; x.v = (typename C::R)(a.Ext() & C::Mask()); y.v = (typename C::R)(b.Ext() & C::Mask()); \
    return Bool4(C::sign ? ((s64)x.Sext() op (s64)y.Sext()) : (x.Zext() op y.Zext())); } \
  template <int N, bool S, class I, class = typename std::enable_if<IsInt<I>::value>::type> Bool4 operator op(const T<N, S>& a, I b) { typedef typename Promoted<N, S>::type P; return P(a) op P(b); } \
  template <int N, bool S, class I, class = typename std::enable_if<IsInt<I>::value>::type> Bool4 operator op(I a, const T<N, S>& b) { typedef typename Promoted<N, S>::type P; return P(a) op P(b); }
CMP(==) CMP(!=) CMP(<) CMP(<=) CMP(>) CMP(>=)
// shifts take the type of the left operand
template <int N, bool S, int M, bool U> T<N, S> operator<<(const T<N, S>& a, const T<M, U>& b) { u64 c = b.Zext(); return T<N, S>(c >= 8 * N ? 0 : (a.Zext() << c)); }
template <int N, bool S, int M, bool U> T<N, S> operator>>(const T<N, S>& a, const T<M, U>& b) {
  u64 c = b.Zext();
  if (S) return T<N, S>(c >= 8 * N ? ((s64)a.Sext() < 0 ? ~0ULL : 0ULL) : (u64)((s64)a.Sext() >> c));
  return T<N, S>(c >= 8 * N ? 0 : (a.Zext() >> c)); }
template <int N, bool S, class I, class = typename std::enable_if<IsInt<I>::value>::type> T<N, S> operator<<(const T<N, S>& a, I b) { return a << T<4, false>(b); }
template <int N, bool S, class I, class = typename std::enable_if<IsInt<I>::value>::type> T<N, S> operator>>(const T<N, S>& a, I b) { return a >> T<4, false>(b); }
template <int N, bool S> T<N, S> operator-(const T<N, S>& a) { return T<N, S>((0 - a.Zext()) & T<N, S>::Mask()); }
template <int N, bool S> T<N, S> operator~(const T<N, S>& a) { return T<N, S>(~a.Zext() & T<N, S>::Mask()); }
template <int N, bool S> T<N, S> operator+(const T<N, S>& a) { return a; }
template <int N, bool S> Bool4 operator!(const T<N, S>& a) { return Bool4(a.Zext() == 0); }
template <int N, bool S, int M, bool U> Bool4 operator&&(const T<N, S>& a, const T<M, U>& b) { return Bool4(a.Zext() != 0 && b.Zext() != 0); }
template <int N, bool S, int M, bool U> Bool4 operator||(const T<N, S>& a, const T<M, U>& b) { return Bool4(a.Zext() != 0 || b.Zext() != 0); }
template <int N, bool S, class I, class = typename std::enable_if<IsInt<I>::value>::type> Bool4 operator&&(const T<N, S>& a, I b) { return Bool4(a.Zext() != 0 && b != 0); }
template <int N, bool S, class I, class = typename std::enable_if<IsInt<I>::value>::type> Bool4 operator&&(I a, const T<N, S>& b) { return Bool4(a != 0 && b.Zext() != 0); }
template <int N, bool S, class I, class = typename std::enable_if<IsInt<I>::value>::type> Bool4 operator||(const T<N, S>& a, I b) { return Bool4(a.Zext() != 0 || b != 0); }
template <int N, bool S, class I, class = typename std::enable_if<IsInt<I>::value>::type> Bool4 operator||(I a, const T<N, S>& b) { return Bool4(a != 0 || b.Zext() != 0); }
// the names the engine prints (xunknown: no sign, read as signed)
typedef T<1, false> undefined; typedef T<1, false> undefined1; typedef T<2, false> undefined2; typedef T<3, false> undefined3; typedef T<4, false> undefined4; typedef T<8, false> undefined8;
typedef T<1, false> byte; typedef T<1, false> uchar; typedef T<2, false> ushort; typedef T<4, false> uint; typedef T<3, false> uint3; typedef T<8, true> longlong; typedef T<8, false> ulonglong;
typedef T<2, false> word; typedef T<4, false> dword; typedef T<8, false> qword;
typedef T<3, true> int3; typedef T<1, true> int1; typedef T<2, true> int2; typedef T<4, true> int4; typedef T<8, true> int8;
typedef T<1, false> uint1; typedef T<2, false> uint2; typedef T<4, false> uint4; typedef T<8, false> uint8;
typedef T<1, true> xunknown1; typedef T<2, true> xunknown2; typedef T<4, true> xunknown4; typedef T<8, true> xunknown8;
typedef T<3, true> unkbyte3; typedef T<3, false> unkuint3; typedef T<3, true> unkint3;
typedef T<5, true> unkbyte5; typedef T<6, true> unkbyte6; typedef T<7, true> unkbyte7; typedef T<5, false> unkuint5; typedef T<6, false> unkuint6; typedef T<7, false> unkuint7;
typedef T<5, true> unkint5; typedef T<6, true> unkint6; typedef T<7, true> unkint7;
template <class X> static inline u64 RAW(const X& x) { return B2U(x); }
template <int N, bool S> static inline u64 RAW(const T<N, S>& x) { return x.Zext(); }
template <int N> static inline u64 RAW(const Piece<N>& x) { return x.Zext(); }
template <class X> struct SZ { static const int n = sizeof(X); };
template <int N, bool S> struct SZ<T<N, S> > { static const int n = N; };
template <int N> struct SZ<Piece<N> > { static const int n = N; };
#define SIZEOFV(v) (SZ<__typeof__(v)>::n)
#define FLDMASK(n) ((n) >= 8 ? ~0ULL : ((1ULL << (8 * (n))) - 1))
#define FLDRAW(v, o, n) ((RAW(v) >> (8 * (SIZEOFV(v) - (o) - (n)))) & FLDMASK(n))
#define FLDT_1(x) (xunknown1((u64)(x)))
#define FLDT_2(x) (xunknown2((u64)(x)))
#define FLDT_3(x) (unkbyte3((u64)(x)))
#define FLDT_4(x) (xunknown4((u64)(x)))
#define FLDT_5(x) (T<5, true>((u64)(x)))
#define FLDT_6(x) (T<6, true>((u64)(x)))
#define FLDT_7(x) (T<7, true>((u64)(x)))
#define FLDT_8(x) (T<8, false>((u64)(x)))
#define FLD(v, o, n) FLDT_##n(FLDRAW(v, o, n))
#define SETFLD(v, o, n, e) ((v) = static_cast<__typeof__(v)>((RAW(v) & ~(FLDMASK(n) << (8 * (SIZEOFV(v) - (o) - (n))))) | ((RAW(e) & FLDMASK(n)) << (8 * (SIZEOFV(v) - (o) - (n))))))
static inline Bool4 CARRY4(u64 a, u64 b) { return Bool4((uint32_t)((uint32_t)a + (uint32_t)b) < (uint32_t)a); }
static inline Bool4 CARRY1(u64 a, u64 b) { return Bool4((uint8_t)((uint8_t)a + (uint8_t)b) < (uint8_t)a); }
static inline Bool4 SCARRY4(u64 a, u64 b) { return Bool4(((int32_t)a >= 0) == ((int32_t)b >= 0) && (((int32_t)((uint32_t)a + (uint32_t)b) >= 0) != ((int32_t)a >= 0))); }
static inline Bool4 SCARRY1(u64 a, u64 b) { return Bool4(((int8_t)a >= 0) == ((int8_t)b >= 0) && (((int8_t)((uint8_t)a + (uint8_t)b) >= 0) != ((int8_t)a >= 0))); }
static inline Bool4 SBORROW4(u64 a, u64 b) { return Bool4(((int32_t)a >= 0) != ((int32_t)b >= 0) && (((int32_t)((uint32_t)a - (uint32_t)b) >= 0) != ((int32_t)a >= 0))); }
static inline Bool4 SBORROW1(u64 a, u64 b) { return Bool4(((int8_t)a >= 0) != ((int8_t)b >= 0) && (((int8_t)((uint8_t)a - (uint8_t)b) >= 0) != ((int8_t)a >= 0))); }
#define CARRY4(a, b) CARRY4(RAW(a), RAW(b))
#define CARRY1(a, b) CARRY1(RAW(a), RAW(b))
#define SCARRY4(a, b) SCARRY4(RAW(a), RAW(b))
#define SCARRY1(a, b) SCARRY1(RAW(a), RAW(b))
#define SBORROW4(a, b) SBORROW4(RAW(a), RAW(b))
#define SBORROW1(a, b) SBORROW1(RAW(a), RAW(b))
static unsigned int g_in[16];
static unsigned int g_wsr, g_wgbr, g_wvbr;
template <class X> static void Write_SR(const X& v) { g_wsr = (unsigned int)RAW(v); }
template <class X> static void Write_GBR(const X& v) { g_wgbr = (unsigned int)RAW(v); }
template <class X> static void Write_VBR(const X& v) { g_wvbr = (unsigned int)RAW(v); }
)CXX";

// The compiler for the engine reading: SE_CXX_COMPILER, else the C++ driver that goes with the C compiler found (gcc -> g++, clang -> clang++, cc -> c++).
std::string FindCxxCompiler(const std::string& cc)
{
    if (const char* env = std::getenv("SE_CXX_COMPILER"))
        if (*env) return env;
    std::vector<std::string> tries;
    if (cc.find("clang") != std::string::npos) tries.push_back(cc.substr(0, cc.rfind("clang")) + "clang++");
    else if (cc.size() >= 3 && cc.compare(cc.size() - 3, 3, "gcc") == 0) tries.push_back(cc.substr(0, cc.size() - 3) + "g++");
    else if (cc.size() >= 2 && cc.compare(cc.size() - 2, 2, "cc") == 0) tries.push_back(cc.substr(0, cc.size() - 2) + "c++");
    for (const char* name : { "c++", "g++", "clang++" }) tries.push_back(name);
    for (const std::string& t : tries)
        if (std::system((t + " --version > /dev/null 2>&1").c_str()) == 0) return t;
    return std::string();
}

// The engine's reading of an unsigned literal (A2-60). The engine prints `U` on a constant whose operation is unsigned where the other operand is
// signed (`param_1 + 0x80U` of an int4 param_1 is an unsigned sum and `0x7f < param_1 + 0x80U` an unsigned compare); in C++ `0x80U` and a plain
// `0xffffffff` are both an `unsigned int`, which Promoted brings to the other operand's sign like any plain number, so the U was lost. Here the
// suffixed literals are made the engine's unsigned types (`uint4(0x80)`, `uint8(...)` for ULL), which Common and Promoted treat as unsigned.
std::string UnsignedLiteralsTyped(const std::string& text)
{
    static const std::regex wide("\\b(0x[0-9a-fA-F]+|[0-9]+)(?:ULL|LLU|ull|llu)\\b");
    static const std::regex narrow("\\b(0x[0-9a-fA-F]+|[0-9]+)[Uu]\\b");
    return std::regex_replace(std::regex_replace(text, wide, "uint8($1)"), narrow, "uint4($1)");
}

// The engine's reading of a negative literal (A2-71). The engine prints an 8-byte constant of -2^31 and below, to -(2^32-1), as `-0x80000000`: a
// negative number of the arithmetic around it, which the 8-byte operand is multiplied by or added to. In C and C++ a hexadecimal literal that does
// not fit an int is an `unsigned int` and its negation the same unsigned int, so `(int8)param_2 * -0x80000000` multiplied by +2^31 (and
// the verdict was `mismatch` where the text means the product by -2^31, which agrees with the run). Here the literal is a `long long`; at four bytes
// the value is the same bits (the class takes the low four bytes), so no function whose text is read at four bytes changes.
std::string NegativeWideLiteralsTyped(const std::string& text)
{
    static const std::regex neg("-(0x[89a-fA-F][0-9a-fA-F]{7})\\b");
    return std::regex_replace(text, neg, "-$1LL");
}

std::string CxxHeader()
{
    std::ostringstream o;
    o << kCxxHeaderBase;
    auto mask = [](int bytes) -> std::string {
        char b[32];
        std::snprintf(b, sizeof b, "0x%sULL", std::string(static_cast<size_t>(bytes) * 2, 'F').c_str());
        return b;
    };
    // The piece operations, typed by the size they give (xunknown: signed in this reading).
    for (int a = 1; a <= 7; ++a)
        for (int b = 1; a + b <= 8; ++b)
            o << "#define CONCAT" << a << b << "(a, b) (Piece<" << a + b << ">(((RAW(a) & " << mask(a) << ") << " << 8 * b << ") | (RAW(b) & " << mask(b) << ")))\n";
    for (int a = 2; a <= 8; ++a)
        for (int b = 1; b < a; ++b)
            o << "#define SUB" << a << b << "(v, o) (T<" << b << ", true>((RAW(v) >> ((o) * 8)) & " << mask(b) << "))\n";
    for (int a = 1; a <= 7; ++a)
        for (int b = a + 1; b <= 8; ++b)
        {
            o << "#define ZEXT" << a << b << "(v) (T<" << b << ", false>(RAW(v) & " << mask(a) << "))\n";
            o << "#define SEXT" << a << b << "(v) (T<" << b << ", true>(T<" << a << ", true>(RAW(v)).Sext()))\n";
        }
    o << kFrameHeader;
    o << "#define char T<1, true>\n#define bool T<1, false>\n#define true 1\n#define false 0\n";
    return o.str();
}

struct CVerdict
{
    std::string cls;      // "" agrees; else the class of the disagreement
    std::string detail;
    std::string text;
    bool compared = false;
    bool voidReturn = false;
    bool upperBits = false;   // a narrow return whose r0 upper bits are no extension of it (A2-26): never excused as C's reading
    int mismatches = 0;
};

struct CCase
{
    std::string name;
    std::vector<uint16_t> words;
};

// The type a name the engine makes for an access wider than its symbol (`_Stack_4`) is declared with in the printed-C harness (A2-57): the
// engine's untyped name of the width of the write. The text gives the width as `_Stack_4 = CONCATab(...)` (a + b bytes); a name written by
// no CONCAT, or by CONCATs of different widths, or of a width with no untyped name in both readings (1, 2, 3, 4 and 8 have one), is declared
// `unsigned long long`, as every such name was before.
std::string UnderscoreNameType(const std::string& text, const std::string& name)
{
    const std::regex write("\\b" + name + " = CONCAT([0-9])([0-9])\\(");
    int width = 0;
    for (std::sregex_iterator it(text.begin(), text.end(), write), end; it != end; ++it)
    {
        const int w = std::atoi((*it)[1].str().c_str()) + std::atoi((*it)[2].str().c_str());
        if (width != 0 && width != w) return "unsigned long long";
        width = w;
    }
    switch (width)
    {
    case 1: return "xunknown1";
    case 2: return "xunknown2";
    case 3: return "unkbyte3";
    case 4: return "xunknown4";
    case 8: return "xunknown8";
    default: return "unsigned long long";
    }
}

// The value r8-r14 start with in a printed-C run. Its four bytes differ from each other, for every register (A2-44): with
// 0xA0000000 + r * 0x01010101 the bytes 1 and 2 were equal, so a text that named the two the wrong way round (`unaff_r8_b1` for the register's
// byte 2) read the same value under either name and the differential could not tell.
uint32_t CalleeSaved(int r) { return (0xA0000000u + static_cast<uint32_t>(r) * 0x01010101u) ^ 0x00010203u; }

// The edge values the printed-C inputs walk through r4-r7 first (the first poolSize inputs). It has the sign boundaries of the byte, the
// word and the longword, and (A2-40) the negative values whose low 8, 16 or 24 bits are positive, which a text that sign-extends a narrow
// piece of a value that is not itself narrow cannot tell from the run without.
const std::vector<uint32_t>& PrintedCPool()
{
    static const std::vector<uint32_t> pool = { 0, 1, 2, 0xFFFFFFFFu, 0x7FFFFFFFu, 0x80000000u, 0x80000001u, 0xFFFFFFFEu, 0x7FFF, 0x8000, 0xFFFF,
                                                0x10000, 0x7F, 0x80, 0xFF, 0x100, 0xFFFF8000u, 0xFFFFFF80u, 3, 31, 32, 33,
                                                0xFFFF7FFFu, 0xFFFFFF7Fu, 0xFF7FFFFFu, 0x7F7F7F7Fu };
    return pool;
}

// The class of a text that returns nothing over a run that changed r0. A function that returns nothing but whose r0 ended as something
// other than it began: a return the engine dropped, or r0 left as scratch. Compiled code cannot tell the two apart, and 40 percent of the
// corpora's lent signatures are such functions (withholding every one would cost 235 of 600 and 431 of 984: the round 5 record), so a `void`
// text is no failure by itself (`void-return`, and `void-return-lent` when the signature is lent: counted, visible). But the generator knows
// when it meant a result: the rts slot `mov Rm,r0` is the compiled idiom for `return Rm`. A `void` signature lent over that is a return
// dropped, which is A3-5's decision (such a function is withheld, not lent void) and a failure (A2-34).
std::string VoidReturnClass(bool signatureKnown, const std::vector<uint16_t>& words)
{
    const auto rts = std::find(words.begin(), words.end(), uint16_t(0x000B));
    const bool intent = rts != words.end() && rts + 1 != words.end() && (*(rts + 1) & 0xFF0Fu) == 0x6003u && *(rts + 1) != 0x6003u;   // mov Rm,r0, Rm not r0
    return !signatureKnown ? "void-return" : intent ? "return-dropped" : "void-return-lent";
}

// The two above are the oracle's own decisions, not the engine's, so a mutation of either does not fail the engine's fixtures: pinned here.
void TestOracleOwnDecisions()
{
    // A2-34: the slot is the intent; a withheld signature, another slot, an r0-to-r0 move and a move before the rts are not
    CHECK(VoidReturnClass(true, { 0x000B, 0x6013 }) == "return-dropped");        // rts ; mov r1,r0
    CHECK(VoidReturnClass(true, { 0x000B, 0x63F3 }) == "void-return-lent");      // rts ; mov r15,r3
    CHECK(VoidReturnClass(false, { 0x000B, 0x6013 }) == "void-return");
    CHECK(VoidReturnClass(true, { 0x000B, 0x0009 }) == "void-return-lent");      // rts ; nop
    CHECK(VoidReturnClass(true, { 0x000B, 0x6003 }) == "void-return-lent");      // rts ; mov r0,r0
    CHECK(VoidReturnClass(true, { 0x6013, 0x000B, 0x0009 }) == "void-return-lent");
    CHECK(VoidReturnClass(true, { 0x6013 }) == "void-return-lent");              // no rts at all
    // A2-44: the entry values of r8-r14 have four different bytes (a text that numbered the bytes 1 and 2 the wrong way round read the same
    // value under both names)
    for (int r = 8; r <= 14; ++r)
    {
        const uint32_t v = CalleeSaved(r);
        for (int i = 0; i < 4; ++i)
            for (int j = i + 1; j < 4; ++j) CHECK(((v >> (8 * i)) & 0xFF) != ((v >> (8 * j)) & 0xFF));
    }
    // A2-40: for each width a value with all the bits above it set whose narrow piece is positive
    const std::vector<uint32_t>& pool = PrintedCPool();
    for (int bits : { 8, 16, 24 })
    {
        const uint32_t high = ~0u << bits;
        bool found = false;
        for (uint32_t v : pool) found = found || ((v & high) == high && !((v >> (bits - 1)) & 1u));
        CHECK(found);
    }
    // A2-60: a literal the engine prints with `U` is unsigned in the engine's reading, a plain one takes the other operand's type
    CHECK(UnsignedLiteralsTyped("0x7f < param_1 + 0x80U") == "0x7f < param_1 + uint4(0x80)");
    CHECK(UnsignedLiteralsTyped("x * 3U + 0xfffffffeULL") == "x * uint4(3) + uint8(0xfffffffe)");
    CHECK(UnsignedLiteralsTyped("uVar1 = in_r2U + 0xffffffff; uRam0600U = 1;") == "uVar1 = in_r2U + 0xffffffff; uRam0600U = 1;");
    // A2-71: an 8-byte constant of -0x80000000 is negative in the engine's reading; the other literals are as they were
    CHECK(NegativeWideLiteralsTyped("(int8)param_2 * -0x80000000") == "(int8)param_2 * -0x80000000LL");
    CHECK(NegativeWideLiteralsTyped("x + -0xfffffffe;") == "x + -0xfffffffeLL;");
    CHECK(NegativeWideLiteralsTyped("x * -0x7fffffff + y - 0x80000000 + -0x80000000U + -0x180000000 + -2147483648") == "x * -0x7fffffff + y - 0x80000000 + -0x80000000U + -0x180000000 + -2147483648");
}

bool IsFailing(const CVerdict& v)
{
    return !v.cls.empty() && v.cls != "narrow-c" && v.cls != "void-return" && v.cls != "void-return-lent" &&
           v.cls != "raw-stack-register" && v.cls.compare(0, 8, "skipped:") != 0;
}

// Frame mode (A3-65): the code reaches memory through a register other than r15, before its rts (the literal pool after it is data). The
// register-only grammar's own memory is its stack items, through r15; the walk of a frame (MakeWalkItem) copies r15 into another register.
bool UsesFrameMemory(const std::vector<uint16_t>& words)
{
    for (size_t i = 0; i < words.size(); ++i)
    {
        const uint16_t w = words[i];
        const uint32_t hi = w >> 12, n = (w >> 8) & 15, m = (w >> 4) & 15, lo = w & 15;
        const bool form = lo <= 2 || (lo >= 4 && lo <= 6);
        if (hi == 2 && form && n != 15) return true;   // mov.X Rm,@Rn / mov.X Rm,@-Rn
        if (hi == 6 && form && m != 15) return true;   // mov.X @Rm,Rn / mov.X @Rm+,Rn
        if (i > 0 && words[i - 1] == 0x000B) break;   // rts's slot is the last instruction looked at
    }
    return false;
}

// The stack offset (from the entry r15) of a local's name: `xStack_8` is 8 below it, `iStack00000004` and `_xStack00000004` 4 above.
bool StackNameOffset(const std::string& name, int64_t& off)
{
    const size_t at = name.find("Stack");
    if (at == std::string::npos) return false;
    const std::string rest = name.substr(at + 5);
    if (rest.size() > 1 && rest[0] == '_')
    {
        off = -static_cast<int64_t>(std::strtoull(rest.c_str() + 1, nullptr, 16));
        return true;
    }
    if (rest.size() == 8)
    {
        off = static_cast<int32_t>(static_cast<uint32_t>(std::strtoul(rest.c_str(), nullptr, 16)));
        return true;
    }
    return false;
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
        bool frame = false;   // frame mode (A3-65): the locals are in g_frame_base's memory, cleared before each call
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
        {
            bool unsettled = false;
            for (const std::string& w : r.warnings) unsettled |= w.find("Exceeded maximum repeats") != std::string::npos;
            if (unsettled && r.status == DecompileStatus::Ok)
            {
                // patch 0055 stopped a repeating action of the engine that did not settle (A2-59's class): failing, whatever the value
                v.cls = "unsettled";
                v.detail = "the engine's simplification did not settle (Exceeded maximum repeats)";
                continue;
            }
        }
        // A3-87/A3-79: a walk that reaches storage no local is mapped to (a pushed register's save slot) prints a pointer into it
        // as the raw stack register `register0x0000003c`, which is no compilable C. That is A3-65's recorded limit for a pointer
        // into unmapped stack (upstream prints it raw). The decompiler now marks such a function unsupported and withholds it
        // (never a lent signature with non-compilable text), so it never offers the text as a decompilation. The printed-C
        // differential excuses this known limit rather than reporting `not-compilable` (the raw text is right, only unprintable).
        if (v.text.find("register0x") != std::string::npos)
        {
            v.cls = "raw-stack-register";
            v.detail = "a pointer into stack storage no local is mapped to prints as the raw register (A3-65 limit; the function is withheld)";
            continue;
        }
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
        // is not run (the host's pointers are 64-bit and its memory is not the machine's). But for its own frame (A3-65): a function whose code
        // reaches memory through a register other than r15 (a copy of it walked over the locals) has its stack locals mapped onto memory
        // (frame mode, below), and a dereference in it is of a frame address: it is run.
        const bool frameMode = UsesFrameMemory(c.words);
        static const std::regex memoryRe("\\*[A-Za-z_(]");
        if (!frameMode && std::regex_search(text, memoryRe))
        {
            v.cls = "skipped:memory";
            continue;
        }
        // Names the harness cannot give a meaning to, counted apart (they are no C either): a piece of the stack no symbol covers
        // (`stack0xfffffffe`), and a local that is an array of bytes (`axStack_3 [3]`, assigned and returned as a value).
        static const std::regex stackPiece("\\bstack0x[0-9a-fA-F]+\\b");
        static const std::regex arrayLocal("\\b\\w+ \\[\\d+\\];");
        if (std::regex_search(text, stackPiece))
        {
            v.cls = "skipped:stack-piece";
            continue;
        }
        if (!frameMode && std::regex_search(text, arrayLocal))
        {
            v.cls = "skipped:array-local";
            continue;
        }
        // The address of a local (`return &xStack_1;`, `(uint4)&xStack00000000 & 0xff`): the run's is an address in the machine's stack, the C's one in
        // the host's, so the two cannot be compared by value (A2-61: the harness took it for 0, a mismatch where the text is right).
        // In frame mode (A3-65) a local's address is a pointer into the mapped frame and the walk's own: only one returned as a value is
        // still incomparable, so there the search is of the return statements alone. A local array named in a return statement is such an
        // address as well (`return axStack_8;`, `return CONCAT44(axStack_4,param_2);`: the array decays to a pointer, the host's frame in the C
        // and the machine's stack in the run), A2-65; an element (`axStack_8[1]`) is a value and is compared.
        static const std::regex stackAddress("&\\s*[A-Za-z]*[Ss]tack_?[0-9a-fA-F]+\\b");
        static const std::regex returnStmt("\\breturn\\b[^;]*;");
        {
            std::string what;   // the address taken, copied: a match into a statement's string would not outlive it
            std::smatch m;
            if (!frameMode)
            {
                if (std::regex_search(text, m, stackAddress)) what = m.str();
            }
            else
            {
                static const std::regex arrayDecl("\\b(\\w+)\\s+\\[\\d+\\];");   // `xunknown1 axStack_8 [8];`
                std::vector<std::string> arrays;
                for (std::sregex_iterator r(text.begin(), text.end(), arrayDecl), e; r != e; ++r) arrays.push_back((*r)[1].str());
                for (std::sregex_iterator r(text.begin(), text.end(), returnStmt), e; r != e && what.empty(); ++r)
                {
                    const std::string stmt = r->str();
                    if (std::regex_search(stmt, m, stackAddress)) what = m.str();
                    for (size_t k = 0; k < arrays.size() && what.empty(); ++k)
                        if (std::regex_search(stmt, std::regex("\\b" + arrays[k] + "\\b(?!\\s*\\[)"))) what = arrays[k];
                }
            }
            if (!what.empty())
            {
                v.cls = "skipped:stack-address";
                v.detail = "the text computes the address of a local ('" + what + "')";
                continue;
            }
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
        // Field notation (`v._o_n_ = e;` and reads of it) is Ghidra's, not C's: the macros of the header say what it means.
        static const std::regex fieldSet("\\b([A-Za-z_]\\w*)\\._(\\d+)_(\\d+)_ =\\s+([^;]*);");   // the printer wraps a long one: `x._1_3_ =` and the value below (A2-39)
        static const std::regex fieldGet("\\b([A-Za-z_]\\w*)\\._(\\d+)_(\\d+)_");
        text = std::regex_replace(text, fieldSet, "SETFLD($1, $2, $3, $4);");
        text = std::regex_replace(text, fieldGet, "FLD($1, $2, $3)");
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
                std::smatch pieceName;
                int num = -1;
                if (std::sscanf(name.c_str() + (name.compare(0, 3, "in_") == 0 ? 4 : 7), "%d", &num) == 1 && num >= 0 && num <= 15 &&
                    (name == "in_r" + std::to_string(num) || name == "unaff_r" + std::to_string(num)))
                    value = num <= 7 ? "g_in[" + std::to_string(num) + "]" : std::to_string(num == 15 ? kStackTop : CalleeSaved(num)) + "u";
                else if (std::regex_match(name, pieceName, std::regex("(?:in|unaff)_r(\\d+)_b([1-3])")) && std::atoi(pieceName[1].str().c_str()) <= 15)
                {
                    // a byte of a register (cmp/str compares four): named with the byte it is, counted from the least significant (A2-25; r8-r15, A2-37)
                    const int reg = std::atoi(pieceName[1].str().c_str());
                    // (r15 starts at the stack top the run gives it: the text was given CalleeSaved(15), which only agreed with the run
                    // while no input matched a byte of either; A2-44.)
                    const std::string whole = reg <= 7 ? "g_in[" + std::to_string(reg) + "]" : std::to_string(reg == 15 ? kStackTop : CalleeSaved(reg)) + "u";
                    value = "((" + whole + " >> " + std::to_string(8 * (pieceName[2].str()[0] - '0')) + ") & 0xFF)";
                }
                else if (name == "in_sr" || name == "unaff_sr") value = "g_in[8]";
                else if (name == "in_mach" || name == "unaff_mach") value = "g_in[9]";
                else if (name == "in_macl" || name == "unaff_macl") value = "g_in[10]";
                else if (name == "unaff_gbr" || name == "in_gbr") value = "g_in[11]";
                else if (name == "unaff_vbr" || name == "in_vbr") value = "g_in[12]";
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
        // Frame mode (A3-65): every stack local the text declares (`xunknown4 xStack_8;`, `int4 aiStack_10 [3];`, `xunknown1 xStack00000000;`) is
        // a name for the bytes at its offset from the entry r15 in g_frame_base's memory, the entry r15 at its middle, so that the text's pointers
        // into the frame (`piVar2 = &iStack_4; *--piVar2`) reach the locals the code's walk does. The run's stores are of the width they are read
        // at (MakeWalkItem), so the host's byte order does not matter. A name the engine gives the stack register itself (`register0x0000003c`)
        // is left undeclared: the text cannot be run, and fails.
        std::set<std::string> frameNames;
        std::string frameDefines, frameUndefs;
        if (frameMode)
        {
            static const std::regex declRe("^\\s*([A-Za-z_][\\w ]*?[\\w*])\\s*\\b(_?[A-Za-z]*Stack(?:_[0-9a-f]+|[0-9a-f]{8}))(\\s*\\[(\\d+)\\])?;\\s*$");
            std::istringstream in(rebuilt);
            std::string kept, l;
            while (std::getline(in, l))
            {
                std::smatch dm;
                int64_t off = 0;
                if (std::regex_match(l, dm, declRe) && dm[1].str().compare(0, 6, "return") != 0 && dm[1].str().compare(0, 4, "goto") != 0 &&
                    StackNameOffset(dm[2].str(), off))
                {
                    const std::string name = dm[2].str(), type = dm[1].str();
                    const bool array = dm[3].matched;
                    frameDefines += "#define " + name + (array ? " ((" : " (*(") + type + " *)(g_frame_base + 0x8000 + (" + std::to_string(off) + "))" +
                                    ")\n";
                    frameUndefs += "#undef " + name + "\n";
                    frameNames.insert(name);
                    continue;
                }
                kept += l + "\n";
            }
            rebuilt = kept;
        }
        // A name the engine makes for an access wider than the symbol it starts at (`_Stack_4 = CONCAT31(...)` where only `Stack_4`
        // and `xStack_1` are declared) is no declaration of its own. It is declared with the width of the write (A2-57): `CONCATab` gives a + b
        // bytes, and the engine's untyped name for that width (`xunknown4`: unsigned as C, signed as the engine reads it) is the type the
        // engine would have given it, so a signed compare of a four-byte one is a signed compare and the C++ reading can assign a `CONCAT31`
        // to it. A name no `CONCAT` writes (or one of a width with no such name) keeps the widest type, so the value written is the value read.
        {
            static const std::regex under("\\b(_[A-Za-z]\\w*)\\b");
            std::set<std::string> names;
            for (std::sregex_iterator it(rebuilt.begin(), rebuilt.end(), under), end; it != end; ++it) names.insert((*it)[1].str());
            const size_t brace = rebuilt.find("{\n");
            std::string decls;
            for (const std::string& n : names)
                if (n.compare(0, 2, "__") != 0 && frameNames.count(n) == 0) decls += "  " + UnderscoreNameType(rebuilt, n) + " " + n + ";\n";
            if (!decls.empty() && brace != std::string::npos) rebuilt.insert(brace + 2, decls);
        }
        pr.cText = frameDefines + rebuilt + frameUndefs;
        pr.frame = frameMode;
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
        // The run: the emulator from the entry for nIn inputs.
        Rng ir(inputSeed ^ (static_cast<uint64_t>(ci) * 0x9E3779B97F4A7C15ull) ^ c.words.size());
        const std::vector<uint32_t>& pool = PrintedCPool();
        const uint32_t poolSize = static_cast<uint32_t>(pool.size());
        int sweep = 0;   // the first poolSize inputs walk the edge values through every register (r4 takes pool[k], r5 pool[k + 5] ...)
        int byteSweep = 0;   // ...and the next 44 share one byte with a register a function may compare them with (A2-44)
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
        uint32_t baseIn[13] = {};   // the first row's inputs (area 2 round 11: SE_GENERATED_PRINTEDC_SWEEP16 varies r4 from it)
        bool r0Changed = false;
        // Area 2 round 11 instrument: SE_GENERATED_PRINTEDC_EXTRA=<hex>,<hex>,... and SE_GENERATED_PRINTEDC_BYTES=1 add rows with r4 (and r5)
        // set to those values (the byte rows: every value 0..255 and its sign extension), after the rows above.
        static const std::vector<uint32_t> extraVals = [] {
            std::vector<uint32_t> e;
            if (const char* x = std::getenv("SE_GENERATED_PRINTEDC_EXTRA"))
            {
                std::string t(x);
                size_t at = 0;
                while (at < t.size())
                {
                    size_t c = t.find(',', at);
                    if (c == std::string::npos) c = t.size();
                    if (c > at) e.push_back(static_cast<uint32_t>(std::strtoull(t.substr(at, c - at).c_str(), nullptr, 16)));
                    at = c + 1;
                }
            }
            if (const char* b = std::getenv("SE_GENERATED_PRINTEDC_BYTES"))
                if (b[0] == '1')
                    for (uint32_t j = 0; j < 256; ++j)
                    {
                        e.push_back(j);
                        e.push_back(j >= 0x80 ? (0xFFFFFF00u | j) : j);
                    }
            return e;
        }();
        const int extraRows = static_cast<int>(extraVals.size()) * 2;
        for (int k = 0; k < nIn + 44 + extraRows; ++k)
        {
            uint32_t in[13];
            for (int i = 0; i < 8; ++i) in[i] = draw();
            in[8] = draw() & 0x3F3u;
            in[9] = draw();
            in[10] = draw();
            // GBR and VBR start as something other than 0 (A2-26: with 0 a text that dropped either, or read the wrong one, passed)
            in[11] = draw();
            in[12] = draw();
            if (sweep < static_cast<int>(poolSize))
            {
                for (int i = 4; i < 8; ++i) in[i] = pool[(sweep + 5 * (i - 4) + (i - 4) * (i - 4)) % poolSize];
                in[0] = pool[(sweep + 11) % poolSize];
                in[1] = pool[(sweep * 3 + 1) % poolSize];
                in[8] = (sweep & 1) ? (in[8] | 1u) : (in[8] & ~1u);   // T both ways
                ++sweep;
            }
            // A parameter that shares exactly one byte with a register the function may compare it with (cmp/str is an OR of four byte
            // compares, and a random 32-bit value matches a given byte with probability 1/256): the other three bytes are the register's
            // complement, so a text that compares the wrong byte of it or names the wrong one decides differently (A2-44).
            // The 44 inputs after the edge sweep walk every (register, byte) pair through every parameter register: register t = (j + 3 i) % 11
            // (r0-r3, r8-r14) and byte (j / 11 + i) % 4 for parameter r(4 + i) at input j.
            if (sweep >= static_cast<int>(poolSize) && byteSweep < 44)
            {
                for (int i = 0; i < 4; ++i)
                {
                    const uint32_t t = static_cast<uint32_t>(byteSweep + 3 * i) % 11, byte = (static_cast<uint32_t>(byteSweep) / 11 + i) % 4;
                    const uint32_t other = t < 4 ? in[t] : CalleeSaved(8 + (t - 4));
                    in[4 + i] = (~other & ~(0xFFu << (8 * byte))) | (other & (0xFFu << (8 * byte)));
                }
                ++byteSweep;
            }
            if (k >= nIn + 44)
            {
                const size_t xi = static_cast<size_t>(k - (nIn + 44)) / 2, n = extraVals.size();
                if ((k - (nIn + 44)) & 1) { in[5] = extraVals[xi]; in[4] = extraVals[(xi * 5 + 1) % n]; }
                else { in[4] = extraVals[xi]; in[5] = extraVals[(xi * 7 + 3) % n]; }
            }
            for (int i = 0; i < 8; ++i) machine->Set(("r" + std::to_string(i)).c_str(), in[i]);
            for (int i = 8; i < 15; ++i) machine->Set(("r" + std::to_string(i)).c_str(), CalleeSaved(i));
            machine->Set("r15", kStackTop);
            machine->Set("pr", kSentinel);
            machine->Set("sr", in[8]);
            machine->Set("gbr", in[11]);
            machine->Set("vbr", in[12]);
            machine->Set("mach", in[9]);
            machine->Set("macl", in[10]);
            RunOut run;
            env.trace.clear();
            if (!machine->Run(kCode, kSentinel, kStepLimit, run, env)) continue;
            if (run.fin[0] != in[0]) r0Changed = true;
            char buf[256];
            std::snprintf(buf, sizeof buf, "      { 0x%08Xu,0x%08Xu,0x%08Xu,0x%08Xu,0x%08Xu,0x%08Xu,0x%08Xu,0x%08Xu,0x%08Xu,0x%08Xu,0x%08Xu,0x%08Xu,0x%08Xu,0x%08Xu,0x%08Xu,0x%08Xu,0x%08Xu,0x%08Xu },\n",
                          in[0], in[1], in[2], in[3], in[4], in[5], in[6], in[7], in[8], in[9], in[10], run.fin[0], run.fin[1], run.finSr,
                          machine->Get("gbr"), machine->Get("vbr"), in[11], in[12]);
            rows << buf;
            if (kept == 0) std::memcpy(baseIn, in, sizeof baseIn);
            ++kept;
        }
        if (!kept)
        {
            v.cls = "skipped:emulator-unfinished";
            continue;
        }
        std::ostringstream m;
        m << "  { /* " << pr.name << " */\n    static const unsigned int D[][18] = {\n" << rows.str() << "    };\n    for (unsigned k = 0; k < " << kept << "; ++k) {\n";
        m << "      memcpy(g_in, D[k], sizeof g_in[0] * 11);\n      g_in[11] = D[k][16]; g_in[12] = D[k][17];\n";
        std::ostringstream call;
        call << pr.name << "(";
        for (int i = 0; i < nParams && i < 4; ++i) call << (i ? ", " : "") << "(int)g_in[" << 4 + i << "]";
        call << ")";
        // The control registers: the last SR, GBR, VBR written through Write_*, or what they were, against the run's.
        // (SR is compared in its S and interrupt-mask bits, which only ldc changes.)
        const std::string ctl = "      if ((g_wsr & 0xF2u) != (D[k][13] & 0xF2u) || g_wgbr != D[k][14] || g_wvbr != D[k][15]) { printf(\"MISMATCH " + pr.name +
            " control registers: pseudo sr=%08X gbr=%08X vbr=%08X, run sr=%08X gbr=%08X vbr=%08X\\n\", g_wsr, g_wgbr, g_wvbr, D[k][13], D[k][14], D[k][15]); ++bad_" +
            pr.name + "; }\n";
        m << "      g_wsr = g_in[8]; g_wgbr = g_in[11]; g_wvbr = g_in[12];\n";
        if (frameMode) m << "      memset((void *)(uintptr_t)g_frame_base, 0, 0x10000);\n";
        if (v.voidReturn) m << "      " << call.str() << ";\n" << ctl;
        else
        {
            m << "      unsigned long long g64 = (unsigned long long)" << call.str() << ";\n";
            m << "      int rsz = (int)sizeof(" << call.str() << ");\n";
            m << "      unsigned int rmask = rsz >= 4 ? 0xFFFFFFFFu : (1u << (8 * rsz)) - 1;\n";
            m << "      unsigned int got = rsz == 8 ? (unsigned int)(g64 >> 32) : (unsigned int)g64 & rmask;\n";
            m << "      unsigned int want = rsz == 8 ? D[k][11] : D[k][11] & rmask;\n";
            // A 1- or 2-byte return is r0's low bytes, and a caller reads it as the extension of them (a lent 'char f()'): r0's upper
            // bits must be the zero or the sign extension, or the text dropped part of r0 (A2-26: the low bytes alone agreed).
            m << "      int badext = 0;\n"
                 "      if (rsz < 4) { unsigned sh = 32 - 8 * rsz; unsigned sx = (unsigned)((int)(want << sh) >> sh); badext = D[k][11] != want && D[k][11] != sx; }\n";
            m << "      if (got != want || badext || (rsz == 8 && (unsigned int)g64 != D[k][12])) { printf(\"MISMATCH " << pr.name
              << " r4=%08X r5=%08X r6=%08X r7=%08X size=%d%s run=%08X:%08X pseudo=%08X:%08X\\n\", g_in[4], g_in[5], g_in[6], g_in[7], rsz, "
                 "(rsz == 8 && (unsigned int)g64 == D[k][12]) ? \" high-word-only\" : badext ? \" upper-bits\" : \"\", D[k][11], D[k][12], "
                 "got, rsz == 8 ? (unsigned int)g64 : 0u); ++bad_" << pr.name << "; }\n";
            m << ctl;
        }
        m << "    }\n";
        // Area 2 round 11 instrument (SE_GENERATED_PRINTEDC_SWEEP16=1): r4 = every value 0..65535, the other inputs those of the first row,
        // the run's r0 kept in compact tables and each value compared as the rows above are (the 16-bit domain of a function that reduces r4).
        static const bool sweep16 = std::getenv("SE_GENERATED_PRINTEDC_SWEEP16") != nullptr;
        if (sweep16 && !v.voidReturn && nParams >= 1)
        {
            std::ostringstream t0, t1, tk, tb;
            for (int i = 0; i < 13; ++i) tb << (i ? "," : "") << baseIn[i] << "u";
            for (uint32_t j = 0; j < 65536; ++j)
            {
                uint32_t in2[13];
                std::memcpy(in2, baseIn, sizeof in2);
                in2[4] = j;
                for (int i = 0; i < 8; ++i) machine->Set(("r" + std::to_string(i)).c_str(), in2[i]);
                for (int i = 8; i < 15; ++i) machine->Set(("r" + std::to_string(i)).c_str(), CalleeSaved(i));
                machine->Set("r15", kStackTop);
                machine->Set("pr", kSentinel);
                machine->Set("sr", in2[8]);
                machine->Set("gbr", in2[11]);
                machine->Set("vbr", in2[12]);
                machine->Set("mach", in2[9]);
                machine->Set("macl", in2[10]);
                RunOut run2;
                env.trace.clear();
                const bool ok = machine->Run(kCode, kSentinel, kStepLimit, run2, env);
                t0 << (ok ? run2.fin[0] : 0u) << (j + 1 < 65536 ? "," : "");
                t1 << (ok ? run2.fin[1] : 0u) << (j + 1 < 65536 ? "," : "");
                tk << (ok ? 1 : 0) << (j + 1 < 65536 ? "," : "");
            }
            m << "    { static const unsigned int B[13] = { " << tb.str() << " };\n      static const unsigned int S0[65536] = { " << t0.str() << " };\n"
              << "      static const unsigned int S1[65536] = { " << t1.str() << " };\n      static const unsigned int SK[65536] = { " << tk.str() << " };\n"
              << "      for (unsigned k = 0; k < 65536; ++k) {\n        if (!SK[k]) continue;\n"
              << "        memcpy(g_in, B, sizeof g_in[0] * 11);\n        g_in[4] = k; g_in[11] = B[11]; g_in[12] = B[12];\n"
              << "        g_wsr = g_in[8]; g_wgbr = g_in[11]; g_wvbr = g_in[12];\n";
            if (frameMode) m << "        memset((void *)(uintptr_t)g_frame_base, 0, 0x10000);\n";
            m << "        unsigned long long g64 = (unsigned long long)" << call.str() << ";\n"
              << "        int rsz = (int)sizeof(" << call.str() << ");\n"
              << "        unsigned int rmask = rsz >= 4 ? 0xFFFFFFFFu : (1u << (8 * rsz)) - 1;\n"
              << "        unsigned int got = rsz == 8 ? (unsigned int)(g64 >> 32) : (unsigned int)g64 & rmask;\n"
              << "        unsigned int want = rsz == 8 ? S0[k] : S0[k] & rmask;\n"
              << "        int badext = 0;\n"
              << "        if (rsz < 4) { unsigned sh = 32 - 8 * rsz; unsigned sx = (unsigned)((int)(want << sh) >> sh); badext = S0[k] != want && S0[k] != sx; }\n"
              << "        if (got != want || badext || (rsz == 8 && (unsigned int)g64 != S1[k])) { printf(\"MISMATCH " << pr.name
              << " r4=%08X r5=%08X r6=%08X r7=%08X size=%d%s run=%08X:%08X pseudo=%08X:%08X\\n\", g_in[4], g_in[5], g_in[6], g_in[7], rsz, "
                 "(rsz == 8 && (unsigned int)g64 == S1[k]) ? \" high-word-only\" : badext ? \" upper-bits\" : \"\", S0[k], S1[k], "
                 "got, rsz == 8 ? (unsigned int)g64 : 0u); ++bad_" << pr.name << "; }\n"
              << "      } }\n";
        }
        m << "  }\n";
        pr.mainText = m.str();
        pr.ready = true;
        v.compared = !v.voidReturn;
        v.detail = std::to_string(kept) + " inputs";
        // A `void` text over a run that changed r0: VoidReturnClass (A2-34).
        if (v.voidReturn && r0Changed)
        {
            v.cls = VoidReturnClass(r.prototype.known, c.words);
            if (v.cls == "return-dropped") v.detail = "the rts slot moves a register into r0 (the compiled `return Rm`), the text returns nothing and the signature is lent";
        }
    }
    // Compile and run.
    const std::string cxx = FindCxxCompiler(cc);
    auto compileRun = [&](const std::vector<size_t>& which, const std::string& tag, std::string& log, bool& compiled, bool engine = false) -> std::string {
        std::ostringstream src;
        src << (engine ? CxxHeader() : CHeader());
        for (size_t i : which) src << "static int bad_" << prep[i].name << ";\n" << (engine ? NegativeWideLiteralsTyped(UnsignedLiteralsTyped(prep[i].cText)) : prep[i].cText) << "\n";
        src << "int main(void) {\n  FrameInit();\n";
        for (size_t i : which) src << prep[i].mainText;
        int total = 0;
        (void)total;
        src << "  int bad = 0;\n";
        for (size_t i : which) src << "  bad += bad_" << prep[i].name << ";\n";
        src << "  printf(\"BAD %d\\n\", bad);\n  return 0;\n}\n";
        const std::string base = dts::Join(gScratch, "printedc_" + std::to_string(batchNo) + "_" + tag);
        const std::string cfile = base + (engine ? ".cpp" : ".c"), bin = base + ".bin", clog = base + ".log", rlog = base + ".out";
        {
            std::ofstream f(cfile);
            f << src.str();
        }
        const std::string compileCmd = engine ? cxx + " -O0 -fwrapv -w -std=gnu++14 -o " + bin + " " + cfile + " > " + clog + " 2>&1"
                                              : cc + " -O0 -fwrapv -fsigned-char -w -std=gnu11 -o " + bin + " " + cfile + " > " + clog + " 2>&1";
        compiled = std::system(compileCmd.c_str()) == 0;
        if (!compiled)
        {
            std::ifstream f(clog);
            std::stringstream ss;
            ss << f.rdbuf();
            log = ss.str();
            return std::string();
        }
        const int runStatus = std::system((bin + " > " + rlog + " 2>&1").c_str());
        std::ifstream f(rlog);
        std::stringstream ss;
        ss << f.rdbuf();
        // The program ends with "BAD <n>"; without it the run died (a signal) or never finished. The exit status is kept for the message only:
        // the marker is the verdict, since a program that printed it and then died in exit would still have compared every function.
        if (ss.str().find("\nBAD ") == std::string::npos && ss.str().compare(0, 4, "BAD ") != 0)
            compiled = false, log = "the compiled program did not finish (status " + std::to_string(runStatus) + "): " + ss.str().substr(0, 200);
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
                    // r0's upper bits that are no extension of a narrow return are not C's promotion: always a failure.
                    if (line.find(" upper-bits") != std::string::npos) v.cls = "mismatch", v.upperBits = true;
                    else v.cls = "mismatch";   // a second reading below may excuse it; none is assumed (A2-33)
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
    // The text is not always C (A2-19; DECOMPILER_GUIDE.md: read the types, not C's promotion rules): the engine prints arithmetic on a
    // value narrower than int at its own width (`(int4)-param_1` of a char negates a byte), a comparison or a `!` is a four-byte unsigned
    // in the arithmetic around it, `(uint8)x` of a signed x is a zero extension, and a value with no sign (xunknown) has none. A text that
    // disagrees with the run as C is read once more as the engine means it (CxxHeader: C++ classes that do the arithmetic at their own
    // width). If it agrees there it is counted apart ('narrow-c'); a text that agrees in neither reading is a finding, and so is one
    // that cannot be read (A2-33: every mismatch goes through here, none is excused by what its declarations look like).
    {
        std::vector<size_t> cand;
        for (size_t i : all)
            if (out[i].cls == "mismatch" && !out[i].upperBits) cand.push_back(i);
        if (!cand.empty() && cxx.empty())
        {
            for (size_t i : cand) out[i].detail += " [no C++ compiler for the engine's reading: set SE_CXX_COMPILER]";
        }
        else if (!cand.empty())
        {
            std::set<std::string> bad, unreadable;
            auto readBatch = [&](const std::vector<size_t>& which, const std::string& tag) {
                std::string altLog;
                bool altCompiled = false;
                const std::string altRun = compileRun(which, tag, altLog, altCompiled, true);
                if (!altCompiled)
                    return false;
                std::istringstream in(altRun);
                std::string line;
                while (std::getline(in, line))
                    if (line.compare(0, 9, "MISMATCH ") == 0) bad.insert(line.substr(9, line.find(' ', 9) - 9));
                return true;
            };
            if (!readBatch(cand, "engine"))
                for (size_t i : cand)   // one at a time: which text this reading cannot read
                    if (!readBatch({ i }, "engine_" + prep[i].name))
                    {
                        unreadable.insert(prep[i].name);
                        std::string log1;
                        bool ok1 = false;
                        compileRun({ i }, "engine_" + prep[i].name, log1, ok1, true);
                        out[i].detail += " [the engine's reading did not compile: " + log1.substr(0, 160) + "]";
                    }
            for (size_t i : cand)
                if (bad.count(prep[i].name) == 0 && unreadable.count(prep[i].name) == 0)
                {
                    out[i].cls = "narrow-c";
                    out[i].detail += " [agrees when read as the engine means it]";
                }
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
    // Area 2, round 8. A2-56 (generator seed 965717): a byte of SR read through `& 1` and whole (cmp/str), the two SUBPIECEs merged by CSE.
    { "A2-56 cmp/str on the SR byte after addc", { 0x346F, 0x0529, 0x340F, 0x0202, 0x262C, 0x8B01, 0x0502, 0x041A, 0x000B, 0x6053 } },
    // A2-57 (seed 968411): the engine's `_Stack_4 = CONCAT31(...)`, a name declared at the width of the write, read as a signed compare.
    { "A2-57 _Stack_4 written by CONCAT31", { 0x246B, 0x7FF8, 0x1F30, 0x1F41, 0x6053, 0x80F7, 0x6073, 0x80F2, 0x6013, 0x80F1, 0x55F1, 0x7F08, 0x350F, 0x312A, 0x000B, 0x6053 } },
    // A2-52 (the A2-47 family beyond patch 0046): sums of scaled terms over shared variables, `c + 8x + 4y + 14x` through `shlr2 ; shll2`.
    { "A2-52 c + 8x + 4y + 14x, shlr2 ; shll2", { 0xD008, 0xD309, 0x0347, 0x031A, 0x303C, 0xD308, 0x0367, 0x031A, 0x303C, 0xD307, 0x0347, 0x031A, 0x303C, 0x4009, 0x4008, 0x000B, 0x0009, 0x0009, 0x0000, 0x0100, 0x0000, 0x0008, 0x0000, 0x0004, 0x0000, 0x000E } },
    { "A2-52 termgen 2", { 0x2468, 0x0129, 0xD00A, 0x6343, 0x4308, 0x4308, 0x303C, 0x6313, 0x4308, 0x4308, 0x303C, 0xD307, 0x0367, 0x031A, 0x303C, 0xD306, 0x0347, 0x031A, 0x303C, 0xD305, 0x2039, 0x000B, 0x0009, 0x0009, 0x0000, 0x0000, 0x0000, 0x0008, 0xFFFF, 0xFFFA, 0xFFFF, 0xFFFC } },
    { "A2-52 termgen 4", { 0x3456, 0x0129, 0xD00A, 0xD30B, 0x0317, 0x031A, 0x3038, 0xD30A, 0x0367, 0x031A, 0x303C, 0xD309, 0x0347, 0x031A, 0x3038, 0xD308, 0x0317, 0x031A, 0x303C, 0xD307, 0x2039, 0x000B, 0x0009, 0x0009, 0x0000, 0x0000, 0x0000, 0x0048, 0x0000, 0x001C, 0x0000, 0x0024, 0x0000, 0x0001, 0xFFFF, 0xFFFC } },
    // Area 2, round 9. A2-58: a join of two equal computations (RulePushMulti) read at two widths, through an add and through a test.
    { "A2-58 add", { 0xE301, 0x4305, 0x4415, 0x8905, 0x6153, 0x2139, 0x6213, 0x7201, 0xA004, 0x0009, 0x6163, 0x2139, 0x6213, 0x7201, 0x6013, 0x600C, 0x202B, 0x000B, 0x0009, 0x0009 } },
    { "A2-58 tst", { 0xE301, 0x4305, 0x4411, 0x8904, 0x6153, 0x2139, 0x2118, 0xA003, 0x0009, 0x6163, 0x2139, 0x2118, 0x6013, 0x4008, 0x0229, 0x3028, 0x000B, 0x0009 } },
    // A2-59: loops that AND a seeded accumulator with the pieces of a register (the engine did not finish these: RuleAndDistribute and RuleHumptyOr).
    { "A2-59 swap.b", { 0xE104, 0x6328, 0x2139, 0x4610, 0x8BFB, 0x000B, 0x6013, 0x0009 } },
    { "A2-59 swap.w", { 0xE104, 0x6329, 0x2139, 0x4610, 0x8BFB, 0x000B, 0x6013, 0x0009 } },
    { "A2-59 shlr8 ; swap.b", { 0xE11C, 0xE601, 0x6323, 0x4319, 0x6338, 0x2139, 0x4610, 0x8BF9, 0x000B, 0x6013, 0x000B, 0x6033 } },
    { "A2-59 xtrct", { 0xE601, 0xE17F, 0x222B, 0x200D, 0x2109, 0x4610, 0x8BFA, 0x4308, 0x6023, 0x000B, 0x0009, 0x0009 } },
    // A2-60 (oracle): `0x7f < param_1 + 0x80U` read as the engine means it, an unsigned compare (the C reading widens the two-byte negation: narrow-c).
    { "A2-60 0x80U in the engine's reading", { 0x6043, 0x6153, 0x6263, 0x6373, 0x4404, 0x4421, 0x323C, 0x651B, 0x4421, 0xD104, 0x301C, 0x3012, 0x0129, 0x321C, 0x6053, 0x600F, 0x000B, 0x0009, 0x0000, 0x0080 } },
    // A3-65: a copy of r15 walked down by `mov.l Rm,@-Rn` over a local (`*--p = v`). The engine took a pointer into the frame to reach only what lies
    // at or above its base, so the local the walk wrote kept its first value (`return 5;`, the code returns 1), and a walk from the entry r15 itself
    // printed as the stack register (`register0x0000003c`). By value, in frame mode (the locals mapped onto memory, CheckPrintedC).
    { "A3-65 leaf: r15 + 8 walked down by longwords", { 0x7FF8, 0xE005, 0x2F02, 0x62F3, 0x7208, 0xE102, 0x2216, 0x4110, 0x8BFC, 0x60F2, 0x000B, 0x7F08 } },
    { "A3-65 walked down by bytes", { 0x7FF8, 0xE005, 0x2F00, 0x62F3, 0x7208, 0xE108, 0x2214, 0x4110, 0x8BFC, 0x60F0, 0x000B, 0x7F08 } },
    { "A3-65 walked down by words", { 0x7FF8, 0xE005, 0x2F01, 0x62F3, 0x7208, 0xE104, 0x2215, 0x4110, 0x8BFC, 0x60F1, 0x000B, 0x7F08 } },
    { "A3-65 from r15 + 4", { 0x7FF8, 0xE005, 0x2F02, 0x62F3, 0x7204, 0xE101, 0x2216, 0x4110, 0x8BFC, 0x60F2, 0x000B, 0x7F08 } },
    { "A3-65 from r15 + 8 of a 12-byte frame", { 0x7FF4, 0xE005, 0x2F02, 0x62F3, 0x7208, 0xE102, 0x2216, 0x4110, 0x8BFC, 0x60F2, 0x000B, 0x7F0C } },
    { "A3-65 from r15 + 12 of a 16-byte frame", { 0x7FF0, 0xE005, 0x2F02, 0x62F3, 0x720C, 0xE103, 0x2216, 0x4110, 0x8BFC, 0x60F2, 0x000B, 0x7F10 } },
    { "A3-65 written, then read down", { 0x7FF8, 0xE005, 0x2F02, 0xE007, 0x1F01, 0x62F3, 0x7208, 0xE102, 0xE300, 0x72FC, 0x6422, 0x334C, 0x4110, 0x8BFA, 0x6033, 0x000B, 0x7F08 } },
    { "A3-65 walked down, then read back up", { 0x7FF8, 0xE005, 0x2F02, 0x62F3, 0x7208, 0xE102, 0x2216, 0x4110, 0x8BFC, 0xE102, 0xE300, 0x6426, 0x334C, 0x4110, 0x8BFB, 0x6033, 0x000B, 0x7F08 } },
    // ...over a register's save slot its pop freed (`mov.l r13,@-r15 ; mov.l @r15+,r13` first): the slot was one variable with r13 (`return unaff_r13;`)
    { "A3-65 over a freed save slot", { 0x2FD6, 0x6DF6, 0x7FF8, 0x62F3, 0x7208, 0xE102, 0x2216, 0x4110, 0x8BFC, 0x50F1, 0x000B, 0x7F08 } },
    { "A3-65 over a freed save slot, a byte of it", { 0x2FD6, 0x6DF6, 0x7FF8, 0x62F3, 0x7208, 0xE102, 0x2216, 0x4110, 0x8BFC, 0x84F5, 0x000B, 0x7F08 } },
    // ...and the controls, walked up (`mov.l r1,@r2 ; add #4,r2`), which the engine always had right
    { "A3-65 control: walked up by longwords", { 0x7FF8, 0xE005, 0x2F02, 0x62F3, 0xE102, 0x2212, 0x7204, 0x4110, 0x8BFB, 0x60F2, 0x000B, 0x7F08 } },
    { "A3-65 control: walked up by bytes", { 0x7FF8, 0xE005, 0x2F00, 0x62F3, 0xE108, 0x2210, 0x7201, 0x4110, 0x8BFB, 0x60F0, 0x000B, 0x7F08 } },    // Area 2, round 12 P3 fix. A2-79 (oracle): `(uint8)CONCAT14((char)in_r1,in_r1)` is a zero extension in the engine's reading (narrow-c, not mismatch).
    { "A2-79 (uint8) of a 5-byte piece", { 0x265C, 0x0329, 0x8B06, 0x4300, 0x223A, 0x6013, 0x600F, 0x600C, 0x000B, 0x0009, 0x200B, 0x4301, 0x7180, 0x6033, 0x600E, 0x000B, 0x0009, 0x0009 } },
    // A2-80 (engine, found by dagen12.py family 52): RuleRightShiftAnd dropped the mask of `(sext(b) & 0x20) s>> 8` once the value was one byte (a shift by
    // the width: both sides of its test were 0), and the text read `return param_1 >> 8` (the sign of b) where the code computes 0.
    { "A2-80 exts.b ; and #0x20 ; shar x8", { 0x604E, 0xC920, 0x4021, 0x4021, 0x4021, 0x4021, 0x4021, 0x4021, 0x4021, 0x4021, 0x000B, 0x0009 } },
    { "A2-80 exts.b ; and #0x40 ; shar x9", { 0x604E, 0xC940, 0x4021, 0x4021, 0x4021, 0x4021, 0x4021, 0x4021, 0x4021, 0x4021, 0x4021, 0x000B, 0x0009 } },
    // A2-81 (engine, generator printed-C seed 2223957): RuleSLess2Zero took the high bit of a sum of two values that can both set it for the first of them:
    // `(uVar1 + 0x80000000) s< 0` of a masked sign was `uVar1 != 0`, its negation, so the subv's T and the GBR write after it were the wrong way round.
    { "A2-81 subv of a masked sign by 0x80000000, ldc r0,gbr unless T", { 0xE301, 0x4305, 0x2139, 0x313B, 0x8901, 0x401E, 0x0009, 0x000B, 0x0009 } },
    { "A2-81 subv of a masked sign, the flag moved", { 0xE301, 0x4305, 0x2139, 0x313B, 0x0029, 0x6003, 0x000B, 0x0009 } },
    { "A2-81 addv of a masked sign by 0x80000000", { 0xE301, 0x4305, 0x2139, 0x313F, 0x0029, 0x6003, 0x000B, 0x0009 } },
    { "A2-80 exts.b ; and #0x01 ; shar x12", { 0x604E, 0xC901, 0x4021, 0x4021, 0x4021, 0x4021, 0x4021, 0x4021, 0x4021, 0x4021, 0x4021, 0x4021, 0x4021, 0x4021, 0x000B, 0x0009 } },
    // A2-82 (engine, found by dagen13.py family 60): RuleDivOpt took a sign extension times a constant with a logical shift (and a zero extension with an arithmetic
    // one) for a division when only the width fitted, and a product that wraps its multiply for an exact one. The text divided where the code shifted the
    // bits of the product: (a) `(unsigned)((short)x * 65537) >> 16` read `(int4)x / 1 + (x >> 31)`; (b) `(x * 0x01000001 >> 24) & 0xff` read `x / 1`.
    { "A2-82 (a) extsw ; * 0x00010001 ; shlr16", { 0x614F, 0xD203, 0x0127, 0x001A, 0x4029, 0x000B, 0x0009, 0x0009, 0x0001, 0x0001 } },
    { "A2-82 (a) extsw ; * 0x00010001 ; shlr16 ; add r1,r0", { 0x614F, 0xD203, 0x0127, 0x001A, 0x4029, 0x301C, 0x000B, 0x0009, 0x0001, 0x0001 } },
    { "A2-82 (a) extsb ; * 0x8001 ; shlr16 ; shlr8", { 0x614E, 0xD203, 0x0127, 0x001A, 0x4029, 0x4019, 0x000B, 0x0009, 0x0000, 0x8001 } },
    { "A2-82 (a) extuw ; * 0x00010001 ; shar x16 (the zero extension, the arithmetic shift)", { 0x614D, 0xD20A, 0x0127, 0x001A, 0x4021, 0x4021, 0x4021, 0x4021, 0x4021, 0x4021, 0x4021, 0x4021, 0x4021, 0x4021, 0x4021, 0x4021, 0x4021, 0x4021, 0x4021, 0x4021, 0x000B, 0x0009, 0x0001, 0x0001 } },
    { "A2-82 (b) extuw ; * 0x01000001 ; >> 24 ; and #0xff", { 0x614D, 0xD204, 0x0127, 0x001A, 0x4029, 0x4019, 0xC9FF, 0x000B, 0x0009, 0x0009, 0x0100, 0x0001 } },
    { "A2-82 (b) extub ; * 0x2AAAAAAB ; >> 31 (x / 3 only when the product does not wrap)", { 0x614C, 0xD206, 0x0127, 0x001A, 0x6303, 0x6033, 0x4029, 0x4019, 0x4009, 0x4009, 0x4009, 0x4001, 0x000B, 0x0009, 0x2AAA, 0xAAAB } },
    // A2-89 (engine, found by the fixer's in-situ check): RuleDivTermAdd took `(mulhu(x, c) + x) >> 16` of an unsigned x for the exact sum and the division by 0xd20b, but the add
    // keeps the low 32 bits of the sum, which is 33 bits wide for a large x: x = 0xffffffff gave 0x3803 in the code and 0x13803 in the text.
    { "A2-89 dmulu.l ; sts mach ; add ; shlr16, the sum wraps", { 0xD203, 0x3425, 0x000A, 0x304C, 0x4029, 0x000B, 0x0009, 0x0009, 0x3803, 0x2949 } },
    { "A2-89 control: the same with a multiplier whose sum does not wrap", { 0xD203, 0x3425, 0x000A, 0x304C, 0x4029, 0x000B, 0x0009, 0x0009, 0x0003, 0x2949 } },
    // ...and the division it must still make: extuw ; * 0xAAAB ; >> 17 is x / 3
    { "A2-82 control: extuw ; * 0xAAAB ; >> 17 is x / 3", { 0x614D, 0xD203, 0x0127, 0x001A, 0x4029, 0x4001, 0x000B, 0x0009, 0x0000, 0xAAAB } },
};

// The fixtures of area 2, round 4 (A2-21, A2-22, A2-26): the compiler's division by a constant, scaled by the power of two it
// was shifted by, and a byte or word put into a stacked longword; with their neighbours that were always right.
std::vector<CCase> WidePrintedCFixtures()
{
    std::vector<CCase> out;
    auto words = [](const RegItem& item) {
        RegFn f;
        f.items.push_back(item);
        f.slot = 0x0009;
        return f.Words();
    };
    // x / d * 2^k for the seven unsigned divisors the review found wrong (the scale is the last shift), x in r4, L = r1, T = r0
    for (uint32_t d : { 7u, 14u, 19u, 21u, 27u, 28u, 31u })
        out.push_back({ ("A2-21 u/" + std::to_string(d) + "*2^k (the last shift)"), words(DivisionItem(d, false, -1, 4, 1, 0, 2, true)) });
    // ...and their neighbours, right before and after: the quotient alone, the scale not the last shift, signed, and other divisors
    for (uint32_t d : { 7u, 14u, 19u, 31u })
        out.push_back({ ("A2-21 control u/" + std::to_string(d)), words(DivisionItem(d, false, 0, 4, 1, 0, 2, true)) });
    out.push_back({ "A2-21 control u/7*2", words(DivisionItem(7, false, 1, 4, 1, 0, 2, true)) });
    out.push_back({ "A2-21 control u/7*8", words(DivisionItem(7, false, 3, 4, 1, 0, 2, true)) });
    out.push_back({ "A2-21 control u/7<<8", words(DivisionItem(7, false, 8, 4, 1, 0, 2, true)) });
    out.push_back({ "A2-21 control u/19*4", words(DivisionItem(19, false, 2, 4, 1, 0, 2, true)) });
    out.push_back({ "A2-21 control u/3*2", words(DivisionItem(3, false, -1, 4, 1, 0, 2, true)) });
    out.push_back({ "A2-21 control u/10*2", words(DivisionItem(10, false, -1, 4, 1, 0, 2, true)) });
    out.push_back({ "A2-21 control u/641", words(DivisionItem(641, false, 0, 4, 1, 0, 2, true)) });
    out.push_back({ "A2-21 control u/6700417*4", words(DivisionItem(6700417, false, -1, 4, 1, 0, 2, true)) });
    for (uint32_t d : { 7u, 19u, 100u })
    {
        out.push_back({ ("A2-21 control s/" + std::to_string(d)), words(DivisionItem(d, true, 0, 4, 1, 0, 2, true)) });
        out.push_back({ ("A2-21 control s/" + std::to_string(d) + "*2^k"), words(DivisionItem(d, true, -1, 4, 1, 0, 2, true)) });
    }
    out.push_back({ "A2-21 control s/-7", words(DivisionItem(0u - 7u, true, 0, 4, 1, 0, 2, true)) });
    // the ledger's own words: the add form (x + t) >> 2 with no scale, and RuleDivTermAdd, unsigned and signed
    out.push_back({ "A2-21 u/7*4, the ledger's words", { 0xD104, 0x3415, 0x000A, 0x3408, 0x4401, 0x340C, 0x4409, 0x4408, 0x000B, 0x6043, 0x2492, 0x4925 } });
    out.push_back({ "A2-21 (x + t) >> 2", { 0xD104, 0x3415, 0x000A, 0x3408, 0x4401, 0x340C, 0x4401, 0x000B, 0x6043, 0x0009, 0x2492, 0x4925 } });
    out.push_back({ "A2-21 RuleDivTermAdd, unsigned", { 0xD102, 0x3415, 0x000A, 0x4001, 0x000B, 0x304C, 0x1234, 0x5678 } });
    out.push_back({ "A2-21 RuleDivTermAdd, signed", { 0xD102, 0x314D, 0x000A, 0x4021, 0x000B, 0x304C, 0x1234, 0x5678 } });
    // A2-26: GBR and VBR start as something other than 0, so a text that drops or mistakes them differs from the run
    out.push_back({ "A2-26 GBR advanced", { 0x0112, 0x7104, 0x411E, 0x000B, 0x0009 } });      // stc gbr,r1 ; add #4,r1 ; ldc r1,gbr
    out.push_back({ "A2-26 VBR advanced", { 0x0122, 0x7108, 0x412E, 0x000B, 0x0009 } });      // stc vbr,r1 ; add #8,r1 ; ldc r1,vbr
    out.push_back({ "A2-26 GBR from VBR", { 0x0122, 0x411E, 0x000B, 0x0009 } });             // stc vbr,r1 ; ldc r1,gbr
    // A2-25: cmp/str on a register that is not a parameter: its four bytes are inputs named with the byte they are
    out.push_back({ "A2-25 cmp/str r6,r3", { 0x236C, 0x000B, 0x0029 } });
    out.push_back({ "A2-25 cmp/str r2,r3 (neither a parameter)", { 0x232C, 0x000B, 0x0029 } });
    // A2-22: a byte or a word put into a stacked longword and the longword loaded back
    out.push_back({ "A2-22 byte at 3", { 0x7FFC, 0x2F42, 0x6053, 0x80F3, 0x60F2, 0x000B, 0x7F04 } });
    out.push_back({ "A2-22 byte at 2", { 0x6053, 0x2F46, 0x80F2, 0x60F6, 0x000B, 0x0009 } });
    out.push_back({ "A2-22 byte at 1", { 0x6053, 0x2F46, 0x80F1, 0x60F6, 0x000B, 0x0009 } });
    out.push_back({ "A2-22 word at 2", { 0x6053, 0x2F46, 0x81F1, 0x60F6, 0x000B, 0x0009 } });
    out.push_back({ "A2-22 byte at 3, copied to r1", { 0x6053, 0x2F46, 0x80F3, 0x60F6, 0x000B, 0x6103 } });
    out.push_back({ "A2-22 control: byte at 0", { 0x6053, 0x2F46, 0x80F0, 0x60F6, 0x000B, 0x0009 } });
    out.push_back({ "A2-22 control: an operation after the load", { 0x6053, 0x2F46, 0x80F3, 0x60F6, 0x7001, 0x000B, 0x0009 } });
    out.push_back({ "A2-22 control: through registers", { 0x6043, 0x4019, 0x4018, 0x615C, 0x201B, 0x000B, 0x0009 } });
    // Round 5. A2-32: x / 0xFFFFFFFE, 0xFFFFFFFC and 0xFFFFFFF0 (the add form, a total shift of 64) printed `return 0;`; the scaled form is seed 1900's
    for (uint16_t multiplier : { uint16_t(3), uint16_t(5), uint16_t(0x11) })
        out.push_back({ "A2-32 x / 0xFFFFFFF" + std::string(multiplier == 3 ? "E" : multiplier == 5 ? "C" : "0"),
                        { 0xD106, 0x3415, 0x000A, 0x3408, 0x4401, 0x340C, 0x4429, 0x4419, 0x4409, 0x4409, 0x4409, 0x4401, 0x000B, 0x6043, 0x0000, multiplier } });
    out.push_back({ "A2-32 x / 0xFFFFFFFE * 4 (seed 1900)", { 0xD307, 0x3535, 0x000A, 0x3508, 0x4501, 0x350C, 0x4529, 0x4519, 0x4509, 0x4509, 0x4509, 0x4501, 0x4508, 0x6053, 0x000B, 0x0009, 0x0000, 0x0003 } });
    // A2-33: the two functions of seeds 3404 and 9223 that were A2-32's wrong value counted as `narrow-c` for the declarations and a `(uint8)` in the text
    out.push_back({ "A2-33 seed 3404 #0", { 0x0537, 0x317A, 0x0018, 0x4700, 0x4408, 0x8B00, 0x673F, 0x3358, 0x0577, 0x4200, 0xD307, 0x3135, 0x020A, 0x3128, 0x4101, 0x312C, 0x4129,
                                             0x4119, 0x4109, 0x4109, 0x4109, 0x4101, 0x6013, 0x000B, 0x6073, 0x0009, 0x0000, 0x0003 } });
    out.push_back({ "A2-33 seed 9223 #0", { 0x4120, 0xD60C, 0x3065, 0x050A, 0x3058, 0x4001, 0x305C, 0x4029, 0x4019, 0x4009, 0x4009, 0x4009, 0x4001, 0x4008, 0x4008, 0x6003, 0x4004,
                                             0x8B00, 0x462E, 0xCB02, 0x8901, 0x0412, 0x461A, 0x000B, 0x356A, 0x0009, 0x0000, 0x0003 } });
    // A2-39: a bool minus one shifted right, with no narrow declaration (the engine's four-byte unsigned subtraction); a field assignment the printer wraps
    out.push_back({ "A2-39 a: (bool) - 1 >> 16", { 0xCB6D, 0x602B, 0x400A, 0x0607, 0x4205, 0x2247, 0x0129, 0x2229, 0x3277, 0x4415, 0x4209, 0x4110, 0x677A, 0x337E, 0x6609, 0x4010,
                                                    0x2258, 0x020A, 0x3554, 0xD202, 0x4129, 0x000B, 0x8802, 0x0009, 0x8000, 0x0000 } });
    out.push_back({ "A2-39 b: a wrapped field assignment", { 0x6043, 0x6153, 0x6263, 0x6373, 0x214B, 0x324B, 0x8F02, 0x603C, 0x610A, 0x4320, 0x7300, 0x8D01, 0x323A, 0x4719, 0x636B,
                                                              0x201B, 0x7FFC, 0x1F20, 0x6053, 0x81F0, 0x6053, 0x80F1, 0x57F0, 0x3437, 0x7F04, 0x000B, 0x6073 } });
    // A2-37: the bytes of r8 and r14, which a function keeps, named with the byte they are, given their values by the run
    out.push_back({ "A2-37 cmp/str r6,r8", { 0x286C, 0x000B, 0x0029 } });
    out.push_back({ "A2-37 cmp/str r6,r14", { 0x2E6C, 0x000B, 0x0029 } });
    // A2-34: `rts ; mov r1,r0` returns r1, an input no signature passes: the engine prints `void` and the signature is withheld (A3-5). A lent
    // `void` over this slot is `return-dropped` and fails; withheld, it is counted as `void-return`.
    out.push_back({ "A2-34 rts ; mov r1,r0 (r1 on entry)", { 0x000B, 0x6013 } });
    // A2-38: bra over two instructions with a slot (the item that replaced braf in the grammar), and a bt/s with a slot
    out.push_back({ "A2-38 mov #2,r1 ; bra +1 ; neg r4,r7 ; add #1,r5 ; mov r5,r0", { 0xE102, 0xA001, 0x674B, 0x7501, 0x6053, 0x000B, 0x0009 } });
    return out;
}

// The walk pointer kept in a stack local and read back (A3-80, patch 0060): a frame of F bytes, the slot read back written with a constant
// first, a copy of r15 + N stored to a longword of the frame (`mov.l Rp,@(4*S,r15)`), half the time a call to a leaf that does nothing
// (`bsr`, the function then saves PR), the pointer read back (`mov.l @(4*S,r15),Rp`), sometimes moved down by a slot, and walked down a
// counted number of slots storing the counter or a constant stepped each pass, one slot read back into r0. Engine patches 0056 and 0057
// follow the registers only: the read-back pointer's stores were taken not to reach the stack below it, and the slot printed its first
// constant (`return 5;` where the code returns 1), lent when there was no call. The run of every such function is deterministic (no input
// is read), so a text whose returns are all constants is checked against it (CheckSpillWalks). Drawn on a stream of its own, one seed in five.
bool GenSpillWalkFn(uint64_t seed, CCase& c, std::string& shape)
{
    Rng rng(seed * 3266489917u + 0xA380u);
    if (!rng.Chance(20)) return false;
    static const int kWidths[3] = { 1, 2, 4 };
    const int w = kWidths[rng.Below(3)];
    const int sz = w == 1 ? 0 : w == 2 ? 1 : 2;
    const int frame = 8 + 4 * static_cast<int>(rng.Below(w == 4 ? 4 : 3));   // 8-20 / 8-16 bytes: the displacements reach
    const int slots = frame / w;
    const bool call = rng.Chance(50);
    uint32_t regs[7] = { 1, 2, 3, 4, 5, 6, 7 };
    for (int i = 6; i > 0; --i) std::swap(regs[i], regs[rng.Below(static_cast<uint32_t>(i + 1))]);
    const uint32_t p = regs[0], ctr = regs[1];
    const uint32_t v = rng.Chance(33) ? ctr : regs[2];
    const int start = 1 + static_cast<int>(rng.Below(static_cast<uint32_t>(slots)));   // the slot the read-back pointer starts at (units of w)
    const bool lower = start > 1 && rng.Chance(25);                                     // read back, then moved down a slot before the walk
    const int from = lower ? start - 1 : start;
    const int count = 1 + static_cast<int>(rng.Below(static_cast<uint32_t>(from)));
    const int lowest = from - count, highest = from - 1;
    // the slot read back: one the walk writes three times in four (its constant is then stale), and never in the spilled longword
    int spill = static_cast<int>(rng.Below(static_cast<uint32_t>(frame / 4)));
    int back = rng.Chance(75) ? lowest + static_cast<int>(rng.Below(static_cast<uint32_t>(count))) : static_cast<int>(rng.Below(static_cast<uint32_t>(slots)));
    if ((back * w) / 4 == spill) spill = (spill + 1) % (frame / 4);
    const int k = 1 + static_cast<int>(rng.Below(100));   // the constant written first: 1-100, never a value the walk stores (<= count, or + 3k)
    const int first = 100 + k;
    c.words.clear();
    if (call) c.words.push_back(0x4F22);                                                       // sts.l pr,@-r15
    c.words.push_back(static_cast<uint16_t>(0x7F00 | ((0x100 - frame) & 0xFF)));              // add #-F,r15
    c.words.push_back(static_cast<uint16_t>(0xE000 | (first & 0xFF)));                        // mov #first,r0 (101-127 or a negative byte)
    if (w == 4) c.words.push_back(static_cast<uint16_t>(0x1F00 | back));                      // mov.l r0,@(4*back,r15)
    else c.words.push_back(static_cast<uint16_t>((w == 2 ? 0x81F0 : 0x80F0) | back));         // mov.w / mov.b r0,@(disp,r15)
    c.words.push_back(static_cast<uint16_t>(0x6003 | p << 8 | 15 << 4));                      // mov r15,Rp
    c.words.push_back(static_cast<uint16_t>(0x7000 | p << 8 | (start * w)));                  // add #N,Rp
    c.words.push_back(static_cast<uint16_t>(0x1F00 | p << 4 | spill));                        // mov.l Rp,@(4*S,r15)
    size_t bsrAt = 0;
    if (call)
    {
        bsrAt = c.words.size();
        c.words.push_back(0xB000);                                                             // bsr leaf (patched below)
        c.words.push_back(0x0009);                                                             // nop
    }
    c.words.push_back(static_cast<uint16_t>(0x5000 | p << 8 | 15 << 4 | spill));              // mov.l @(4*S,r15),Rp
    if (lower) c.words.push_back(static_cast<uint16_t>(0x7000 | p << 8 | ((0x100 - w) & 0xFF)));   // add #-W,Rp
    c.words.push_back(static_cast<uint16_t>(0xE000 | ctr << 8 | count));                      // mov #count,Rc
    if (v != ctr) c.words.push_back(static_cast<uint16_t>(0xE000 | v << 8 | 0x01));           // mov #1,Rv
    const size_t loop = c.words.size();
    c.words.push_back(static_cast<uint16_t>(0x2004 | p << 8 | v << 4 | sz));                  // L: mov.X Rv,@-Rp
    if (v != ctr) c.words.push_back(static_cast<uint16_t>(0x7000 | v << 8 | 0x03));           //    add #3,Rv
    c.words.push_back(static_cast<uint16_t>(0x4010 | ctr << 8));                              //    dt Rc
    c.words.push_back(static_cast<uint16_t>(0x8B00 | ((static_cast<int>(loop) - static_cast<int>(c.words.size()) - 2) & 0xFF)));   // bf L
    if (w == 4) c.words.push_back(static_cast<uint16_t>(0x50F0 | back));                      // mov.l @(4*back,r15),r0
    else c.words.push_back(static_cast<uint16_t>((w == 2 ? 0x85F0 : 0x84F0) | back));         // mov.w / mov.b @(disp,r15),r0
    c.words.push_back(static_cast<uint16_t>(0x7F00 | frame));                                 // add #F,r15
    if (call) c.words.push_back(0x4F26);                                                       // lds.l @r15+,pr
    c.words.push_back(0x000B);                                                                 // rts
    c.words.push_back(0x0009);                                                                 // nop
    if (call)
    {
        const size_t leaf = c.words.size();
        c.words.push_back(0x000B);                                                             // leaf: rts
        c.words.push_back(0x0009);                                                             //       nop
        c.words[bsrAt] = static_cast<uint16_t>(0xB000 | ((static_cast<int>(leaf) - static_cast<int>(bsrAt) - 2) & 0xFFF));
    }
    shape = std::string("spill.") + (call ? "call." : "nocall.") + (w == 1 ? "b" : w == 2 ? "w" : "l") + (lower ? ".lowered" : "") +
            (back >= lowest && back <= highest ? "" : ".control");
    c.name = "seed" + std::to_string(seed) + "_spill";
    return true;
}

// The check of the spill walks (A3-80): decompile, run once, and compare every constant return with the run's r0. A text that returns a local
// (the fixed engine's `return xStack_1c;`) is counted, not compared: the evaluator has no loops, and the printed-C harness cannot run a pointer kept
// in memory (the host's are 8 bytes). The class this catches is the stale constant, a local folded to the value it had before the walk.
std::vector<CVerdict> CheckSpillWalks(Sh2Decompiler& d, const std::vector<CCase>& cases)
{
    std::vector<CVerdict> out(cases.size());
    static const std::regex ret("\\breturn\\b\\s*([^;]*);");
    static const std::regex literal("^\\(?\\s*(?:\\(\\s*\\w+\\s*\\)\\s*)?(-?(?:0x[0-9a-fA-F]+|\\d+))\\s*\\)?$");
    for (size_t ci = 0; ci < cases.size(); ++ci)
    {
        const CCase& c = cases[ci];
        CVerdict& v = out[ci];
        std::vector<uint8_t> hw;
        for (size_t i = 0; i < c.words.size(); ++i) dts::Put16(hw, kCode - 0x06000000u + 2 * i, c.words[i]);
        dts::Put16(hw, kSentinel - 0x06000000u, 0x0009);
        dts::Put16(hw, kSentinel - 0x06000000u + 2, 0x0009);
        auto snap = dts::MakeSnapshot(hw, 5000 + ci);
        DecompileRequest q;
        q.entry = kCode;
        const DecompileResult r = d.Decompile(snap, q);
        v.text = r.Text();
        if (r.status != DecompileStatus::Ok || r.unsupported || r.truncated)
        {
            v.cls = "undecompiled";
            continue;
        }
        Env env;
        env.ram = snap->regions[1].bytes;
        std::unique_ptr<Machine> machine(new Machine(snap, &env));
        for (int i = 0; i < 15; ++i) machine->Set(("r" + std::to_string(i)).c_str(), i < 8 ? 0x11111111u * static_cast<uint32_t>(i) : CalleeSaved(i));
        machine->Set("r15", kStackTop);
        machine->Set("pr", kSentinel);
        RunOut run;
        if (!machine->Run(kCode, kSentinel, kStepLimit, run, env))
        {
            v.cls = "generator:no-return";
            continue;
        }
        bool any = false, local = false;
        for (std::sregex_iterator it(v.text.begin(), v.text.end(), ret), e; it != e; ++it)
        {
            any = true;
            const std::string what = (*it)[1].str();
            std::smatch m;
            if (!std::regex_match(what, m, literal))
            {
                local = true;
                continue;
            }
            const std::string n = m[1].str();
            const bool neg = n[0] == '-';
            const uint32_t mag = static_cast<uint32_t>(std::strtoull(n.c_str() + (neg ? 1 : 0), nullptr, 0));
            const uint32_t value = neg ? 0u - mag : mag;
            v.compared = true;
            if (value != run.fin[0])
            {
                v.cls = "stale-constant";
                v.detail = "the text returns " + what + "; the code returns " + Hex(run.fin[0]);
            }
        }
        if (v.cls.empty() && (!any || local)) v.cls = any ? "spill:local" : "spill:no-return";
    }
    return out;
}


// ---- Round 11 fix instrument: laundered frame addresses, the walks below them, and the callees that make them (A3-82 to A3-88) ------------------
// The reviewer's walk fuzzers, as a generator. A frame of 32 bytes, the sentinel at r15 + 0 written first with a constant (101-127), a frame
// address p = r15 + 16 (four longwords above the sentinel), then one value-preserving laundering step of p (none, a mask, a swap pair, a
// negation pair, a shift pair, a rotate pair, an xor pair, a global round trip, a spill in halves reloaded whole, a spill reloaded by bytes),
// and a store below p that reaches the sentinel: a counted walk of four pre-decrement stores, or one store at a constant negative displacement.
//   self: all of it in one function, sometimes p joined with a global pointer first (A3-88; the run takes the frame path); the engine's case
//         (patch 0061's escape analysis, A3-85, A3-88).
//   call: p handed in r4 to a callee that makes the store below it -- directly, through a wrapper that calls it, through a thunk that jumps to
//         it, or through a thunk that lowers p by 16 on the way to a callee that stores at its pointer -- with the laundering step on the caller's
//         side (A3-84) or the callee's (A3-83); the callees are decompiled first and lent, deepest first (the host's case: A3-82 to A3-84).
// The run reads no input, so it is one value: a constant the text returns is checked against it (CheckLaunderWalks, `stale-constant`), as
// CheckSpillWalks does; a text that returns a local is counted. Code at kCode, the callee at kCode + 0x80, its callee at kCode + 0xC0, the
// literal pool at kCode + 0x100. Drawn on a stream of its own, one seed in four.
const uint32_t kLaunderGlobal = 0x0600F100u;   // the global of the round trip and of the join (ordinary work RAM, zero)
static const char* const kLaunderSteps[] = { "none", "mask", "swap2", "neg2", "shift2", "rot2", "xor2", "global", "halves", "bytes" };
struct LaunderBuilder
{
    std::vector<uint16_t> w;      // the whole blob
    std::vector<uint32_t> lits;   // literal values, at kCode + 0x100 + 4k
    size_t at = 0;                // the next word of the function being built
    void Put(uint16_t x)
    {
        if (w.size() <= at) w.resize(at + 1, 0x0009);
        w[at++] = x;
    }
    // mov.l @(disp,PC),Rn of a literal (the pool is within reach of every function here)
    void Lit(int n, uint32_t value)
    {
        size_t k = 0;
        while (k < lits.size() && lits[k] != value) ++k;
        if (k == lits.size()) lits.push_back(value);
        const uint32_t pc = kCode + 2u * static_cast<uint32_t>(at);
        const uint32_t ea = kCode + 0x100u + 4u * static_cast<uint32_t>(k);
        Put(static_cast<uint16_t>(0xD000 | n << 8 | ((ea - ((pc & ~3u) + 4u)) / 4u)));
    }
    std::vector<uint16_t> Finish()
    {
        std::vector<uint16_t> out = w;
        out.resize(0x80, 0x0009);   // through kCode + 0x100
        for (uint32_t v : lits)
        {
            out.push_back(static_cast<uint16_t>(v >> 16));
            out.push_back(static_cast<uint16_t>(v));
        }
        return out;
    }
};
// The laundering step on register p (r2 or r4), with scratch t (r3) and x (r5), r0 free; 'h' the frame offset of a scratch longword the store
// below p does not reach (for the spills).
void LaunderStep(LaunderBuilder& b, const std::string& step, int p, int t, int x, int h)
{
    if (step == "mask") { b.Put(static_cast<uint16_t>(0xE0FC | t << 8)); b.Put(static_cast<uint16_t>(0x2009 | p << 8 | t << 4)); }          // mov #-4,t ; and t,p
    else if (step == "swap2") { b.Put(static_cast<uint16_t>(0x6009 | p << 8 | p << 4)); b.Put(static_cast<uint16_t>(0x6009 | p << 8 | p << 4)); }
    else if (step == "neg2") { b.Put(static_cast<uint16_t>(0x600B | p << 8 | p << 4)); b.Put(static_cast<uint16_t>(0x600B | p << 8 | p << 4)); }
    else if (step == "shift2") { b.Put(static_cast<uint16_t>(0x4008 | p << 8)); b.Put(static_cast<uint16_t>(0x4009 | p << 8)); }         // shll2 ; shlr2
    else if (step == "rot2") { b.Put(static_cast<uint16_t>(0x4004 | p << 8)); b.Put(static_cast<uint16_t>(0x4005 | p << 8)); }           // rotl ; rotr
    else if (step == "xor2")
    {
        b.Put(static_cast<uint16_t>(0xE003 | t << 8));                                                                                    // mov #3,t
        b.Put(static_cast<uint16_t>(0x200A | p << 8 | t << 4));
        b.Put(static_cast<uint16_t>(0x200A | p << 8 | t << 4));
    }
    else if (step == "global")
    {
        b.Lit(t, kLaunderGlobal);
        b.Put(static_cast<uint16_t>(0x2002 | t << 8 | p << 4));   // mov.l p,@t
        b.Put(static_cast<uint16_t>(0x6002 | p << 8 | t << 4));   // mov.l @t,p
    }
    else if (step == "halves")
    {
        b.Put(static_cast<uint16_t>(0x6003 | 0 << 8 | p << 4));            // mov p,r0
        b.Put(static_cast<uint16_t>(0x81F0 | ((h + 2) / 2)));              // mov.w r0,@(h+2,r15): the low half
        b.Put(static_cast<uint16_t>(0x4029));                              // shlr16 r0
        b.Put(static_cast<uint16_t>(0x81F0 | (h / 2)));                    // mov.w r0,@(h,r15): the high half
        b.Put(static_cast<uint16_t>(0x5000 | p << 8 | 15 << 4 | (h / 4))); // mov.l @(h,r15),p
    }
    else if (step == "bytes")
    {
        b.Put(static_cast<uint16_t>(0x1F00 | p << 4 | (h / 4)));           // mov.l p,@(h,r15)
        b.Put(static_cast<uint16_t>(0x6003 | t << 8 | 15 << 4));           // mov r15,t
        b.Put(static_cast<uint16_t>(0x7000 | t << 8 | h));                 // add #h,t
        b.Put(static_cast<uint16_t>(0x6004 | 0 << 8 | t << 4));            // mov.b @t+,r0
        b.Put(0x600C);                                                     // extu.b r0,r0
        for (int i = 0; i < 3; ++i)
        {
            b.Put(0x4018);                                                 // shll8 r0
            b.Put(static_cast<uint16_t>(0x6004 | x << 8 | t << 4));        // mov.b @t+,x
            b.Put(static_cast<uint16_t>(0x600C | x << 8 | x << 4));        // extu.b x,x
            b.Put(static_cast<uint16_t>(0x200B | 0 << 8 | x << 4));        // or x,r0
        }
        b.Put(static_cast<uint16_t>(0x6003 | p << 8 | 0 << 4));            // mov r0,p
    }
}
// The store below p that reaches p - 16 (the sentinel): four pre-decrement stores of the counter (the last is 1), or one store of 1 at p - 16.
void LaunderBelow(LaunderBuilder& b, int p, bool walk)
{
    if (walk)
    {
        b.Put(0xE104);                                                     // mov #4,r1
        const size_t loop = b.at;
        b.Put(static_cast<uint16_t>(0x2016 | p << 8));                     // L: mov.l r1,@-p
        b.Put(0x4110);                                                     //    dt r1
        b.Put(static_cast<uint16_t>(0x8B00 | ((static_cast<int>(loop) - static_cast<int>(b.at) - 2) & 0xFF)));   // bf L
    }
    else
    {
        b.Put(static_cast<uint16_t>(0x70F0 | p << 8));                     // add #-16,p
        b.Put(0xE101);                                                     // mov #1,r1
        b.Put(static_cast<uint16_t>(0x2012 | p << 8));                     // mov.l r1,@p
    }
}
bool GenLaunderWalkFn(uint64_t seed, CCase& c, std::string& shape)
{
    Rng rng(seed * 2246822519u + 0xA385u);
    if (!rng.Chance(25)) return false;
    const int K = 101 + static_cast<int>(rng.Below(27));
    const bool call = rng.Chance(60);
    const bool walk = rng.Chance(60);
    const std::string step = kLaunderSteps[rng.Below(10)];
    LaunderBuilder b;
    if (!call)
    {
        const bool join = rng.Chance(30);
        b.Put(0x2008);   // tst r0,r0   (r0 is 0 at the run: r7 = 1 picks the frame path of the join)
        b.Put(0x0729);   // movt r7
        b.Put(0x7FE0);   // add #-32,r15
        b.Put(static_cast<uint16_t>(0xE000 | K));   // mov #K,r0
        b.Put(0x2F02);   // mov.l r0,@r15
        b.Put(0x62F3);   // mov r15,r2
        b.Put(0x7210);   // add #16,r2
        LaunderStep(b, step, 2, 3, 5, 24);
        if (join)
        {
            b.Lit(6, kLaunderGlobal + 0x40u);
            b.Put(0x2778);   // tst r7,r7
            b.Put(0x8B00);   // bf S (over the next instruction)
            b.Put(0x6263);   // mov r6,r2
        }
        LaunderBelow(b, 2, walk);
        b.Put(0x60F2);   // mov.l @r15,r0
        b.Put(0x7F20);   // add #32,r15
        b.Put(0x000B);   // rts
        b.Put(0x0009);
        shape = std::string("launder.self.") + (walk ? "walk." : "disp.") + step + (join ? ".join" : "");
    }
    else
    {
        static const char* const kForms[] = { "direct", "wrapper", "thunk", "lowered" };
        const std::string form = kForms[rng.Below(4)];
        const bool callerSide = form != "lowered" && step != "none" && rng.Chance(40);
        // the caller
        b.Put(0x4F22);   // sts.l pr,@-r15
        b.Put(0x7FE0);   // add #-32,r15
        b.Put(static_cast<uint16_t>(0xE000 | K));
        b.Put(0x2F02);   // mov.l r0,@r15
        b.Put(0x64F3);   // mov r15,r4
        b.Put(0x7410);   // add #16,r4
        if (callerSide) LaunderStep(b, step, 4, 3, 5, 24);
        b.Lit(1, kCode + 0x80u);
        b.Put(0x410B);   // jsr @r1
        b.Put(0x0009);
        b.Put(0x60F2);   // mov.l @r15,r0
        b.Put(0x7F20);   // add #32,r15
        b.Put(0x4F26);   // lds.l @r15+,pr
        b.Put(0x000B);
        b.Put(0x0009);
        // the callee at kCode + 0x80, its callee at kCode + 0xC0
        const std::string calleeStep = callerSide ? std::string("none") : step;
        auto body = [&](bool laundered) {
            const bool frame = laundered && (calleeStep == "halves" || calleeStep == "bytes");
            if (frame) b.Put(0x7FF8);   // add #-8,r15: a scratch word of the callee's own
            if (laundered) LaunderStep(b, calleeStep, 4, 3, 5, 0);
            if (frame) b.Put(0x7F08);
            LaunderBelow(b, 4, walk);
            b.Put(0x000B);
            b.Put(0x0009);
        };
        b.at = 0x40;
        if (form == "direct") body(true);
        else if (form == "wrapper")
        {
            b.Put(0x4F22);
            const bool frame = calleeStep == "halves" || calleeStep == "bytes";
            if (frame) b.Put(0x7FF8);
            LaunderStep(b, calleeStep, 4, 3, 5, 0);
            if (frame) b.Put(0x7F08);
            b.Lit(1, kCode + 0xC0u);
            b.Put(0x410B);   // jsr @r1
            b.Put(0x0009);
            b.Put(0x4F26);
            b.Put(0x000B);
            b.Put(0x0009);
            b.at = 0x60;
            body(false);
        }
        else if (form == "thunk")
        {
            const bool frame = calleeStep == "halves" || calleeStep == "bytes";
            if (frame) b.Put(0x7FF8);
            LaunderStep(b, calleeStep, 4, 3, 5, 0);
            if (frame) b.Put(0x7F08);
            b.Lit(1, kCode + 0xC0u);
            b.Put(0x412B);   // jmp @r1
            b.Put(0x0009);
            b.at = 0x60;
            body(false);
        }
        else   // lowered: the thunk lowers p by 16 in its jump's slot; the callee stores at its pointer
        {
            if (calleeStep != "halves" && calleeStep != "bytes") LaunderStep(b, calleeStep, 4, 3, 5, 0);
            b.Lit(1, kCode + 0xC0u);
            b.Put(0x412B);   // jmp @r1
            b.Put(0x74F0);   // add #-16,r4
            b.at = 0x60;
            b.Put(0xE101);   // mov #1,r1
            b.Put(0x2412);   // mov.l r1,@r4
            b.Put(0x000B);
            b.Put(0x0009);
        }
        shape = std::string("launder.call.") + form + "." + (walk ? "walk." : "disp.") + step + (callerSide ? ".caller" : "");
    }
    c.words = b.Finish();
    c.name = "seed" + std::to_string(seed) + "_launder";
    return true;
}

// ---- Round 12 fix instrument: a frame address that goes out of the function and comes back by a way that is no op of it (A3-89 to A3-94) ----------
// The shapes the round 12 review found the escape analysis open to, drawn on a stream of their own (one seed in four), with the same frame,
// sentinel and store below p = r15 + 16 as the laundered walks, and checked the same way (CheckLaunderWalks: a constant return against the run).
// A callee that is "unlent" reads r3 on entry (a hidden input no signature passes), so it is withheld and the caller's text is the probe's.
//   keep.global.<lent|unlent>.<walk|ret>: g stores r4 to a global; h loads it and stores below it (walk), or returns it in r0 and the caller
//        does (ret) (A3-89 a, b, d)
//   keep.arg2.<lent|unlent>: g stores its second argument (p) through its first (a slot of the caller's frame); the caller reloads it (A3-89 c)
//   keep.outgoing: an unlent g writes p to its own r15 + 4, the caller's outgoing word, which the caller reloads (A3-89 e)
//   back.<r1|r9|r10|mach|gbr>: a lent g moves its argument into another register (`mov r4,r9`, `lds r4,mach`, `ldc r4,gbr`); the caller
//        reads that and stores below it (A3-91)
//   chain.<r1|r4>.<call|jump|callback>[.counted]: P hands p in r1 or r4 to an unlent U1 that passes it on in r4 to an unlent U2 (by a call, a
//        tail jump, or a call through a pointer in r5) that stores below it (A3-90; r1 is no argument the engine shows); counted: U2's walk
//        counts from r3, which P sets, so no exact offset bounds it and only the probe's carry of U2's storesBelow up through U1 holds it
//   struct.<none|mask|swap2>: r2 = p, laundered, at a call to an unlent g that writes through r2 (A3-94); struct.stack.<step>: the address of
//        a local, laundered, in the first stack argument slot with r4-r7 unset (the engine drops it), to a lent g that writes through it
//   callerframe.<none|swap2|mask>: g writes the word at its entry r15 through a copy of r15, laundered (A3-92)
//   spare: f pushes r14, hands a swapped copy of r15 to a lent writer h (which overwrites the saved r14), pops r14; checked by f's claim, not
//        the caller's text (the caller of a withheld f keeps r14 by the model: A3-93)
//   rewrite.<load|ret|r9>.<lent|unlent>: g keeps p (in the global, or moves it to r9); after the call the caller writes the sentinel again and
//        stores below a pointer it did not compute: the global loaded, what h (handed nothing) returns, or r9 (patch 0063's unknown-pointer
//        rule: the call's own guard no longer stands for the local once it is written again)
//   neg.<not2|sub>, shift.<shll|shal>: a frame address through a negation the engine does not fold back (`not` twice across a loop's passes; `neg`
//        then a subtraction from 0), or `shll|shal ; shar` (A3-97: the review's mutations E3 and E4, inert before, are held by these)
static const char* const kEscapeNegShift[] = { "neg.not2", "neg.sub", "shift.shll", "shift.shal" };
static const char* const kEscapeForms[] = { "keep.global", "keep.arg2", "keep.outgoing", "back", "chain", "struct", "callerframe", "spare", "neg", "shift", "rewrite" };
bool GenEscapeWalkFn(uint64_t seed, CCase& c, std::string& shape)
{
    Rng rng(seed * 2654435761u + 0x51A7u);
    if (!rng.Chance(25)) return false;
    const int K = 101 + static_cast<int>(rng.Below(27));
    const std::string form = kEscapeForms[rng.Below(11)];
    const bool walk = rng.Chance(60);
    LaunderBuilder b;
    auto prologue = [&]() {
        b.Put(0x4F22);   // sts.l pr,@-r15
        b.Put(0x7FE0);   // add #-32,r15
        b.Put(static_cast<uint16_t>(0xE000 | K));
        b.Put(0x2F02);   // mov.l r0,@r15: the sentinel
    };
    auto epilogue = [&](uint16_t read) {
        b.Put(read);     // mov.l @r15,r0 (or another word of the frame)
        b.Put(0x7F20);   // add #32,r15
        b.Put(0x4F26);   // lds.l @r15+,pr
        b.Put(0x000B);
        b.Put(0x0009);
    };
    auto call = [&](uint32_t off) {
        b.Lit(0, kCode + off);
        b.Put(0x400B);   // jsr @r0
        b.Put(0x0009);
    };
    auto hidden = [&]() { b.Put(0x2338); };   // tst r3,r3: reads r3 on entry, so the callee is withheld
    if (form == "keep.global")
    {
        const bool lent = rng.Chance(50), ret = rng.Chance(40);
        prologue();
        b.Put(0x64F3);   // mov r15,r4
        b.Put(0x7410);   // add #16,r4
        call(0x80);
        call(0xC0);
        if (ret)
        {
            b.Put(0x6203);   // mov r0,r2
            LaunderBelow(b, 2, walk);
        }
        epilogue(0x60F2);
        b.at = 0x40;     // g
        if (!lent) hidden();
        b.Lit(1, kLaunderGlobal);
        b.Put(0x000B);
        b.Put(0x2142);   // mov.l r4,@r1
        b.at = 0x60;     // h
        if (!lent) hidden();
        b.Lit(1, kLaunderGlobal);
        if (ret)
        {
            b.Put(0x000B);
            b.Put(0x6012);   // mov.l @r1,r0
        }
        else
        {
            b.Put(0x6212);   // mov.l @r1,r2
            LaunderBelow(b, 2, walk);
            b.Put(0x000B);
            b.Put(0x0009);
        }
        shape = std::string("escape.keep.global.") + (lent ? "lent." : "unlent.") + (ret ? "ret." : "") + (walk ? "walk" : "disp");
    }
    else if (form == "keep.arg2")
    {
        const bool lent = rng.Chance(50);
        prologue();
        b.Put(0x64F3);   // mov r15,r4
        b.Put(0x7414);   // add #20,r4: a slot of this frame
        b.Put(0x65F3);   // mov r15,r5
        b.Put(0x7510);   // add #16,r5: p
        call(0x80);
        b.Put(0x52F5);   // mov.l @(20,r15),r2
        LaunderBelow(b, 2, walk);
        epilogue(0x60F2);
        b.at = 0x40;
        if (!lent) hidden();
        b.Put(0x000B);
        b.Put(0x2452);   // mov.l r5,@r4
        shape = std::string("escape.keep.arg2.") + (lent ? "lent." : "unlent.") + (walk ? "walk" : "disp");
    }
    else if (form == "keep.outgoing")
    {
        prologue();
        b.Put(0x64F3);
        b.Put(0x7410);
        call(0x80);
        b.Put(0x52F1);   // mov.l @(4,r15),r2
        LaunderBelow(b, 2, walk);
        epilogue(0x60F2);
        b.at = 0x40;
        hidden();
        b.Put(0x000B);
        b.Put(0x1F41);   // mov.l r4,@(4,r15): its own r15 + 4, the caller's outgoing word
        shape = std::string("escape.keep.outgoing.") + (walk ? "walk" : "disp");
    }
    else if (form == "back")
    {
        static const char* const kRegs[] = { "r1", "r9", "r10", "mach", "gbr" };
        static const uint16_t kMove[] = { 0x6143, 0x6943, 0x6A43, 0x440A, 0x441E };   // mov r4,r1 ; mov r4,r9 ; mov r4,r10 ; lds r4,mach ; ldc r4,gbr
        static const uint16_t kRead[] = { 0x6213, 0x6293, 0x62A3, 0x020A, 0x0212 };   // mov r1,r2 ; mov r9,r2 ; mov r10,r2 ; sts mach,r2 ; stc gbr,r2
        const int k = static_cast<int>(rng.Below(5));
        prologue();
        b.Put(0x64F3);
        b.Put(0x7410);
        call(0x80);
        b.Put(kRead[k]);
        LaunderBelow(b, 2, walk);
        epilogue(0x60F2);
        b.at = 0x40;
        b.Put(0x000B);
        b.Put(kMove[k]);
        shape = std::string("escape.back.") + kRegs[k] + "." + (walk ? "walk" : "disp");
    }
    else if (form == "chain")
    {
        static const char* const kHow[] = { "call", "jump", "callback" };
        const bool r1 = rng.Chance(50);
        const std::string how = kHow[rng.Below(3)];
        const bool counted = rng.Chance(50);   // U2 counts its walk from r3, which P sets: a walk no exact offset bounds
        prologue();
        b.Put(static_cast<uint16_t>(r1 ? 0x61F3 : 0x64F3));   // mov r15,r1 / r4
        b.Put(static_cast<uint16_t>(r1 ? 0x7110 : 0x7410));   // add #16
        if (counted) b.Put(0xE304);                             // mov #4,r3
        if (how == "callback") b.Lit(5, kCode + 0xC0u);        // U2's address, for U1 to call
        call(0x80);
        epilogue(0x60F2);
        b.at = 0x40;     // U1
        hidden();
        if (r1) b.Put(0x6413);   // mov r1,r4
        if (how == "jump")
        {
            b.Lit(0, kCode + 0xC0u);
            b.Put(0x402B);   // jmp @r0
            b.Put(0x0009);
        }
        else
        {
            b.Put(0x4F22);
            if (how == "call") b.Lit(0, kCode + 0xC0u);
            else b.Put(0x6053);   // mov r5,r0
            b.Put(0x400B);
            b.Put(0x0009);
            b.Put(0x4F26);
            b.Put(0x000B);
            b.Put(0x0009);
        }
        b.at = 0x60;     // U2
        hidden();
        if (counted)
        {
            b.Put(0x6133);   // mov r3,r1
            b.Put(0x2416);   // L: mov.l r1,@-r4
            b.Put(0x4110);   //    dt r1
            b.Put(0x8BFC);   //    bf L
        }
        else LaunderBelow(b, 4, walk);
        b.Put(0x000B);
        b.Put(0x0009);
        shape = std::string("escape.chain.") + (r1 ? "r1." : "r4.") + how + "." + (counted ? "counted" : walk ? "walk" : "disp");
    }
    else if (form == "struct")
    {
        static const char* const kSteps[] = { "none", "mask", "swap2" };
        const std::string step = kSteps[rng.Below(3)];
        if (rng.Chance(40))
        {
            // stack: p, laundered, in the call's first stack argument slot with r4-r7 unset, so the engine drops the word (A3-51) and only
            // NoteFrameEscapeOf lets the frame out; the lent g reads the word and writes through it
            b.Put(0x4F22);   // sts.l pr,@-r15
            b.Put(0x7FF8);   // add #-8,r15
            b.Put(static_cast<uint16_t>(0xE000 | K));
            b.Put(0x1F01);   // mov.l r0,@(4,r15): the word the callee writes
            b.Put(0x61F3);   // mov r15,r1
            b.Put(0x7104);   // add #4,r1
            LaunderStep(b, step, 1, 3, 5, 24);
            b.Put(0x2F12);   // mov.l r1,@r15: the first stack argument slot
            call(0x80);
            b.Put(0x50F1);   // mov.l @(4,r15),r0
            b.Put(0x7F08);   // add #8,r15
            b.Put(0x4F26);
            b.Put(0x000B);
            b.Put(0x0009);
            b.at = 0x40;
            b.Put(0x62F2);   // mov.l @r15,r2
            b.Put(0xE101);   // mov #1,r1
            b.Put(0x000B);
            b.Put(0x2212);   // mov.l r1,@r2
            shape = "escape.struct.stack." + step;
            c.words = b.Finish();
            c.name = "seed" + std::to_string(seed) + "_escape";
            return true;
        }
        prologue();
        b.Put(0x1F04);   // mov.l r0,@(16,r15): the word the callee writes
        b.Put(0x62F3);   // mov r15,r2
        b.Put(0x7210);   // add #16,r2
        LaunderStep(b, step, 2, 3, 5, 24);
        call(0x80);
        epilogue(0x50F4);   // mov.l @(16,r15),r0
        b.at = 0x40;
        hidden();
        b.Put(0xE101);   // mov #1,r1
        b.Put(0x000B);
        b.Put(0x2212);   // mov.l r1,@r2
        shape = "escape.struct." + step;
    }
    else if (form == "callerframe")
    {
        static const char* const kSteps[] = { "none", "swap2", "mask" };
        const std::string step = kSteps[rng.Below(3)];
        prologue();
        call(0x80);
        epilogue(0x60F2);
        b.at = 0x40;
        b.Put(0x61F3);   // mov r15,r1
        LaunderStep(b, step, 1, 3, 5, 0);
        b.Put(0xE201);   // mov #1,r2
        b.Put(0x2122);   // mov.l r2,@r1: the word at its entry r15, its caller's
        b.Put(0x000B);
        b.Put(0x0009);
        shape = "escape.callerframe." + step;
    }
    else if (form == "spare")
    {
        b.Put(0x4F22);   // sts.l pr,@-r15
        b.Put(static_cast<uint16_t>(0xEE00 | K));   // mov #K,r14
        call(0x80);
        b.Put(0x60E3);   // mov r14,r0
        b.Put(0x4F26);
        b.Put(0x000B);
        b.Put(0x0009);
        b.at = 0x40;     // f
        b.Put(0x4F22);
        b.Put(0x2FE6);   // mov.l r14,@-r15
        b.Put(0x64F3);   // mov r15,r4: the slot of the saved r14
        LaunderStep(b, "swap2", 4, 3, 5, 0);
        call(0xC0);
        b.Put(0x6EF6);   // mov.l @r15+,r14
        b.Put(0x4F26);
        b.Put(0x000B);
        b.Put(0x0009);
        b.at = 0x60;     // h
        b.Put(0xE201);   // mov #1,r2
        b.Put(0x000B);
        b.Put(0x2422);   // mov.l r2,@r4
        shape = "escape.spare";
    }
    else if (form == "rewrite")
    {
        static const char* const kVia[] = { "load", "ret", "r9" };
        const std::string via = kVia[rng.Below(3)];
        const bool lent = rng.Chance(50);
        prologue();
        b.Put(0x64F3);   // mov r15,r4
        b.Put(0x7410);   // add #16,r4: p
        call(0x80);      // g keeps p
        if (via == "ret")
        {
            b.Put(0xE400);   // mov #0,r4: h is handed nothing
            call(0xC0);
            b.Put(0x6203);   // mov r0,r2
        }
        b.Put(static_cast<uint16_t>(0xE300 | K));   // mov #K,r3
        b.Put(0x2F32);   // mov.l r3,@r15: the sentinel written again, after the call
        if (via == "load")
        {
            b.Lit(1, kLaunderGlobal);
            b.Put(0x6212);   // mov.l @r1,r2
        }
        else if (via == "r9") b.Put(0x6293);   // mov r9,r2
        LaunderBelow(b, 2, walk);
        epilogue(0x60F2);
        b.at = 0x40;     // g
        if (!lent) hidden();
        if (via == "r9")
        {
            b.Put(0x000B);
            b.Put(0x6943);   // mov r4,r9
        }
        else
        {
            b.Lit(1, kLaunderGlobal);
            b.Put(0x000B);
            b.Put(0x2142);   // mov.l r4,@r1
        }
        if (via == "ret")
        {
            b.at = 0x60;     // h
            if (!lent) hidden();
            b.Lit(1, kLaunderGlobal);
            b.Put(0x000B);
            b.Put(0x6012);   // mov.l @r1,r0
        }
        shape = "escape.rewrite." + via + "." + (lent ? "lent." : "unlent.") + (walk ? "walk" : "disp");
    }
    else   // neg, shift: a frame address through an op the engine keeps, then a store below it
    {
        const bool neg = form == "neg";
        const bool second = rng.Chance(50);
        prologue();
        b.Put(0x62F3);   // mov r15,r2
        b.Put(0x7210);   // add #16,r2
        if (neg && !second)
        {
            // not r2,r2 twice, one in each pass of a loop: the engine cannot fold the pair, so INT_NEGATE of a frame address reaches the walk
            // (the review's mutation E3 is held by this one)
            b.Put(0xE302);   // mov #2,r3
            b.Put(0x6227);   // L: not r2,r2
            b.Put(0x4310);   //    dt r3
            b.Put(0x8BFC);   //    bf L
        }
        else if (neg)
        {
            b.Put(0x632B);   // neg r2,r3
            b.Put(0xE200);   // mov #0,r2
            b.Put(0x3238);   // sub r3,r2
        }
        else if (!second)
        {
            b.Put(0x4200);   // shll r2
            b.Put(0x4221);   // shar r2
        }
        else
        {
            b.Put(0x4220);   // shal r2
            b.Put(0x4221);   // shar r2
        }
        LaunderBelow(b, 2, walk);
        epilogue(0x60F2);
        shape = std::string("escape.") + form + "." + (neg ? (second ? "sub" : "not2") : (second ? "shal" : "shll")) + "." + (walk ? "walk" : "disp");
    }
    c.words = b.Finish();
    c.name = "seed" + std::to_string(seed) + "_escape";
    return true;
}

// ---- Round 13 fix instrument (A3-114): the three shapes the round 13 review found open (A3-104 to A3-106), on a stream of their own (one seed in
// four), checked as the escape stream is (CheckLaunderWalks: a constant return against the run). The frame and sentinel are the escape
// stream's; the sentinel is also at r15 + 4, the word above the call's r15, and that is what is read back.
//   twoargs.<lent|unlent>.<walk|disp>: g(p, obj) keeps p = r15 + 20 in an object (`mov.l r4,@(4,r5)`), with the word at the call's r15 written
//        (the engine takes it for the call's fifth argument); the caller loads the kept pointer and stores below it, to r15 + 4 (A3-104)
//   stackkeep.<lent|unlent>[.r4r7].<walk|disp>: p = r15 + 20 in the call's first stack argument word with r4-r7 unset (the engine drops it);
//        g reads its incoming stack word and publishes it in a global; the caller loads the global and stores below it (A3-105); r4r7: p in
//        r4 as well and r7 set, in escfuzz's frame of four written locals (seed 303 i=469), which the engine prints with no argument at all
//        when g was decompiled first
//   reload.<swap2|mask|global>: g spills the address of its caller's outgoing word to a local and reloads it through a laundered copy of r15
//        (a swap pair, a mask) or through r15 published in a global, and writes through it; the caller reads its word back (A3-106)
bool GenEscape13Fn(uint64_t seed, CCase& c, std::string& shape)
{
    Rng rng(seed * 2654435789u + 0x13A3u);
    if (!rng.Chance(25)) return false;
    const int K = 101 + static_cast<int>(rng.Below(27));
    static const char* const kForms[] = { "twoargs", "stackkeep", "reload" };
    const std::string form = kForms[rng.Below(3)];
    const bool walk = rng.Chance(60);
    const bool lent = rng.Chance(50);
    // An unlent callee sits where CheckLaunderWalks does not decompile it first (kCode + 0xA0), as a callee the panel has not decompiled: a
    // callee decompiled first is withheld, and the caller's call is then shown with the callee's own reading of its arguments (r4 and r5),
    // so the word at the call's r15 is no argument and A3-104's boundary is not reached.
    const uint32_t gOff = lent ? 0x80u : 0xA0u;
    LaunderBuilder b;
    auto prologue = [&]() {
        b.Put(0x4F22);   // sts.l pr,@-r15
        b.Put(0x7FE0);   // add #-32,r15
        b.Put(static_cast<uint16_t>(0xE000 | K));
        b.Put(0x2F02);   // mov.l r0,@r15: the word at the call's r15
        b.Put(0x1F01);   // mov.l r0,@(4,r15): the sentinel read back
    };
    auto epilogue = [&](uint16_t read) {
        b.Put(read);
        b.Put(0x7F20);   // add #32,r15
        b.Put(0x4F26);   // lds.l @r15+,pr
        b.Put(0x000B);
        b.Put(0x0009);
    };
    auto call = [&](uint32_t off) {
        b.Lit(0, kCode + off);
        b.Put(0x400B);   // jsr @r0
        b.Put(0x0009);
    };
    auto hidden = [&]() { b.Put(0x2338); };   // tst r3,r3: reads r3 on entry, so the callee is withheld
    if (form == "twoargs")
    {
        prologue();
        b.Put(0x64F3);   // mov r15,r4
        b.Put(0x7414);   // add #20,r4: p
        b.Lit(5, kLaunderGlobal);   // obj
        call(gOff);
        b.Lit(2, kLaunderGlobal);
        b.Put(0x5221);   // mov.l @(4,r2),r2: the kept pointer
        LaunderBelow(b, 2, walk);
        epilogue(0x50F1);   // mov.l @(4,r15),r0
        b.at = gOff / 2;   // g
        if (!lent) hidden();
        b.Put(0x000B);
        b.Put(0x1541);   // mov.l r4,@(4,r5)
        shape = std::string("escape13.twoargs.") + (lent ? "lent." : "unlent.") + (walk ? "walk" : "disp");
    }
    else if (form == "stackkeep")
    {
        // r4r7: p in r4 too and r7 set, r5 and r6 unset (escfuzz seed 303 i=469): the engine prints the call with no argument at all
        const bool r4r7 = rng.Chance(50);
        if (r4r7)
        {
            // escfuzz's frame: four locals 1 to 4 at r15 + 0 to 12, p = r15 + 8 in r4 and in the stack word, r7 = 37; the store below p
            // reaches r15 + 4 (2 by the walk, 1 by the displacement), which is read back
            b.Put(0x4F22);   // sts.l pr,@-r15
            b.Put(0x7FF0);   // add #-16,r15
            for (uint16_t k = 0; k < 4; ++k)
            {
                b.Put(static_cast<uint16_t>(0xE001 + k));   // mov #k+1,r0
                b.Put(static_cast<uint16_t>(0x1F00 + k));   // mov.l r0,@(4k,r15)
            }
            b.Put(0x64F3);   // mov r15,r4
            b.Put(0x7408);   // add #8,r4: p
            b.Put(0xE725);   // mov #37,r7
            b.Put(0x7FFC);   // add #-4,r15
            b.Put(0x2F42);   // mov.l r4,@r15
            call(gOff);
            b.Put(0x7F04);   // add #4,r15
            b.Lit(2, kLaunderGlobal);
            b.Put(0x6222);   // mov.l @r2,r2
            if (walk)
            {
                b.Put(0xE102);   // mov #2,r1
                b.Put(0x2216);   // L: mov.l r1,@-r2
                b.Put(0x4110);   //    dt r1
                b.Put(0x8BFC);   //    bf L
            }
            else
            {
                b.Put(0x72FC);   // add #-4,r2
                b.Put(0xE101);   // mov #1,r1
                b.Put(0x2212);   // mov.l r1,@r2
            }
            b.Put(0x50F1);   // mov.l @(4,r15),r0
            b.Put(0x7F10);   // add #16,r15
            b.Put(0x4F26);   // lds.l @r15+,pr
            b.Put(0x000B);
            b.Put(0x0009);
        }
        else
        {
            prologue();
            b.Put(0x61F3);   // mov r15,r1
            b.Put(0x7114);   // add #20,r1: p
            b.Put(0x7FFC);   // add #-4,r15
            b.Put(0x2F12);   // mov.l r1,@r15: the first stack argument word, r4-r7 unset
            call(gOff);
            b.Put(0x7F04);   // add #4,r15
            b.Lit(2, kLaunderGlobal);
            b.Put(0x6222);   // mov.l @r2,r2
            LaunderBelow(b, 2, walk);
            epilogue(0x50F1);
        }
        b.at = gOff / 2;   // g
        if (!lent) hidden();
        b.Put(0x61F2);   // mov.l @r15,r1: its incoming stack word
        b.Lit(2, kLaunderGlobal);
        b.Put(0x000B);
        b.Put(0x2212);   // mov.l r1,@r2
        shape = std::string("escape13.stackkeep.") + (lent ? "lent." : "unlent.") + (r4r7 ? "r4r7." : "") + (walk ? "walk" : "disp");
    }
    else
    {
        static const char* const kVia[] = { "swap2", "mask", "global" };
        const std::string via = kVia[rng.Below(3)];
        prologue();
        call(0x80);
        epilogue(0x60F2);   // mov.l @r15,r0: the caller's outgoing word, which g writes
        b.at = 0x40;     // g, lent when its signature is known
        b.Put(0x7FFC);   // add #-4,r15
        b.Put(0x61F3);   // mov r15,r1
        b.Put(0x7104);   // add #4,r1: its entry r15, the caller's word
        b.Put(0x2F12);   // mov.l r1,@r15: spilled to a local
        if (via == "swap2")
        {
            b.Put(0x62F3);   // mov r15,r2
            b.Put(0x6229);   // swap.w r2,r2
            b.Put(0x6229);   // swap.w r2,r2
        }
        else if (via == "mask")
        {
            b.Put(0x62F3);   // mov r15,r2
            b.Put(0xE4FC);   // mov #-4,r4
            b.Put(0x2249);   // and r4,r2
        }
        else
        {
            b.Lit(2, kLaunderGlobal);
            b.Put(0x22F2);   // mov.l r15,@r2
            b.Put(0x6222);   // mov.l @r2,r2
        }
        b.Put(0x6322);   // mov.l @r2,r3: the spilled pointer, reloaded
        b.Put(0xE001);   // mov #1,r0
        b.Put(0x2302);   // mov.l r0,@r3
        b.Put(0x7F04);   // add #4,r15
        b.Put(0x000B);
        b.Put(0x0009);
        shape = "escape13.reload." + via;
    }
    c.words = b.Finish();
    c.name = "seed" + std::to_string(seed) + "_escape13";
    return true;
}

// ...and the walk the round 13 matrix found open (the round 11 review's s5.src S17 to S19): the frame address p = r15 + 16 with a variable
// index added or subtracted (a word of ordinary memory that holds 0: the guard's value set analysis cannot bound it), then stepped down by a
// loop to the sentinel at r15. On a stream of its own (one seed in eight), checked as the others are.
//   index.<add|sub>: p + idx or p - idx, then `L: mov.l r1,@-p ; dt r1 ; bf L`
bool GenIndexWalk13Fn(uint64_t seed, CCase& c, std::string& shape)
{
    Rng rng(seed * 2246822507u + 0x13B5u);
    if (!rng.Chance(12)) return false;
    const int K = 101 + static_cast<int>(rng.Below(27));
    const bool sub = rng.Chance(50);
    LaunderBuilder b;
    b.Put(0x4F22);   // sts.l pr,@-r15
    b.Put(0x7FE0);   // add #-32,r15
    b.Put(static_cast<uint16_t>(0xE000 | K));
    b.Put(0x2F02);   // mov.l r0,@r15: the sentinel
    b.Put(0x62F3);   // mov r15,r2
    b.Put(0x7210);   // add #16,r2: p
    b.Lit(3, kLaunderGlobal);
    b.Put(0x6332);   // mov.l @r3,r3: the index, 0 in the run
    b.Put(static_cast<uint16_t>(sub ? 0x3238 : 0x323C));   // sub r3,r2 / add r3,r2
    LaunderBelow(b, 2, true);
    b.Put(0x60F2);   // mov.l @r15,r0
    b.Put(0x7F20);   // add #32,r15
    b.Put(0x4F26);   // lds.l @r15+,pr
    b.Put(0x000B);
    b.Put(0x0009);
    shape = std::string("escape13.index.") + (sub ? "sub" : "add");
    c.words = b.Finish();
    c.name = "seed" + std::to_string(seed) + "_index13";
    return true;
}

// ---- Round 14 fix instrument (A3-121): the walks and words the round 14 review found open (A3-117 to A3-119, A3-122, and A3-126 of the fix), on
// a stream of their own (one seed in eight), checked as the others are (CheckLaunderWalks: a constant return against the run, and a returned
// temporary written once with a constant). Frame of 32 bytes, the sentinel K at r15 + 0, p = r15 + 16; the walk's four stores of the counter
// (4, 3, 2, 1) reach r15 + 12 down to r15 + 0, so the code returns 1 (4 for the walk up from a negative index).
//   walk14.idx.<down|subup|negup>.<c|u>: an index stepped by a loop: `@(r0,p)` with r0 counting down from 0 (down), `p - i` with i counting up
//        (subup), `@(r0,p)` with r0 counting up from -16 (negup, A3-126); u: p also has an index added that is 0 in the run (a word of memory)
//   walk14.stride.<pool|poolw|reload|global>.<add|sub>.<c|u>: p stepped by a register the value-set analysis cannot read: a long or a halfword
//        of the literal pool, a frame slot the stride was spilled to, a global (-4 added, or 4 subtracted)
//   walk14.stackarg.<r6|r7|r6r7>.<walk|callee>: A3-117's frame (locals 1 to 4 at r15 + 0 to 12, &local[2] in r4, an object in r5, and
//        the registers named set, so the engine shows the call with stack arguments) and an unlent callee that keeps r4 in the object; then the
//        caller walks down from the kept pointer to r15 + 0 (walk), or the callee writes its own first stack argument word, the caller's
//        r15 + 0 (callee), and the caller returns that word (0x55)
//   walk14.cap: S18's walk with 4,100 to 4,300 stores through the pointer before the walking store in the loop body (A3-122: the walk down's
//        bound), drawn one seed in a hundred of the stream (a decompile of about 30 s)
bool GenWalk14Fn(uint64_t seed, CCase& c, std::string& shape)
{
    Rng rng(seed * 2654435761u + 0x14A3u);
    if (!rng.Chance(12)) return false;
    const int K = 101 + static_cast<int>(rng.Below(27));
    LaunderBuilder b;
    const uint32_t kStrideWord = 0x1F8u;   // a word of the blob past the literal pool: a global the stride is read from
    auto prologue = [&]() {
        b.Put(0x4F22);   // sts.l pr,@-r15
        b.Put(0x7FE0);   // add #-32,r15
        b.Put(static_cast<uint16_t>(0xE000 | K));
        b.Put(0x2F02);   // mov.l r0,@r15: the sentinel
        b.Put(0x62F3);   // mov r15,r2
        b.Put(0x7210);   // add #16,r2: p
    };
    auto unbounded = [&]() {
        b.Lit(3, kLaunderGlobal);
        b.Put(0x6332);   // mov.l @r3,r3: an index, 0 in the run
        b.Put(0x323C);   // add r3,r2
    };
    auto epilogue = [&]() {
        b.Put(0x60F2);   // mov.l @r15,r0
        b.Put(0x7F20);   // add #32,r15
        b.Put(0x4F26);   // lds.l @r15+,pr
        b.Put(0x000B);
        b.Put(0x0009);
    };
    auto loopBack = [&](size_t loop) {
        b.Put(0x4110);   // dt r1
        b.Put(static_cast<uint16_t>(0x8B00 | ((static_cast<int>(loop) - static_cast<int>(b.at) - 2) & 0xFF)));   // bf L
    };
    std::vector<std::pair<size_t, uint32_t>> data;   // words of the blob to set after Finish (word index, value)
    const int form = static_cast<int>(rng.Below(100)) == 0 ? 3 : static_cast<int>(rng.Below(3));
    if (form == 0)
    {
        static const char* const kIdx[] = { "down", "subup", "negup" };
        const std::string idx = kIdx[rng.Below(3)];
        const bool u = rng.Chance(50);
        prologue();
        if (u) unbounded();
        b.Put(0xE104);   // mov #4,r1
        if (idx == "down")
        {
            b.Put(0xE300);   // mov #0,r3
            const size_t loop = b.at;
            b.Put(0x73FC);   // L: add #-4,r3
            b.Put(0x6033);   //    mov r3,r0
            b.Put(0x0216);   //    mov.l r1,@(r0,r2)
            loopBack(loop);
        }
        else if (idx == "subup")
        {
            b.Put(0xE300);   // mov #0,r3
            const size_t loop = b.at;
            b.Put(0x7304);   // L: add #4,r3
            b.Put(0x6523);   //    mov r2,r5
            b.Put(0x3538);   //    sub r3,r5
            b.Put(0x2512);   //    mov.l r1,@r5
            loopBack(loop);
        }
        else
        {
            b.Put(0xE3F0);   // mov #-16,r3
            const size_t loop = b.at;
            b.Put(0x6033);   // L: mov r3,r0
            b.Put(0x0216);   //    mov.l r1,@(r0,r2)
            b.Put(0x7304);   //    add #4,r3
            loopBack(loop);
        }
        epilogue();
        shape = "walk14.idx." + idx + (u ? ".u" : ".c");
    }
    else if (form == 1)
    {
        static const char* const kSrc[] = { "pool", "poolw", "reload", "global" };
        const std::string src = kSrc[rng.Below(4)];
        const bool sub = rng.Chance(50);
        const bool u = rng.Chance(50);
        const uint32_t stride = sub ? 4u : 0xFFFFFFFCu;
        prologue();
        if (u) unbounded();
        if (src == "pool") b.Lit(5, stride);
        else if (src == "poolw")
        {
            // mov.w @(disp,pc),r5 of the low half of a pool longword (big-endian: at its address + 2), sign-extended
            b.Lit(0, stride);   // reserve the longword (r0 is overwritten below)
            size_t k = 0;
            while (b.lits[k] != stride) ++k;
            const uint32_t pc = kCode + 2u * static_cast<uint32_t>(b.at);
            const uint32_t ea = kCode + 0x100u + 4u * static_cast<uint32_t>(k) + 2u;
            b.Put(static_cast<uint16_t>(0x9500 | ((ea - (pc + 4u)) / 2u)));
        }
        else if (src == "reload")
        {
            b.Put(sub ? 0xE504 : 0xE5FC);   // mov #4,r5 / mov #-4,r5
            b.Put(0x1F57);   // mov.l r5,@(28,r15): spilled to a slot the walk does not reach
            b.Put(0xE500);   // mov #0,r5
            b.Put(0x55F7);   // mov.l @(28,r15),r5: read back
        }
        else
        {
            b.Lit(5, kCode + kStrideWord);
            b.Put(0x6552);   // mov.l @r5,r5
            data.emplace_back(kStrideWord / 2, stride);
        }
        b.Put(0xE104);   // mov #4,r1
        const size_t loop = b.at;
        b.Put(static_cast<uint16_t>(sub ? 0x3258 : 0x325C));   // L: sub r5,r2 / add r5,r2
        b.Put(0x2212);   //    mov.l r1,@r2
        loopBack(loop);
        epilogue();
        shape = "walk14.stride." + src + (sub ? ".sub" : ".add") + (u ? ".u" : ".c");
    }
    else if (form == 2)
    {
        static const char* const kRegs[] = { "r6", "r7", "r6r7" };
        const std::string regs = kRegs[rng.Below(3)];
        const bool callee = rng.Chance(40);
        const uint32_t gOff = 0xA0u;   // not decompiled first by the check, and withheld (it reads r3): A3-117 is an unlent callee's
        b.Put(0x4F22);   // sts.l pr,@-r15
        b.Put(0x7FF0);   // add #-16,r15
        for (uint16_t k = 0; k < 4; ++k)
        {
            b.Put(static_cast<uint16_t>(0xE001 + k));   // mov #k+1,r0
            b.Put(static_cast<uint16_t>(0x1F00 + k));   // mov.l r0,@(4k,r15)
        }
        b.Put(0x64F3);   // mov r15,r4
        b.Put(0x7408);   // add #8,r4: &local[2]
        b.Lit(5, kLaunderGlobal);   // the object
        if (regs != "r7") b.Put(0xE61F);   // mov #31,r6
        if (regs != "r6") b.Put(0xE725);   // mov #37,r7
        b.Lit(0, kCode + gOff);
        b.Put(0x400B);   // jsr @r0
        b.Put(0x0009);
        if (!callee)
        {
            b.Lit(6, kLaunderGlobal);
            b.Put(0x5661);   // mov.l @(4,r6),r6: the kept pointer
            b.Put(0xE255);   // mov #0x55,r2
            b.Put(0x2626);   // mov.l r2,@-r6
            b.Put(0x2626);
            b.Put(0x2626);   // three stores, the last at r15 + 0
        }
        b.Put(0x60F2);   // mov.l @r15,r0: the word at the call's r15
        b.Put(0x7F10);   // add #16,r15
        b.Put(0x4F26);   // lds.l @r15+,pr
        b.Put(0x000B);
        b.Put(0x0009);
        b.at = gOff / 2;   // g
        b.Put(0x2338);   // tst r3,r3: reads r3 on entry, so g is withheld
        b.Lit(1, kLaunderGlobal);
        b.Put(0x1141);   // mov.l r4,@(4,r1): keeps the pointer
        if (callee)
        {
            b.Put(0xE055);   // mov #0x55,r0
            b.Put(0x2F02);   // mov.l r0,@r15: its first stack argument word, the caller's r15 + 0
        }
        b.Put(0x000B);
        b.Put(0x0009);
        shape = "walk14.stackarg." + regs + (callee ? ".callee" : ".walk");
    }
    else
    {
        // A3-122: segments of up to 1,000 stores with trampolines between them (bf reaches 256 bytes, bra 4 KB), as cap14.py builds them
        // The body is past the reach of the pool at kCode + 0x100, so the index's global is a literal at kCode + 8, jumped over.
        const int n = 4100 + static_cast<int>(rng.Below(201));
        b.Put(0xD301);   // mov.l @(kCode + 8),r3
        b.Put(0xA003);   // bra kCode + 12
        b.Put(0x0009);
        b.Put(0x0009);
        b.Put(static_cast<uint16_t>(kLaunderGlobal >> 16));
        b.Put(static_cast<uint16_t>(kLaunderGlobal));
        b.Put(0x6332);   // mov.l @r3,r3: an index, 0 in the run
        b.Put(0x7FE8);   // add #-24,r15
        b.Put(0x62F3);   // mov r15,r2
        b.Put(0x7210);   // add #16,r2
        b.Put(static_cast<uint16_t>(0xE000 | K));
        b.Put(0x2F02);   // mov.l r0,@r15
        b.Put(0x323C);   // add r3,r2: an unbounded index
        b.Put(0xE104);   // mov #4,r1
        size_t prev = b.at;   // L
        int left = n;
        while (left > 0)
        {
            const int seg = std::min(1000, left);
            left -= seg;
            for (int i = 0; i < seg; ++i) b.Put(0x1211);   // mov.l r1,@(4,r2)
            if (left > 0)
            {
                const size_t braOver = b.at;
                b.Put(0xA000);   // bra OVER (patched)
                b.Put(0x0009);
                const size_t m = b.at;   // M: bra prev
                b.Put(static_cast<uint16_t>(0xA000 | ((static_cast<int>(prev) - static_cast<int>(b.at) - 2) & 0xFFF)));
                b.Put(0x0009);
                b.w[braOver] = static_cast<uint16_t>(0xA000 | ((static_cast<int>(b.at) - static_cast<int>(braOver) - 2) & 0xFFF));
                prev = m;
            }
        }
        b.Put(0x2216);   // mov.l r1,@-r2
        b.Put(0x4110);   // dt r1
        b.Put(0x8902);   // bt EXIT
        b.Put(0x0009);
        b.Put(static_cast<uint16_t>(0xA000 | ((static_cast<int>(prev) - static_cast<int>(b.at) - 2) & 0xFFF)));   // bra prev
        b.Put(0x0009);
        b.Put(0x60F2);   // EXIT: mov.l @r15,r0
        b.Put(0x7F18);   // add #24,r15
        b.Put(0x000B);
        b.Put(0x0009);
        shape = "walk14.cap";
        c.words = b.w;
        c.name = "seed" + std::to_string(seed) + "_walk14";
        return true;
    }
    c.words = b.Finish();
    for (const auto& d : data)
    {
        if (c.words.size() < d.first + 2) c.words.resize(d.first + 2, 0x0009);
        c.words[d.first] = static_cast<uint16_t>(d.second >> 16);
        c.words[d.first + 1] = static_cast<uint16_t>(d.second);
    }
    c.name = "seed" + std::to_string(seed) + "_walk14";
    return true;
}

// ---- A3-124 fix instrument (patch 0068): a struct built on the frame through a pointer kept in a frame slot and reloaded before each access
// (the `-O0` byte copy of the corpus, hwram007 060228B4), on a stream of its own (one seed in four). Frame of 32 bytes on r15 or, `-O0`
// style, on r14 (pushed, r15 copied); an 8-byte struct S at frame + 8, 12 or 16, &S in the slot at frame + 28. S is first filled whole (two
// words of constants, or eight byte stores in a random order), then 1 to 8 byte, halfword and word stores at aligned offsets (the values
// r4 to r7, or constants), each through the pointer reloaded from the slot (one in five through the r1 of the previous store), then 1 to 3
// reads of S, byte, halfword or word wide (wider or narrower than the stores), through the reloaded pointer or directly at the frame
// offset, added or xored into r0.
//   frame124.<r15|r14>.<words|bytes>.<nocall|call|callr>.<b|w|l|mixed>: the frame base, the fill, whether &S is handed to a lent callee
//        at kCode + 0xC0 (rts ; nop) between the stores and the reads, or (callr, A3-128) to a withheld one that reads a word of S, which
//        the caller writes again after the call, and the stores' widths. A function without a call is checked by the
//        printed-C differential (its text compiled and run beside the code for each input), which skips a text with a call; every one, with
//        a call or not, is checked by the frame ledger (dts::FrameLosses, CheckFrame124Ledger). A text with a call reads S through the kept
//        pointer after it (the frame escaped), which no constant check resolves: the ledger is its check.
bool GenFrame124Fn(uint64_t seed, CCase& c, std::string& shape)
{
    Rng rng(seed * 2654435761u + 0x124A3u);
    if (!rng.Chance(25)) return false;
    const bool r14 = rng.Chance(50);
    const bool call = rng.Chance(35);
    // (A3-128) the callee reads S (`mov.l @(off,r4),r0`) and is withheld (it reads r3), and S is written again after the call
    const bool reader = call && rng.Chance(50);
    const bool byteFill = !call && rng.Chance(50);   // (a call's function must end before its callee at kCode + 0xC0)
    const uint32_t fb = r14 ? 14u : 15u;
    const uint32_t sOff = 8u + 4u * rng.Below(3);
    const uint32_t kSlotDisp = 7;   // the slot at frame + 28
    LaunderBuilder b;
    auto put = [&](uint32_t x) { b.Put(static_cast<uint16_t>(x)); };
    if (r14) put(0x2FE6);                            // mov.l r14,@-r15
    if (call) put(0x4F22);                           // sts.l pr,@-r15
    put(0x7FE0);                                     // add #-32,r15
    if (r14) put(0x6EF3);                            // mov r15,r14
    put(0x6103 | fb << 4);                           // mov fb,r1
    put(0x7100 | sOff);                              // add #sOff,r1: &S
    put(0x1010 | fb << 8 | kSlotDisp);               // mov.l r1,@(28,fb): the slot
    bool kept = false;                               // r1 holds &S from the last store
    auto reload = [&]() {
        put(0x5100 | fb << 4 | kSlotDisp);           // mov.l @(28,fb),r1
        kept = true;
    };
    // The value stored: a register parameter, or a constant in r2.
    auto value = [&]() -> uint32_t {
        if (rng.Chance(30))
        {
            put(0xE200 | rng.Below(256));            // mov #imm,r2
            return 2;
        }
        return 4 + rng.Below(4);
    };
    // A store of 'size' bytes at S + off through the reloaded pointer (r1 is never stepped: the address is built in r3).
    auto store = [&](uint32_t size, uint32_t off, bool mayKeep) {
        if (!(mayKeep && kept && rng.Chance(20))) reload();
        const uint32_t v = value();
        if (size == 4)
        {
            put(0x1100 | v << 4 | (off / 4));        // mov.l v,@(off,r1)
            return;
        }
        put(0x6313);                                 // mov r1,r3
        if (off) put(0x7300 | off);                  // add #off,r3
        put(0x2300 | v << 4 | (size == 1 ? 0u : 1u));   // mov.b / mov.w v,@r3
    };
    if (byteFill)
    {
        uint32_t order[8] = { 0, 1, 2, 3, 4, 5, 6, 7 };
        for (uint32_t i = 7; i > 0; --i) std::swap(order[i], order[rng.Below(i + 1)]);
        for (uint32_t i = 0; i < 8; ++i) store(1, order[i], false);
    }
    else
        for (uint32_t k = 0; k < 2; ++k)
        {
            reload();
            put(0xE200 | rng.Below(256));            // mov #imm,r2
            put(0x1120 | k);                         // mov.l r2,@(4k,r1)
        }
    static const char* const kWidths[] = { "b", "w", "l", "mixed" };
    const uint32_t wk = rng.Below(4);
    const uint32_t nStores = 1 + rng.Below(call ? 6 : 8);
    for (uint32_t i = 0; i < nStores; ++i)
    {
        const uint32_t size = wk == 0 ? 1 : wk == 1 ? 2 : wk == 2 ? 4 : (1u << rng.Below(3));
        store(size, rng.Below(8 / size) * size, true);
    }
    const uint32_t readOff = rng.Below(2);           // the word of S the reading callee reads
    if (call)
    {
        put(0x5400 | fb << 4 | kSlotDisp);           // mov.l @(28,fb),r4: &S
        b.Lit(0, kCode + 0xC0u);
        put(0x400B);                                 // jsr @r0
        put(0x0009);
        if (reader)
        {
            put(0xE200 | rng.Below(256));            // mov #imm,r2
            reload();
            put(0x1120 | readOff);                   // mov.l r2,@(4*readOff,r1): the word the callee read, written again
        }
    }
    const uint32_t nReads = 1 + rng.Below(3);
    for (uint32_t i = 0; i < nReads; ++i)
    {
        const uint32_t size = 1u << rng.Below(3);
        const uint32_t off = rng.Below(8 / size) * size;
        if (rng.Chance(70))
        {
            put(0x5100 | fb << 4 | kSlotDisp);       // mov.l @(28,fb),r1
            put(0x6313);                             // mov r1,r3
            if (off) put(0x7300 | off);              // add #off,r3
        }
        else
        {
            put(0x6303 | fb << 4);                   // mov fb,r3: S at its frame offset
            put(0x7300 | (sOff + off));              // add #sOff+off,r3
        }
        put(0x6230 | (size == 1 ? 0u : size == 2 ? 1u : 2u));   // mov.b / .w / .l @r3,r2
        put(i == 0 ? 0x6023u : rng.Chance(50) ? 0x302Cu : 0x202Au);   // mov r2,r0 / add r2,r0 / xor r2,r0
    }
    if (r14)
    {
        put(0x7E20);                                 // add #32,r14
        put(0x6FE3);                                 // mov r14,r15
        if (call) put(0x4F26);                       // lds.l @r15+,pr
        put(0x000B);                                 // rts
        put(0x6EF6);                                 //   mov.l @r15+,r14
    }
    else
    {
        put(0x7F20);                                 // add #32,r15
        if (call) put(0x4F26);                       // lds.l @r15+,pr
        put(0x000B);                                 // rts
        put(0x0009);
    }
    if (call)
    {
        if (b.at > 0xC0u / 2) return false;          // (not drawn: the bound above keeps it under)
        b.at = 0xC0u / 2;                            // the callee
        if (reader)
        {
            put(0x2338);                             // tst r3,r3: reads r3 on entry, so it is withheld (not lent)
            put(0x5040 | readOff);                   // mov.l @(4*readOff,r4),r0
        }
        put(0x000B);
        put(0x0009);
    }
    shape = std::string("frame124.") + (r14 ? "r14" : "r15") + (byteFill ? ".bytes" : ".words") + (reader ? ".callr." : call ? ".call." : ".nocall.") +
            kWidths[wk];
    c.words = call ? b.Finish() : b.w;
    c.name = "frame124_" + std::to_string(seed);
    return true;
}

// The frame ledger over the frame124 stream (A3-124): each function decompiled as CheckLaunderWalks does (the callee at kCode + 0xC0 lent
// first when there is one), and every frame write that went unread while the code reads it is a finding (dts::FrameLosses).
int CheckFrame124Ledger(Sh2Decompiler& d, const std::vector<CCase>& cases)
{
    int lost = 0;
    for (size_t ci = 0; ci < cases.size(); ++ci)
    {
        const CCase& c = cases[ci];
        std::vector<uint8_t> hw;
        for (size_t i = 0; i < c.words.size(); ++i) dts::Put16(hw, kCode - 0x06000000u + 2 * i, c.words[i]);
        dts::Put16(hw, kSentinel - 0x06000000u, 0x0009);
        dts::Put16(hw, kSentinel - 0x06000000u + 2, 0x0009);
        auto snap = dts::MakeSnapshot(hw, 9100 + ci);
        DecompileRequest q;
        q.entry = kCode;
        if (c.words.size() > 0xC0u / 2 && c.words[0xC0u / 2] != 0x0009)
        {
            DecompileRequest cq;
            cq.entry = kCode + 0xC0u;
            const DecompileResult cr = d.Decompile(snap, cq);
            if (cr.status == DecompileStatus::Ok && cr.prototype.known) q.prototypes.emplace_back(kCode + 0xC0u, cr.prototype);
        }
        const DecompileResult r = d.Decompile(snap, q);
        if (EnvNumber("SE_GENERATED_VERBOSE", 0) >= 2)
            std::printf("%s (frame ledger)\n  words: %s\n%s\n", c.name.c_str(), WordsText(c.words).c_str(), r.Text().c_str());
        for (const AccessFate& f : r.accessFates)
            if (f.fate == AccessFate::MalformedPiece)
            {
                ++lost;
                std::printf("FINDING [malformed-piece] %s\n  detail: a piece of %u byte(s) at %s has sizes that do not add up (A3-127)\n  words: %s\n%s\n",
                            c.name.c_str(), unsigned(f.size), Hex(f.insn).c_str(), WordsText(c.words).c_str(), r.Text().c_str());
                break;
            }
        for (const dts::FrameLoss& l : dts::FrameLosses(r, dts::SnapshotWords(snap)))
        {
            ++lost;
            std::printf("FINDING [frame-lost] %s\n  detail: the frame write of %u byte(s) at %s (frame offset %s) went unread, and the read at %s reads it\n"
                        "  words: %s\n%s\n", c.name.c_str(), unsigned(l.size), Hex(l.write).c_str(), Hex(l.offset).c_str(), Hex(l.read).c_str(),
                        WordsText(c.words).c_str(), r.Text().c_str());
            break;
        }
    }
    return lost;
}

// The check of the laundered walks: lend the callees (deepest first: kCode + 0xC0, then kCode + 0x80, when the blob has code there), decompile
// the entry, run once, and compare every constant return with the run's r0 (as CheckSpillWalks).
std::vector<CVerdict> CheckLaunderWalks(Sh2Decompiler& d, const std::vector<CCase>& cases, const std::vector<std::string>* shapes = nullptr)
{
    std::vector<CVerdict> out(cases.size());
    static const std::regex ret("\\breturn\\b\\s*([^;]*);");
    static const std::regex literal("^\\(?\\s*(?:\\(\\s*\\w+\\s*\\)\\s*)?(-?(?:0x[0-9a-fA-F]+|\\d+))\\s*\\)?$");
    for (size_t ci = 0; ci < cases.size(); ++ci)
    {
        const CCase& c = cases[ci];
        CVerdict& v = out[ci];
        std::vector<uint8_t> hw;
        for (size_t i = 0; i < c.words.size(); ++i) dts::Put16(hw, kCode - 0x06000000u + 2 * i, c.words[i]);
        dts::Put16(hw, kSentinel - 0x06000000u, 0x0009);
        dts::Put16(hw, kSentinel - 0x06000000u + 2, 0x0009);
        auto snap = dts::MakeSnapshot(hw, 7000 + ci);
        std::vector<std::pair<uint32_t, Prototype>> protos;
        // (area 3 round 14 fix) A3-122's body is one function of 8 KB: nothing at kCode + 0x80 or 0xC0 is a callee, and its loop runs 17,000 steps
        const bool cap = shapes != nullptr && ci < shapes->size() && (*shapes)[ci] == "walk14.cap";
        for (uint32_t off : { 0xC0u, 0x80u })
        {
            if (cap) break;
            const size_t wi = off / 2;
            if (c.words.size() <= wi || c.words[wi] == 0x0009) continue;
            DecompileRequest cq;
            cq.entry = kCode + off;
            cq.prototypes = protos;
            const DecompileResult cr = d.Decompile(snap, cq);
            if (cr.status == DecompileStatus::Ok && cr.prototype.known) protos.emplace_back(kCode + off, cr.prototype);
        }
        // (area 3 round 12 fix, A3-93) The spare shape is checked by f's claim, not the caller's text: f overwrites the r14 it saved through a
        // laundered copy of r15 handed to a lent writer, and the run returns what was written there, so a lent f must not preserve r14.
        if (shapes != nullptr && ci < shapes->size() && (*shapes)[ci] == "escape.spare")
        {
            for (const auto& pr : protos)
                if (pr.first == kCode + 0x80u && ((pr.second.effects.preserved >> 14) & 1u))
                {
                    v.cls = "claim-wrong:preserved";
                    v.detail = "f is lent with r14 preserved, and its writer overwrote the saved r14";
                }
            if (v.cls.empty()) v.cls = "escape:spare";
            continue;
        }
        DecompileRequest q;
        q.entry = kCode;
        q.prototypes = protos;
        const DecompileResult r = d.Decompile(snap, q);
        v.text = r.Text();
        if (r.status != DecompileStatus::Ok || r.unsupported || r.truncated)
        {
            v.cls = "undecompiled";
            continue;
        }
        Env env;
        env.ram = snap->regions[1].bytes;
        std::unique_ptr<Machine> machine(new Machine(snap, &env));
        for (int i = 0; i < 15; ++i) machine->Set(("r" + std::to_string(i)).c_str(), i < 8 ? 0x11111111u * static_cast<uint32_t>(i) : CalleeSaved(i));
        machine->Set("r15", kStackTop);
        machine->Set("pr", kSentinel);
        RunOut run;
        if (!machine->Run(kCode, kSentinel, cap ? 40000 : kStepLimit, run, env))
        {
            v.cls = "generator:no-return";
            continue;
        }
        bool any = false, local = false;
        for (std::sregex_iterator it(v.text.begin(), v.text.end(), ret), e; it != e; ++it)
        {
            any = true;
            std::string what = (*it)[1].str();
            std::string folded;   // (outlives the match below)
            std::smatch m;
            // (area 3 round 14 fix, A3-121) A temporary the text writes once, with a constant, and returns is that constant: a register or an
            // unmapped stack word (`xVar2 = 1; f(...); return xVar2;`, A3-117), which no call or store of the text can change. A stack local
            // (`xStack_14`) is memory a call may write, and is not resolved. Round 13's check counted such a text as a local and never read it.
            if (!std::regex_match(what, m, literal))
            {
                static const std::regex temp("^(?:xVar|iVar|uVar|in_)\\w*$");
                if (std::regex_match(what, temp) && v.text.find("&" + what) == std::string::npos)
                {
                    const std::regex assign("\\b" + what + " = ([^;]*);");
                    int writes = 0;
                    for (std::sregex_iterator a(v.text.begin(), v.text.end(), assign), ae; a != ae; ++a)
                    {
                        ++writes;
                        folded = (*a)[1].str();
                    }
                    if (writes != 1 || !std::regex_match(folded, literal)) folded.clear();
                }
                if (folded.empty())
                {
                    local = true;
                    continue;
                }
                what = what + " (= " + folded + ")";
                std::regex_match(folded, m, literal);
            }
            const std::string n = m[1].str();
            const bool neg = n[0] == '-';
            const uint32_t mag = static_cast<uint32_t>(std::strtoull(n.c_str() + (neg ? 1 : 0), nullptr, 0));
            const uint32_t value = neg ? 0u - mag : mag;
            v.compared = true;
            if (value != run.fin[0])
            {
                v.cls = "stale-constant";
                v.detail = "the text returns " + what + "; the code returns " + Hex(run.fin[0]);
            }
        }
        if (v.cls.empty() && (!any || local)) v.cls = any ? "launder:local" : "launder:no-return";
    }
    return out;
}

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
    TestOracleOwnDecisions();
    Sh2Decompiler d(gConfig);
    const int nIn = static_cast<int>(EnvNumber("SE_GENERATED_PRINTEDC_INPUTS", 32));
    std::vector<CCase> cases;
    std::vector<RegFn> fns;
    const bool hand = std::getenv("SE_GENERATED_PRINTEDC_WORDS") != nullptr;
    if (!hand)
    {
        // A2-61: a text that returns the address of a local is not compared by value (it was a mismatch: the run's stack address against the C's 0).
        // Not a fixture (a fixture the harness skips is a failure), so it is classified here.
        const std::vector<CVerdict> v = CheckPrintedC(d, { { "seedA2-61", { 0x7FF0, 0x1F72, 0x62F3, 0x720E, 0x2261, 0x62F3, 0x7208, 0x2242, 0x1F01, 0x0447, 0x041A,
                                                                            0x1F60, 0x63F3, 0x730F, 0x2340, 0x7508, 0x50F1, 0x85F5, 0x85F5, 0x600D, 0x7F10, 0x000B,
                                                                            0x6033, 0x0009 } } },
                                                      nIn, cc, 0x51ED, 8997);
        std::printf("pin 'A2-61: a returned stack address is not compared': %s (%s)\n", v[0].cls.c_str(), v[0].detail.c_str());
        CHECK(v[0].cls == "skipped:stack-address");
        // A2-65: a returned local array (frame mode, the register r15 copied) is its address too: it was a mismatch (run=0603FFF8, pseudo=41709FF8).
        const std::vector<CVerdict> w = CheckPrintedC(d, { { "seedA2-65", { 0x6043, 0x6153, 0x6263, 0x6373, 0x7FF8, 0x81F2, 0x61F3, 0x7100, 0x2160, 0x62F3, 0x7200, 0x2262,
                                                                            0x84F2, 0x4001, 0x4000, 0x84F4, 0xC903, 0x7F08, 0x000B, 0x6023 } } },
                                                      nIn, cc, 0x51ED, 8998);
        std::printf("pin 'A2-65: a returned local array is not compared': %s (%s)\n", w[0].cls.c_str(), w[0].detail.c_str());
        CHECK(w[0].cls == "skipped:stack-address");
        // A3-87/A3-79: a walk that reaches storage no local is mapped to (a pushed register's save slot) prints a pointer into it
        // as the raw stack register `register0x0000003c`, which is no compilable C (A3-65's recorded limit for unmapped stack).
        // The decompiler now marks such a function unsupported and withholds it (never a lent signature with non-compilable text),
        // and the printed-C differential excuses the known limit instead of reporting `not-compilable`. Reverting either the
        // withhold (Sh2Decompiler.cpp) or the excuse (IsFailing/CheckPrintedC) makes these seeds fail again.
        static const std::pair<const char*, std::vector<uint16_t>> kRawRegister[] = {
            { "A3-87: walk from the frame pointer r14's save slot (lent at the head)",
              { 0x2FE6, 0x7FF8, 0x6EF3, 0xE005, 0x2E02, 0x62E3, 0x7208, 0xE102, 0x2216, 0x4110, 0x8BFC, 0x60E2, 0x6FE3, 0x7F08, 0x000B, 0x6EF6 } },
            { "A3-79: printed-C seed 1010082 (withheld, reads vbr)",
              { 0x0722, 0x7FEC, 0x1F60, 0x1F74, 0x61F3, 0x7110, 0xE204, 0x2176, 0x4210, 0x8BFC, 0xE204, 0xE400, 0x6616, 0x346C, 0x4210, 0x8BFB, 0x50F2, 0x304C, 0x7F14, 0x000B, 0x0009 } },
            { "A3-87: printed-C seed 1503985 (area 2 round 12)",
              { 0x6043, 0x6153, 0x6263, 0x6373, 0x3718, 0x0022, 0x7FF0, 0x1F10, 0x1F02, 0x63F3, 0x7308, 0xE602, 0x2356, 0x4610, 0x8BFC, 0x50F0, 0x7F10, 0x600C, 0x000B, 0x0009 } },
            { "A3-87: printed-C seed 1506163 (area 2 round 12)",
              { 0x6043, 0x6153, 0x6263, 0x6373, 0x0522, 0x7FF0, 0x1F10, 0x1F51, 0x1F32, 0x66F3, 0x7604, 0xE201, 0x2616, 0x4210, 0x8BFC, 0xE201, 0xE400, 0x6566, 0x345C, 0x4210, 0x8BFB, 0x50F1, 0x304C, 0x7F10, 0x000B, 0x0009 } },
        };
        for (const auto& rr : kRawRegister)
        {
            const std::vector<CVerdict> rv = CheckPrintedC(d, { { rr.first, rr.second } }, nIn, cc, 0x51ED, 8996);
            std::printf("pin '%s': %s (%s)\n", rr.first, rv[0].cls.empty() ? "agrees" : rv[0].cls.c_str(), rv[0].detail.c_str());
            // (area 3 round 14 fix, patch 0066) Each walk's pointer is stepped down, so it is no exact frame offset: the frame escapes, the
            // walk's guard covers the stack space, and the save slot it reaches stays mapped (ActionRestrictLocal). The text names the slot as a
            // local where it named the raw register, and agrees with the run; the excuse stays for a raw register another way may leave.
            CHECK(rv[0].cls.empty() && rv[0].compared);
            CHECK(!IsFailing(rv[0]));
        }
        // A3-102 (area 3 round 12 fix, patch 0063): a pointer stepped up from a local over a slot the function saved GBR in (`stc gbr,r7 ;
        // mov.l r7,@(8,r15)`, then `mov r15,r7 ; L: mov.l r3,@r7 ; add #4,r7 ; dt r3 ; bf L`) wrote the slot, and the read of it after the loop
        // printed the saved register (`return CONCAT44(unaff_gbr,param_2);`, a mismatch): the slot was unmapped as a save slot, which only a
        // pointer stepped down kept mapped (0056). A slot an indexed store's guard covers stays mapped now; the text agrees with the run.
        {
            const std::vector<CVerdict> rv = CheckPrintedC(d, { { "A3-102: printed-C walk 2208857", { 0x6043, 0x6153, 0x6263, 0x6373, 0x0712, 0x7FF4, 0x1F21, 0x1F72, 0x67F3, 0xE303,
                                                                                                         0x2732, 0x7704, 0x4310, 0x8BFB, 0x50F2, 0x7F0C, 0x000B, 0x0009 } } },
                                                            nIn, cc, 0x51ED, 8995);
            std::printf("pin 'A3-102: printed-C walk 2208857': %s (%s)\n", rv[0].cls.empty() ? "agrees" : rv[0].cls.c_str(), rv[0].detail.c_str());
            CHECK(rv[0].cls.empty() && rv[0].compared);
        }
    }
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
        for (const CCase& f : WidePrintedCFixtures()) cases.push_back(f);
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
        // The walks of a frame (A3-65), after every other case, so that none of those moves (a case's inputs are drawn from its place in its batch).
        // SE_GENERATED_PRINTEDC_WALK=0 leaves them out.
        if (EnvNumber("SE_GENERATED_PRINTEDC_WALK", 1) != 0)
            for (long seed = first; seed < first + seeds; ++seed)
            {
                RegFn walk;
                if (!GenWalkFn(static_cast<uint64_t>(seed), walk)) continue;
                fns.push_back(walk);
                cases.push_back({ "seed" + std::to_string(seed) + "_walk", walk.Words() });
            }
        // (A3-124 fix, patch 0068) The frame structs without a call (GenFrame124Fn), after those: compiled and run beside the code like the rest.
        // SE_GENERATED_PRINTEDC_FRAME124=0 leaves them out.
        if (EnvNumber("SE_GENERATED_PRINTEDC_FRAME124", 1) != 0)
            for (long seed = first; seed < first + seeds; ++seed)
            {
                CCase f;
                std::string shape;
                if (!GenFrame124Fn(static_cast<uint64_t>(seed), f, shape) || shape.find(".call") != std::string::npos) continue;
                fns.push_back(RegFn());
                cases.push_back(f);
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
        CVerdict& v = verdicts[i];
        compared += v.compared ? 1 : 0;
        ++classes[v.cls.empty() ? "agrees" : v.cls];
        if (hand || EnvNumber("SE_GENERATED_VERBOSE", 0) >= 2) std::printf("%s: %s%s\n%s\n", cases[i].name.c_str(), v.cls.empty() ? "agrees" : v.cls.c_str(), v.detail.empty() ? "" : (" (" + v.detail + ")").c_str(), v.text.c_str());
        // A generated function (a seed) has no call and reads no stack argument: a text the oracle skipped for one is the grammar leaking an
        // instruction it cannot run (A2-38: it was braf, which the engine reads as a tail call, in every one of 268 of 3,060).
        const bool grammarLeak = !hand && v.cls == "skipped:call-or-stack" && cases[i].name.compare(0, 4, "seed") == 0;
        if (grammarLeak) v.cls = "grammar-leak";
        // A ledger fixture exists to be compared: one the harness skips (an input it cannot give a value, a stack piece) checks nothing, and is a failure,
        // not a quiet class (A2-37: `unaff_r8_00` was `skipped:unknown-input`).
        // A frame124 function (A3-124) is drawn, not written: one the harness skips (a text that takes a local's address) is counted below.
        if (!hand && cases[i].name.compare(0, 4, "seed") != 0 && cases[i].name.compare(0, 9, "frame124_") != 0 && v.cls.compare(0, 8, "skipped:") == 0)
        {
            v.detail = "the fixture was skipped (" + v.cls + "): " + v.detail;
            v.cls = "fixture-skipped";
        }
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
    // The walk pointer kept in a stack local (A3-80), from a stream of its own, after every other case: checked by its constant returns.
    // SE_GENERATED_PRINTEDC_SPILL=0 leaves them out.
    // SE_GENERATED_SPILL_WORDS replays one function (the words of a finding).
    const char* spillWords = std::getenv("SE_GENERATED_SPILL_WORDS");
    if ((!hand || spillWords) && EnvNumber("SE_GENERATED_PRINTEDC_SPILL", 1) != 0)
    {
        const long single = EnvNumber("SE_GENERATED_SEED", 0);
        const long seeds = single ? 1 : EnvNumber("SE_GENERATED_SEEDS", 300);
        const long first = single ? single : EnvNumber("SE_GENERATED_FIRST", 1);
        std::vector<CCase> spills;
        std::map<std::string, int> spillShapes, spillClasses;
        for (long seed = first; seed < first + seeds; ++seed)
        {
            CCase c;
            std::string shape;
            if (spillWords || !GenSpillWalkFn(static_cast<uint64_t>(seed), c, shape)) continue;
            ++spillShapes[shape];
            spills.push_back(c);
        }
        if (spillWords)
        {
            CCase c;
            c.name = "spill-hand";
            std::istringstream in(spillWords);
            std::string tok;
            while (in >> tok) c.words.push_back(static_cast<uint16_t>(std::strtoul(tok.c_str(), nullptr, 16)));
            spills.push_back(c);
        }
        const std::vector<CVerdict> sv = CheckSpillWalks(d, spills);
        for (size_t i = 0; i < sv.size(); ++i)
        {
            ++spillClasses[sv[i].cls.empty() ? "agrees" : sv[i].cls];
            const bool bad = sv[i].cls == "stale-constant" || sv[i].cls == "undecompiled" || sv[i].cls == "generator:no-return";
            if (spillWords) std::printf("%s: %s %s\n%s\n", spills[i].name.c_str(), sv[i].cls.empty() ? "agrees" : sv[i].cls.c_str(), sv[i].detail.c_str(), sv[i].text.c_str());
            if (!bad) continue;
            ++failing;
            std::printf("FINDING [%s] %s\n  words: %s\n  detail: %s\n  replay: SE_GENERATED_PRINTEDC=only SE_GENERATED_SPILL_WORDS=\"%s\"\n%s\n", sv[i].cls.c_str(),
                        spills[i].name.c_str(), WordsText(spills[i].words).c_str(), sv[i].detail.c_str(), WordsText(spills[i].words).c_str(), sv[i].text.c_str());
        }
        std::printf("spill walks: functions=%zu classes:", sv.size());
        for (const auto& c : spillClasses) std::printf(" %s=%d", c.first.c_str(), c.second);
        std::printf(" shapes:");
        for (const auto& c : spillShapes) std::printf(" %s=%d", c.first.c_str(), c.second);
        std::printf("\n");
    }
    // The laundered frame addresses and the callees around them (round 11 fix, A3-82 to A3-88), from a stream of their own, checked by their
    // constant returns. SE_GENERATED_PRINTEDC_LAUNDER=0 leaves them out; SE_GENERATED_LAUNDER_WORDS replays one (the words of a finding).
    const char* launderWords = std::getenv("SE_GENERATED_LAUNDER_WORDS");
    if ((!hand || launderWords) && EnvNumber("SE_GENERATED_PRINTEDC_LAUNDER", 1) != 0)
    {
        const long single = EnvNumber("SE_GENERATED_SEED", 0);
        const long seeds = single ? 1 : EnvNumber("SE_GENERATED_SEEDS", 300);
        const long first = single ? single : EnvNumber("SE_GENERATED_FIRST", 1);
        std::vector<CCase> cases;
        std::vector<std::string> shapes;
        std::map<std::string, int> launderClasses;
        for (long seed = first; seed < first + seeds && !launderWords; ++seed)
        {
            CCase c;
            std::string shape;
            if (GenLaunderWalkFn(static_cast<uint64_t>(seed), c, shape))
            {
                cases.push_back(c);
                shapes.push_back(shape);
            }
            // (area 3 round 12 fix) ...and the shapes of A3-89 to A3-94, A3-97, on a stream of their own
            CCase e;
            if (GenEscapeWalkFn(static_cast<uint64_t>(seed), e, shape))
            {
                cases.push_back(e);
                shapes.push_back(shape);
            }
            // (area 3 round 13 fix, A3-114) ...and those of A3-104 to A3-106, on another, and the indexed walk on a third
            CCase e13;
            if (GenEscape13Fn(static_cast<uint64_t>(seed), e13, shape))
            {
                cases.push_back(e13);
                shapes.push_back(shape);
            }
            CCase i13;
            if (GenIndexWalk13Fn(static_cast<uint64_t>(seed), i13, shape))
            {
                cases.push_back(i13);
                shapes.push_back(shape);
            }
            // (area 3 round 14 fix, A3-121) ...and the walks and words of A3-117 to A3-119, A3-122 and A3-126 on a fourth
            CCase w14;
            if (GenWalk14Fn(static_cast<uint64_t>(seed), w14, shape))
            {
                cases.push_back(w14);
                shapes.push_back(shape);
            }
        }
        if (launderWords)
        {
            CCase c;
            c.name = "launder-hand";
            std::istringstream in(launderWords);
            std::string tok;
            while (in >> tok) c.words.push_back(static_cast<uint16_t>(std::strtoul(tok.c_str(), nullptr, 16)));
            cases.push_back(c);
            shapes.push_back("hand");
        }
        const std::vector<CVerdict> lv = CheckLaunderWalks(d, cases, &shapes);
        std::map<std::string, int> staleByShape;
        for (size_t i = 0; i < lv.size(); ++i)
        {
            ++launderClasses[lv[i].cls.empty() ? "agrees" : lv[i].cls];
            const bool bad = lv[i].cls == "stale-constant" || lv[i].cls == "undecompiled" || lv[i].cls == "generator:no-return" ||
                             lv[i].cls == "claim-wrong:preserved";
            if (launderWords) std::printf("%s: %s %s\n%s\n", cases[i].name.c_str(), lv[i].cls.empty() ? "agrees" : lv[i].cls.c_str(), lv[i].detail.c_str(), lv[i].text.c_str());
            if (!bad) continue;
            ++failing;
            ++staleByShape[shapes[i]];
            std::printf("FINDING [%s] %s (%s)\n  detail: %s\n  replay: SE_GENERATED_PRINTEDC=only SE_GENERATED_LAUNDER_WORDS=\"%s\"\n%s\n", lv[i].cls.c_str(),
                        cases[i].name.c_str(), shapes[i].c_str(), lv[i].detail.c_str(), WordsText(cases[i].words).c_str(), lv[i].text.c_str());
        }
        // (area 3 round 14 fix) the round 14 stream's draws, each with how many texts were compared by value (a constant or a resolved temporary)
        std::map<std::string, std::pair<int, int>> walk14;
        for (size_t i = 0; i < lv.size(); ++i)
            if (shapes[i].compare(0, 7, "walk14.") == 0)
            {
                ++walk14[shapes[i]].first;
                if (lv[i].compared) ++walk14[shapes[i]].second;
            }
        if (!walk14.empty())
        {
            std::printf("round 14 walks (drawn/compared):");
            for (const auto& k : walk14) std::printf(" %s=%d/%d", k.first.c_str(), k.second.first, k.second.second);
            std::printf("\n");
        }
        std::printf("laundered walks: functions=%zu classes:", lv.size());
        for (const auto& k : launderClasses) std::printf(" %s=%d", k.first.c_str(), k.second);
        std::printf(" failing by shape:");
        for (const auto& k : staleByShape) std::printf(" %s=%d", k.first.c_str(), k.second);
        std::printf("\n");
    }
    // (A3-124 fix, patch 0068) The frame ledger over the frame124 stream, with and without a call: a frame write that went unread while the
    // code reads it is a finding whatever the text's value (SE_GENERATED_FRAME124_WORDS replays one).
    const char* frame124Words = std::getenv("SE_GENERATED_FRAME124_WORDS");
    if ((!hand || frame124Words) && EnvNumber("SE_GENERATED_PRINTEDC_FRAME124", 1) != 0)
    {
        const long single = EnvNumber("SE_GENERATED_SEED", 0);
        const long seeds = single ? 1 : EnvNumber("SE_GENERATED_SEEDS", 300);
        const long first = single ? single : EnvNumber("SE_GENERATED_FIRST", 1);
        std::vector<CCase> f124;
        std::map<std::string, int> drawn;
        for (long seed = first; seed < first + seeds && !frame124Words; ++seed)
        {
            CCase c;
            std::string shape;
            if (!GenFrame124Fn(static_cast<uint64_t>(seed), c, shape)) continue;
            ++drawn[shape.substr(0, shape.rfind('.'))];
            f124.push_back(c);
        }
        if (frame124Words)
        {
            CCase c;
            c.name = "frame124-hand";
            std::istringstream in(frame124Words);
            std::string tok;
            while (in >> tok) c.words.push_back(static_cast<uint16_t>(std::strtoul(tok.c_str(), nullptr, 16)));
            f124.push_back(c);
        }
        const int lost = CheckFrame124Ledger(d, f124);
        failing += lost;
        int compared124 = 0, skipped124 = 0, n124 = 0;
        for (size_t i = 0; i < verdicts.size(); ++i)
            if (cases[i].name.compare(0, 9, "frame124_") == 0)
            {
                ++n124;
                compared124 += verdicts[i].compared ? 1 : 0;
                skipped124 += verdicts[i].cls.compare(0, 8, "skipped:") == 0 ? 1 : 0;
            }
        std::printf("frame124 structs: %zu drawn, %d without a call through the differential (%d compared, %d skipped), %d frame writes lost; kinds:",
                    f124.size(), n124, compared124, skipped124, lost);
        for (const auto& k : drawn) std::printf(" %s=%d", k.first.c_str(), k.second);
        std::printf("\n");
    }
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

// The seeds beyond the nightly's range (area 5 round 9 review; ledger A3-45, A3-46, A4-10, A4-11), as the shrunk programs the review printed. They are
// the words the replay lines carry; each must come out with no finding, and the two table pins must have been read through the labels that are
// addresses (the check's `table.switch-on-address` count), so that an oracle which looks for the index's label only fails them.
void SetOrClear(const char* name, const char* value)
{
#ifdef _WIN32
    _putenv_s(name, value ? value : "");
#else
    if (value) setenv(name, value, 1);
    else unsetenv(name);
#endif
}

struct ModulePin
{
    const char* name;
    const char* words;
    const char* funcs;
    const char* tables;
    const char* returns;
    const char* clobbers;
    const char* reads;
    const char* data = nullptr;   // SE_GENERATED_DATA: the data ranges of the module (hex byte offsets, "44-46")
};

Subject ModuleSubject(const ModulePin& m)
{
    const char* names[] = { "SE_GENERATED_FUNCS", "SE_GENERATED_TABLES", "SE_GENERATED_RETURNS", "SE_GENERATED_CLOBBERS", "SE_GENERATED_READS",
                            "SE_GENERATED_VOLATILE", "SE_GENERATED_POLL", "SE_GENERATED_DATA" };
    const char* values[] = { m.funcs, m.tables, m.returns, m.clobbers, m.reads, nullptr, nullptr, m.data };
    std::string saved[8];
    bool had[8];
    for (int i = 0; i < 8; ++i)
    {
        const char* old = std::getenv(names[i]);
        had[i] = old != nullptr;
        if (old) saved[i] = old;
        SetOrClear(names[i], values[i]);
    }
    const Subject s = RawSubject(m.words);
    for (int i = 0; i < 8; ++i) SetOrClear(names[i], had[i] ? saved[i].c_str() : nullptr);
    return s;
}

void TestLeftoverSeedPins(Sh2Decompiler& d)
{
    // A3-45 (seed 22001): rotcr r0 in a do-while loop, r0's entry value reaches the result on every path: not lent. A3-46 (seed 24379): r0 is left
    // alone on the path that skips both writes, and the oracle must not count `and r0,r0` as the write that fills it (it was lent; since area 3
    // round 9 it is withheld, its return chosen by r0's entry value, `tst #0x80,r0`). No finding either way.
    for (const char* words : { "E301 4025 451B 4310 8BFB 000B 0009", "5264 C880 8900 200A 2028 8B00 2009 000B 0009",
                               // the same loop entered only if r4 is not zero (lent void as A3-14's scratch join until round 9, withheld since: r0 rotated
                               // on one path is a read of it)
                               "2448 8902 4025 4410 8BFC 000B 0009" })
    {
        const Report rep = EvaluateSubject(d, WordsSubject(words), true);
        std::printf("pin '%s': %zu finding(s)\n", words, rep.findings.size());
        for (const Finding& f : rep.findings) std::printf("  [%s] %s\n", f.sig.c_str(), f.detail.c_str());
        CHECK(rep.findings.empty());
    }
    static const ModulePin kTables[] = {
        { "A4-10, seed 33994 (div1 in a case)",
          "2F86 2F96 2FC6 0C2A 7FF0 6843 6953 604D 7F10 4C2A 6CF6 69F6 68F6 000B 0009 E301 A000 0009 4310 8BFD E103 3616 8913 4608 C701 016E 412B 0009 0600 9048 0600 "
          "904C 0600 9052 0600 9052 A005 0009 C604 A002 0009 A000 0009 E301 A000 0009 4310 8BFD 4515 8902 3154 A002 0009 A000 0009 E074 000B 0009",
          "0/0/1e,1e/1e/74", "34:6:4:56:48,4c,52,52", "1,1", "0,0", "0,20000" },
        { "A4-11, seed 50070 (stc sr in a case)",
          "2F86 2F96 2FA6 4F22 7FF8 6843 6953 6A63 D203 7F08 4F26 6AF6 69F6 68F6 422B 0009 0600 9024 E105 3616 891B 4608 C701 016E 412B 0009 0600 904C 0600 9050 0600 9054 "
          "0600 9054 0600 9058 0600 905C A009 0009 A007 0009 A005 0009 A003 0009 517F A000 0009 0202 6023 000B 0009",
          "0/0/20,24/24/6a", "30:6:6:62:4c,50,54,54,58,5c", "1,1", "41000,41000", "0,0" },
    };
    for (const ModulePin& m : kTables)
    {
        const Report rep = EvaluateSubject(d, ModuleSubject(m), true);
        const auto it = rep.counts.find("table.switch-on-address");
        const int onAddress = it == rep.counts.end() ? 0 : it->second;
        std::printf("pin '%s': %zu finding(s), switches labelled by address %d\n", m.name, rep.findings.size(), onAddress);
        for (const Finding& f : rep.findings) std::printf("  [%s] %s\n", f.sig.c_str(), f.detail.c_str());
        CHECK(rep.findings.empty());
        CHECK(onAddress > 0);
    }
}

// ---- Area 5, A5-82: what a pointer check may take for the pointer's derivation -------------------------------------------------------------
// The six seeds the pointer rules (3b, 3c) reported where the text was right: 64293 (the pointer is a callee's output merged with `&REG`, and the
// `&REG` of the same register is assigned after the read), 303290 and 202350 (a table the engine leaves as an indirect call: the cases' code, which
// set the pointer, is not in the text, and `param = &REG` stands after the access), 310654 and 405136 (a value stored, `*extraout_r7 = 0xffffff9d;`,
// is also an on-chip register's address), 313937 (the same in `return CONCAT44(*p, 0xffffffa5)`). Their shrunk words, run as modules: no finding,
// and the rule counted the derivation as not the pointer.
struct A582Pin
{
    ModulePin module;
    const char* count;   // the check's count that must be above 0: the derivation was seen and set aside
};

DecompileResult FakeOutput(const std::vector<std::string>& lines)
{
    DecompileResult r;
    for (size_t i = 0; i < lines.size(); ++i)
    {
        Line l;
        l.text = lines[i];
        Token t;
        t.begin = static_cast<uint16_t>(l.text.find_first_not_of(' '));
        t.end = static_cast<uint16_t>(l.text.size());
        t.addr = 0x06009000u + static_cast<uint32_t>(i) * 2;
        l.tokens.push_back(t);
        r.lines.push_back(l);
    }
    return r;
}

void TestA582Pins(Sh2Decompiler& d)
{
    static const A582Pin kPins[] = {
        { { "A5-82, seed 64293", "2FD6 6013 6DF6 000B 0009 2F86 2F96 2FA6 2FE6 4F22 7FF0 6EF3 6843 6953 6A63 B02D 0009 6483 6593 66A3 E105 3616 8916 4608 C701 016E 412B 0009 0600 9050 0600 9050 0600 9054 0600 905C 0600 9058 0600 9058 A005 0009 A003 0009 A001 0009 D705 D106 7E10 6FE3 4F26 6EF6 6AF6 69F6 68F6 412B 0009 0009 25B0 0428 0600 907C 7FE8 E103 3616 8913 4608 C702 016E 412B 0009 0009 0600 90A0 0600 90A4 0600 90A8 0600 90AC A006 0009 A004 0009 A002 0009 8577 D702 E07D 7F18 000B 0009 05F8 0066",
            "0/0/a,a/a/72,7c/7c/b8", "34:6:6:5c:50,50,54,5c,58,58;8a:6:4:ac:a0,a4,a8,ac", "1,1,1", "0,121000,101000", "20000,40000,800" }, "cache.read.pointer.unrelated" },
        { { "A5-82, seed 303290", "2F86 2F96 2FA6 4F22 6843 6953 6A63 E104 3417 8918 4408 C703 014E 412B 0009 0009 0600 903E 0600 9038 0600 903E 0600 9046 0600 9042 0600 9046 D60A A004 0009 A002 0009 A000 0009 1613 2558 8901 A003 E32B D605 A000 0009 6060 4F26 6AF6 69F6 68F6 000B 0009 25B0 0404 05F8 00D0",
            "0/0/64", "1a:4:5:46:38,3e,46,42,46:3e", "1", nullptr, nullptr }, "pointer.effect.unrelated" },
        { { "A5-82, seed 310654", "2F86 2F96 2FA6 2FC6 2FD6 0C2A 6843 6953 6A63 B00F 6593 6483 6593 66A3 D104 4C2A 6DF6 6CF6 6AF6 69F6 68F6 412B 0009 0009 0600 9034 2FD6 E29D 2722 D702 6DF6 000B 0009 0009 25F8 006C",
            "0/0/2e,34/34/42", nullptr, "0,0", "100000,100000", "0,0" }, "pointer.effect.unrelated" },
        { { "A5-82, seed 313937", "E103 3517 8914 4508 C702 015E 412B 0009 0600 9028 0600 9030 0600 9024 0600 9028 0600 902C A004 0009 A002 0009 A000 0009 2678 8F00 E1A5 000B 6050",
            "0/0/30,30/30/3a", "c:5:4:30:30,24,28,2c:28", "1,1", "80000,80000", "2,0" }, "pointer.effect.unrelated" },
        { { "A5-82, seed 405136", "2F86 2F96 4F22 7FD0 6843 6953 B00C 0009 6483 6593 D103 7F30 4F26 69F6 68F6 412B 0009 0009 0600 9028 E1E6 E008 0616 E106 3712 8919 4708 C702 017E 412B 0009 0009 0600 9058 0600 9068 0600 905C 0600 9068 0600 9060 0600 9064 A007 0009 A005 0009 A003 0009 A001 0009 D601 6013 000B 0009 05F8 007C",
            "0/0/22,28/28/70", "3a:7:6:68:58,68,5c,68,60,64", "1,1", "0,0", "20000,20000" }, "pointer.effect.unrelated" },
        { { "A5-85, seed 910697", "E105 3413 8916 4408 C702 014E 412B 0009 0600 902C 0600 9028 0600 9028 0600 902C 0600 902C 0600 9030 A004 0009 A002 0009 A000 0009 E103 3512 890F 4508 C701 015E 412B 0009 0600 9050 0600 9050 0600 9056 E1D3 A002 0009 A000 0009 000B 6040",
            "0/0/34,34/34/5e", "c:4:5:34:28,28,2c,2c,30:2c;40:5:3:5a:50,50,56", "1,1", "180000,180000", "0,20000" }, "pointer.effect.unrelated" },
        { { "A5-82, seed 202350 (specials)", "2F86 2F96 2FA6 2FB6 4F22 6843 6953 6A63 6B73 E106 3613 891A 4608 C703 016E 412B 0009 0009 0600 904A 0600 904E 0600 9040 0600 9046 0600 9046 0600 9046 0600 904A D70B A004 0009 A002 0009 A000 0009 6072 2448 8B01 A003 E11D D706 A000 E1C8 6073 4F26 6BF6 6AF6 69F6 68F6 000B 0009 0009 1DFE 0064 25F8 00D2",
            "0/0/6e", "1e:6:6:4e:4e,40,46,46,46,4a:4a", "1", "1000", "60000" }, "cache.read.pointer.unrelated" },
    };
    for (const A582Pin& p : kPins)
    {
        const Report rep = EvaluateSubject(d, ModuleSubject(p.module), true);
        const auto it = rep.counts.find(p.count);
        const int seen = it == rep.counts.end() ? 0 : it->second;
        std::printf("pin '%s': %zu finding(s), %s %d\n", p.module.name, rep.findings.size(), p.count, seen);
        for (const Finding& f : rep.findings) std::printf("  [%s] %s\n", f.sig.c_str(), f.detail.c_str());
        CHECK(rep.findings.empty());
        CHECK(seen > 0);
    }

    // The seeds themselves (the shrunk words above drop the assignment that stands after the read in 64293's text), and grammar 1's seeds 69 and 179,
    // whose pointer is a register merged with a hardware address no register is named at (0x05FE00DC, 0x35D000D8): the rule that narrowed the
    // derivation must still call them the pointer, and the checks on them (said) stand.
    struct SeedPin { const char* name; bool v2; uint64_t seed; const char* seen; const char* notSeen; };
    static const SeedPin kSeeds[] = {
        { "A5-82, grammar 2 seed 64293", true, 64293, "cache.read.pointer.unrelated", nullptr },
        { "A5-82, grammar 2 seed 303290", true, 303290, "pointer.effect.unrelated", nullptr },
        { "A5-82, grammar 2 seed 310654", true, 310654, "pointer.effect.unrelated", nullptr },
        { "A5-82, grammar 2 seed 313937", true, 313937, "pointer.effect.unrelated", nullptr },
        { "A5-82, grammar 2 seed 405136", true, 405136, "pointer.effect.unrelated", nullptr },
        { "A5-85, grammar 2 seed 910697", true, 910697, "pointer.effect.unrelated", nullptr },
        { "A5-82, grammar 1 seed 69 (control: a register merged with an unmapped address)", false, 69, "cache.read.pointer.said", "cache.read.pointer.unrelated" },
        { "A5-82, grammar 1 seed 179 (control: the same)", false, 179, "pointer.effect.derived", nullptr },
    };
    // (these seeds are programs of the default alphabet: under SE_GENERATED_ALPHABET=specials the same seed is another program, and the pin would
    // fail for a run that is not about it)
    const bool savedSpecials = gSpecialReads;
    gSpecialReads = false;
    for (const SeedPin& sp : kSeeds)
    {
        const Report rep = EvaluateSubject(d, MakeSubject(sp.v2 ? MakeV2(sp.seed) : Generate(sp.seed)), true);
        auto count = [&](const char* name) {
            const auto it = rep.counts.find(name);
            return it == rep.counts.end() ? 0 : it->second;
        };
        std::printf("pin '%s': %zu finding(s), %s %d, %s %d\n", sp.name, rep.findings.size(), sp.seen ? sp.seen : "-", sp.seen ? count(sp.seen) : 0,
                    sp.notSeen ? sp.notSeen : "-", sp.notSeen ? count(sp.notSeen) : 0);
        for (const Finding& f : rep.findings) std::printf("  [%s] %s\n", f.sig.c_str(), f.detail.c_str());
        CHECK(rep.findings.empty());
        if (sp.seen) CHECK(count(sp.seen) > 0);
        if (sp.notSeen) CHECK(count(sp.notSeen) == 0);
    }
    gSpecialReads = savedSpecials;

    // The derivation itself, on text: what counts as the pointer of a read of VDP2_TVSTAT (0x25F80004) and what does not.
    uint32_t tvstat = 0;
    CHECK(HardwareAlias(0x25F80004u, tvstat));
    const std::vector<const HardwareRegister*> none;
    auto check = [&](const char* what, const std::vector<std::string>& lines, size_t useLine, uint32_t reached, bool want, bool wantUnrelated) {
        const DecompileResult r = FakeOutput(lines);
        bool unrelated = false;
        const bool got = PointerPrintsFromHardware(r, 0x06009000u + static_cast<uint32_t>(useLine) * 2, none, reached, unrelated);
        std::printf("pin 'A5-82 derivation: %s': %s (want %s), set aside %s\n", what, got ? "pointer" : "not", want ? "pointer" : "not", unrelated ? "yes" : "no");
        CHECK(got == want);
        CHECK(unrelated == wantUnrelated);
    };
    check("the assignment above the read", { "puVar1 = &VDP2_TVSTAT;", "uVar2 = *puVar1;" }, 1, tvstat, true, false);
    check("the assignment below the read, in no loop", { "uVar2 = *puVar1;", "puVar1 = &VDP2_TVSTAT;" }, 0, tvstat, false, true);
    check("the assignment below the read, in a do-while", { "do {", "  uVar2 = *puVar1;", "  puVar1 = &VDP2_TVSTAT;", "} while (c);" }, 1, tvstat, true, false);
    check("the assignment below the read, before a backward goto", { "code_r0x06009100:", "uVar2 = *puVar1;", "puVar1 = &VDP2_TVSTAT;", "goto code_r0x06009100;" }, 1, tvstat, true, false);
    check("the assignment below the read, in a loop that does not hold the read", { "uVar2 = *puVar1;", "do {", "  puVar1 = &VDP2_TVSTAT;", "} while (c);" }, 0, tvstat, false, true);
    check("a register 60 bytes from the one reached", { "puVar1 = &VDP2_TVSTAT;", "uVar2 = puVar1[0x1e];" }, 1, tvstat + 0x3C, true, false);
    check("a register 68 bytes from the one reached", { "puVar1 = &VDP2_TVSTAT;", "uVar2 = puVar1[0x22];" }, 1, tvstat + 0x44, false, true);
    check("a stored value that is an on-chip address", { "*extraout_r7 = 0xffffffe6;" }, 0, tvstat, false, true);
    check("hardware no register is named at, where the run went", { "puVar1 = (uint4 *)0x5fe00dc;", "uVar2 = *puVar1;" }, 1, 0x05FE00DCu, true, false);
    // A5-85: the read's statement holds a value besides the dereference (`CONCAT44(*param_1, xVar1)`, the rts slot's read and the r1 half of the
    // return); what that value was assigned is not the pointer. The pointer in the dereference, or behind a cast of it, or a name indexed, still is.
    // (the read's own tokens, as the printer gives them to the rts slot's `mov.b @r4,r0` of seed 910697: the parentheses and the cast are no one's)
    auto checkOwn = [&](const char* what, const std::vector<std::string>& lines, const std::vector<std::string>& own, uint32_t reached, bool want,
                        bool wantUnrelated) {
        DecompileResult r = FakeOutput(lines);
        Line& l = r.lines.back();
        l.tokens.clear();
        size_t at = 0;
        for (const std::string& piece : own)
        {
            at = l.text.find(piece, at);
            CHECK(at != std::string::npos);
            if (at == std::string::npos) return;
            Token t;
            t.begin = static_cast<uint16_t>(at);
            t.end = static_cast<uint16_t>(at + piece.size());
            t.addr = 0x06009000u + static_cast<uint32_t>(lines.size() - 1) * 2;
            l.tokens.push_back(t);
            at += piece.size();
        }
        bool unrelated = false;
        const bool got = PointerPrintsFromHardware(r, 0x06009000u + static_cast<uint32_t>(lines.size() - 1) * 2, none, reached, unrelated);
        std::printf("pin 'A5-85 derivation: %s': %s (want %s), set aside %s\n", what, got ? "pointer" : "not", want ? "pointer" : "not", unrelated ? "yes" : "no");
        CHECK(got == want);
        CHECK(unrelated == wantUnrelated);
    };
    checkOwn("a value beside the dereference", { "xVar1 = &VDP2_TVSTAT;", "return CONCAT44((int4)*param_1,xVar1);" },
             { "return", "CONCAT44", "*", "param_1", ",", "xVar1", ";" }, tvstat, false, true);
    checkOwn("the pointer behind a cast, beside a value (control)", { "puVar1 = &VDP2_TVSTAT;", "return CONCAT44((int4)*puVar1,xVar2);" },
             { "return", "CONCAT44", "*", "puVar1", ",", "xVar2", ";" }, tvstat, true, false);
    checkOwn("a value beside, the pointer in the dereference too (control)", { "puVar1 = &VDP2_TVSTAT;", "xVar2 = &VDP2_TVMD;", "return CONCAT44((int4)*puVar1,xVar2);" },
             { "return", "CONCAT44", "*", "puVar1", ",", "xVar2", ";" }, tvstat, true, false);
    check("a cast pointer dereferenced (A5-85 control)", { "iVar1 = (int4)&VDP2_TVSTAT;", "uVar2 = *(uint4 *)(iVar1 + 0);" }, 1, tvstat, true, false);
    check("a product is no dereference (A5-85 control)", { "puVar1 = &VDP2_TVSTAT;", "uVar2 = puVar1[iVar3 * 2];" }, 1, tvstat, true, false);
    CHECK(DerefOperands("return CONCAT44((int4)*param_1,xVar1);") == "param_1 ; ");
    CHECK(DerefOperands("x = a * b;").empty());
    CHECK(DerefOperands("x = *(xunknown4 *)(p + 4) + y;") == "(xunknown4 *)(p + 4) + y ; ");
}

// ---- Area 2 round 7 (ledger A2-49, A2-50) ----------------------------------------------------------------------------------------------
// A2-49: seed 62956 of grammar 2, and the review's reduction of it: a function that calls a lent callee and returns its value, with `tst r0,r0 ;
// bt` over nothing in between, is printed and lent `void` (accepted: the compare is a use of r0 when the return is decided). It is counted, not a
// finding; the same function with the compare and the branch as nops returns r0 and is lent so (the control: the exclusion is the compare's).
// A2-50: the shrinker must keep what the generator declared. The review's shrunk replay of that seed has no call and no write of r0 at all, so
// the `returns` it is replayed with is false of it: no finding. And a shrink step that takes out the last call of a `return g(p);` function leaves
// the declaration (Func::retIsCall) with nothing to stand on: Shrinker::IntentHolds says so.
void TestRound7Pins(Sh2Decompiler& d)
{
    static const char* const kWords =
        "2F86 2F96 2FC6 0C2A 7FF8 0009 0009 2F36 0009 63F6 0009 0009 0009 0009 0009 0009 0009 0009 D309 430B 0009 0009 0009 2008 8903 0009 0009 0009 0009 0009 0009 0009 "
        "7F08 4C2A 6CF6 69F6 000B 68F6 0600 9054 0009 0009 7FC0 0009 0009 0009 0009 0009 0009 0009 0009 0009 0009 0009 624C 0009 0009 7F40 000B 6023";
    const ModulePin dead = { "A2-49: tst r0,r0 ; bt over nothing after a lent call", kWords, "0/0/4c,54/54/78", nullptr, "1,1", nullptr, nullptr };
    {
        const Report rep = EvaluateSubject(d, ModuleSubject(dead), true);
        const auto it = rep.counts.find("excluded.call-result-compared.return-dropped");
        const int excluded = it == rep.counts.end() ? 0 : it->second;
        std::printf("pin '%s': %zu finding(s), excluded %d\n", dead.name, rep.findings.size(), excluded);
        for (const Finding& f : rep.findings) std::printf("  [%s] %s\n", f.sig.c_str(), f.detail.c_str());
        CHECK(rep.findings.empty());
        CHECK(excluded > 0);
    }
    {
        // the seed itself: arms with dead writes, not nops
        const Report rep = EvaluateSubject(d, MakeSubject(MakeV2(62956)), true);
        const auto it = rep.counts.find("excluded.call-result-compared.return-dropped");
        const int excluded = it == rep.counts.end() ? 0 : it->second;
        std::printf("pin 'A2-49: seed 62956': %zu finding(s), excluded %d\n", rep.findings.size(), excluded);
        for (const Finding& f : rep.findings) std::printf("  [%s] %s\n", f.sig.c_str(), f.detail.c_str());
        CHECK(rep.findings.empty());
        CHECK(excluded > 0);
    }
    {
        // the control: the compare and the branch are nops; the function returns the callee's value, the prototype says so, nothing is excluded
        std::string words = kWords;
        const size_t at = words.find("2008 8903");
        CHECK(at != std::string::npos);
        words.replace(at, 9, "0009 0009");
        const ModulePin control = { "A2-49 control: the same without the compare", words.c_str(), dead.funcs, nullptr, "1,1", nullptr, nullptr };
        const Report rep = EvaluateSubject(d, ModuleSubject(control), true);
        const auto it = rep.counts.find("excluded.call-result-compared.return-dropped");
        const int excluded = it == rep.counts.end() ? 0 : it->second;
        std::printf("pin '%s': %zu finding(s), excluded %d\n", control.name, rep.findings.size(), excluded);
        for (const Finding& f : rep.findings) std::printf("  [%s] %s\n", f.sig.c_str(), f.detail.c_str());
        CHECK(rep.findings.empty());
        CHECK(excluded == 0);
    }
    {
        // A2-50: the first shrink of that seed, replayed with the `returns` it was drawn with: r0 is written by nothing, the lent `void` is right
        const ModulePin shrunk = { "A2-50: the shrunk replay of seed 62956", "2F86 2F96 2FC6 0C2A 7FF8 6843 6953 7F08 4C2A 6CF6 69F6 68F6 000B 0009", "0/0", nullptr, "1", nullptr, nullptr };
        const Report rep = EvaluateSubject(d, ModuleSubject(shrunk), true);
        std::printf("pin '%s': %zu finding(s)\n", shrunk.name, rep.findings.size());
        for (const Finding& f : rep.findings) std::printf("  [%s] %s\n", f.sig.c_str(), f.detail.c_str());
        CHECK(rep.findings.empty());
    }
    {
        Program p = MakeV2(62956);
        CHECK(Shrinker::IntentHolds(p));
        bool had = false;
        for (Func& f : p.fns)
        {
            if (!f.retIsCall || f.items.empty() || f.items.back().type != Item::Call) continue;
            had = true;
            // every call of the functions that return one's value out (the shrinker's removal of the call, however many there were)
            Program q = p;
            for (Func& g : q.fns)
                if (g.retIsCall)
                    g.items.erase(std::remove_if(g.items.begin(), g.items.end(), [](const Item& it) { return it.type == Item::Call; }), g.items.end());
            CHECK(!Shrinker::IntentHolds(q));
            // ...and the call kept, but another item after it
            Program r = p;
            for (Func& g : r.fns)
                if (g.retIsCall && !g.items.empty() && g.items.back().type == Item::Call)
                {
                    Item extra = g.items.back();
                    extra.type = Item::Skip;
                    g.items.push_back(extra);
                }
            CHECK(!Shrinker::IntentHolds(r));
            break;
        }
        std::printf("pin 'A2-50: IntentHolds': seed 62956 has a function that returns its last call's value: %s\n", had ? "yes" : "no");
        CHECK(had);
    }
    {
        // The shrinker itself: every step is accepted but the one that gives up `return g(p);` (the flag stays), so only the intent check can
        // keep the call. Without it the call goes first and what is left has the flag and nothing to stand on.
        Program start = MakeV2(62956);
        int flagged = 0;
        for (const Func& f : start.fns) flagged += f.retIsCall ? 1 : 0;
        Shrinker s{ d, start, "" };
        s.accept = [flagged](const Program& q) {
            int now = 0;
            for (const Func& f : q.fns) now += f.retIsCall ? 1 : 0;
            return now == flagged;
        };
        s.budget = 5000;
        const Program small = s.Run();
        std::printf("pin 'A2-50: shrinker': %d steps, %zu function(s) left, %d return(s) of a call's value\n", s.evals, small.fns.size(), flagged);
        CHECK(flagged > 0);
        CHECK(Shrinker::IntentHolds(small));
    }
}

// ---- The canary family (ledger A3-34, A3-36, A3-37, A3-39, A3-40) ---------------------------------------------------------------------
// The grammar never hands a callee the address of a local, nor reads a word a callee wrote, so a text that folds a local across a call (the
// engine's own reading of what a call may write) is invisible to every other check: the function still returns a value, the registers still
// agree, and the accesses are the same. Here the function stores a canary, 0x5A, in a local, calls a writer that overwrites that local through
// a pointer or through its own stack pointer with 0x37 (or with r5 + r4, r5), and returns the local. The run returns the writer's value; a
// text that folds the local reads 0x5A. So the check is on the printed text only: no `return` line contains the canary, when no run
// returned it. The family is enumerated, not drawn from the grammar: six kinds, each over the shapes the review rounds found a hole in --
// the frame address exact, plus an index, minus an index, in r4 (a writer lent or one that reads r1 and is not) or in r2; the frame in
// r15 or r14; the call by bsr or by jsr through a literal; none to two earlier calls (to a callee lent or not); the callee that stores
// through r15 plus an index, a computed value in the caller's word, and a callee that hands its own entry r15 + 4 to a writer.
constexpr uint32_t kCanary = 0x5A, kCanaryWritten = 0x37;

struct CanaryAsm
{
    std::vector<uint16_t> w;
    int literals = 0;
    std::vector<std::pair<uint32_t, uint32_t>> literalValues;   // (byte offset from kCode, value)
    static constexpr uint32_t kPool = 0x60;
    void Put(uint16_t x) { w.push_back(x); }
    uint32_t Here() const { return static_cast<uint32_t>(2 * w.size()); }
    void Bsr(uint32_t target)
    {
        const int32_t disp = (static_cast<int32_t>(target) - static_cast<int32_t>(Here() + 4)) / 2;
        Put(static_cast<uint16_t>(0xB000 | (disp & 0xFFF)));
    }
    void LoadLiteral(int rn, uint32_t value)
    {
        const uint32_t at = kPool + 4u * static_cast<uint32_t>(literals++);
        literalValues.emplace_back(at, value);
        const uint32_t base = (Here() + 4) & ~3u;
        Put(static_cast<uint16_t>(0xD000 | rn << 8 | ((at - base) / 4)));
    }
    // A call: `bsr` or `mov.l @(L,pc),r3 ; jsr @r3`, then the delay slot (nop when empty).
    void Call(uint32_t target, bool jsr, uint16_t slot = 0x0009)
    {
        if (jsr)
        {
            LoadLiteral(3, kCode + target);
            Put(0x430B);
        }
        else Bsr(target);
        Put(slot);
    }
};

Subject MakeCanarySubject(int variant, std::string& label)
{
    Rng r(0xCA4A41ull + static_cast<uint64_t>(variant) * 7919u);
    // Round 8 (A3-44) adds the frame address in r5, r6 or r7 (a lent writer), and handed through a word of memory the writer loads it from
    // (Cell), and three dimensions drawn from a stream of their own (so the older variants draw what they drew): the result used (`add #1,r0`),
    // and the call under a branch or in a loop that runs twice. Round 8 (A3-51) adds the address in the call's stack argument slot (Stack): stored at the
    // call's r15 with r4-r7 untouched, which the engine drops from the call, for a callee that reads it (so is never lent) and writes through it.
    enum Kind { Lent4, Unlent4, Unlent2, Indexed, Computed, Nested, Lent5, Lent6, Lent7, Cell, Stack, kKinds };
    const Kind kind = static_cast<Kind>(variant % kKinds);
    Rng x(0xCA4A42ull + static_cast<uint64_t>(variant) * 104729u);
    const bool plus1 = x.Chance(30);
    int shape = static_cast<int>(x.Below(3));   // 0 straight, 1 `if (r4 != 0)` around the call, 2 a loop of two calls (r13 the counter)
    const bool fp = (kind <= Unlent2 || kind >= Lent5) && r.Chance(40);
    const bool jsr = r.Chance(50);
    const int pre = static_cast<int>(r.Below(3));
    const int form = static_cast<int>(r.Below(3));   // 0 exact, 1 plus a negative index, 2 minus a positive one
    const uint32_t kWriter = 0x80, kKeepLent = 0xA0, kKeepUnlent = 0xC0, kIndexWord = 0x100, kCell = 0x10C;
    CanaryAsm a;
    // (the callee that is not lent stores r1, which the caller sets, through r4: a read of r1 that is live, so the signature is withheld; a
    // copy of r1 to a scratch register was dead code and the callee was lent)
    std::vector<uint16_t> writer, keepLent = { 0x000B, 0x0009 }, keepUnlent = { 0x2412, 0x000B, 0x0009 };
    const char* names[] = { "r4 lent", "r4 unlent", "r2 unlent", "indexed", "computed", "nested", "r5 lent", "r6 lent", "r7 lent", "cell", "stack slot" };
    const uint16_t seven = static_cast<uint16_t>(0xE100 | kCanaryWritten);   // mov #0x37,r1
    // The functions after the caller: the writer, a callee that keeps everything and one that reads r1 (so is not lent), and for the nested
    // kind the lent writer that the first one calls.
    std::vector<std::pair<uint32_t, std::vector<uint16_t>>> others;
    const uint32_t kInner = 0xE0;
    std::vector<uint16_t> inner;
    switch (kind)
    {
    case Lent4: writer = { seven, 0x000B, 0x2412 }; break;                   // mov #K,r1 ; rts ; mov.l r1,@r4
    case Lent5: writer = { seven, 0x000B, 0x2512 }; break;                   // ... mov.l r1,@r5
    case Lent6: writer = { seven, 0x000B, 0x2612 }; break;
    case Lent7: writer = { seven, 0x000B, 0x2712 }; break;
    case Cell:
        // mov.l @(3,pc),r2 ; mov.l @r2,r2 ; mov #K,r1 ; rts ; mov.l r1,@r2 ... the word the caller left its frame address in (the pool at 0x90)
        writer = { 0xD203, 0x6222, seven, 0x000B, 0x2212, 0x0009, 0x0009, 0x0009,
                   static_cast<uint16_t>((kCode + kCell) >> 16), static_cast<uint16_t>(kCode + kCell) };
        break;
    case Stack: writer = { 0x62F2, seven, 0x000B, 0x2212 }; break;           // mov.l @r15,r2 ; mov #K,r1 ; rts ; mov.l r1,@r2: the address from the stack slot
    case Unlent4: writer = { 0x2412, 0x000B, 0x0009 }; break;                // mov.l r1,@r4 ; rts ; nop  (r1 is the caller's)
    case Unlent2: writer = { 0x2212, 0x000B, 0x0009 }; break;                // mov.l r1,@r2
    case Indexed: writer = r.Chance(50) ? std::vector<uint16_t>{ 0x61F3, 0x316C, 0x000B, 0x2152 }    // mov r15,r1 ; add r6,r1 ; rts ; mov.l r5,@r1
                                        : std::vector<uint16_t>{ 0x6063, 0x000B, 0x0F56 };            // mov r6,r0 ; rts ; mov.l r5,@(r0,r15)
        break;
    case Computed: writer = { 0x6143, 0x315C, 0x2F12, 0x000B, 0x6013 }; break;   // r1 = r4 + r5 ; mov.l r1,@r15 ; rts ; mov r1,r0
    case Nested:
    {
        // g: sts.l pr,@-r15 ; mov r15,r4 ; add #4,r4 ; bsr h ; nop ; lds.l @r15+,pr ; rts ; nop, with h the lent writer
        const int32_t disp = (static_cast<int32_t>(kInner) - static_cast<int32_t>(kWriter + 2 * 3 + 4)) / 2;
        writer = { 0x4F22, 0x64F3, 0x7404, static_cast<uint16_t>(0xB000 | (disp & 0xFFF)), 0x0009, 0x4F26, 0x000B, 0x0009 };
        inner = { seven, 0x000B, 0x2412 };
        break;
    }
    default: break;
    }
    others.emplace_back(kWriter, writer);
    others.emplace_back(kKeepLent, keepLent);
    others.emplace_back(kKeepUnlent, keepUnlent);
    if (!inner.empty()) others.emplace_back(kInner, inner);   // last: the harness decompiles the last function first, so g finds h lent
    const bool localKind = kind <= Unlent2 || kind >= Lent5;
    if (!localKind || fp) shape = shape == 2 ? 0 : shape;   // the loop counter needs a push of its own, which the frame pointer variants do not have
    if (!localKind) shape = 0;
    const int base = fp ? 14 : 15;
    // Prologue.
    if (localKind)
    {
        if (shape == 2) a.Put(0x2FD6);      // mov.l r13,@-r15
        if (fp) a.Put(0x2FE6);              // mov.l r14,@-r15
        a.Put(0x4F22);                      // sts.l pr,@-r15
        a.Put(0x7FF8);                      // add #-8,r15
        if (fp) a.Put(0x6EF3);              // mov r15,r14
        a.Put(static_cast<uint16_t>(0xE000 | kCanary));                 // mov #C,r0
        a.Put(fp ? 0x2E02 : 0x2F02);                                    // mov.l r0,@base
        a.Put(fp ? 0x1E01 : 0x1F01);                                    // mov.l r0,@(4,base)
    }
    else
    {
        a.Put(0x2FE6);                      // mov.l r14,@-r15
        a.Put(0x0E2A);                      // sts pr,r14
        a.Put(0x7FFC);                      // add #-4,r15
        a.Put(static_cast<uint16_t>(0xE000 | kCanary));
        a.Put(0x2F02);                      // mov.l r0,@r15: the word the callee will write
    }
    for (int i = 0; i < pre; ++i)
    {
        const bool lent = r.Chance(50);
        a.Put(0xE101);                      // mov #1,r1 (an unlent callee reads it)
        a.Call(lent ? kKeepLent : kKeepUnlent, r.Chance(50));
    }
    // The call under `if (r4 != 0)` (r4 is a pointer in every run): the local is read back on that path only, the other returns 0. Or in a loop
    // that runs twice (r13 the counter, pushed above): the local is read back after it.
    size_t btAt = 0;
    uint32_t loopTop = 0;
    if (shape == 1)
    {
        a.Put(0x2448);                      // tst r4,r4
        btAt = a.w.size();
        a.Put(0x8900);                      // bt SKIP (patched below)
    }
    if (shape == 2)
    {
        a.Put(0xED02);                      // mov #2,r13
        loopTop = a.Here();
    }
    uint16_t slot = 0x0009;
    switch (kind)
    {
    case Lent4:
    case Unlent4:
    case Unlent2:
    case Lent5:
    case Lent6:
    case Lent7:
    {
        const int reg = kind == Unlent2 ? 2 : kind == Lent5 ? 5 : kind == Lent6 ? 6 : kind == Lent7 ? 7 : 4;
        if (kind == Unlent4) a.Put(seven);
        if (kind == Unlent2) a.Put(seven);
        if (form == 0 || kind == Unlent2 || kind >= Lent5)
            a.Put(static_cast<uint16_t>(0x6003 | reg << 8 | base << 4));                    // mov base,Rn
        else
        {
            // The index is read from a word of the image, which the engine takes for ordinary memory (a constant it could fold would
            // make the address exact): -4 reaches the lower local from base + 4, and so does 4 taken away from it.
            a.LoadLiteral(6, kCode + kIndexWord);                                           // mov.l @(L,pc),r6
            a.Put(0x6562);                                                                  // mov.l @r6,r5
            a.Put(static_cast<uint16_t>(0x6003 | reg << 8 | base << 4));
            a.Put(static_cast<uint16_t>(0x7000 | reg << 8 | 4));                             // add #4,Rn
            a.Put(static_cast<uint16_t>((form == 1 ? 0x300C : 0x3008) | reg << 8 | 5 << 4)); // add / sub r5,Rn
        }
        break;
    }
    case Stack:
        // The address of the second local (base + 4) in the word at the call's r15, the first local's: r4-r7 stay as the caller got them, so the engine
        // sees no argument at all and, without the probe's note, folds the second local across the call. That local is the result (read below).
        a.Put(static_cast<uint16_t>(0x6003 | 1 << 8 | base << 4));                          // mov base,r1
        a.Put(0x7104);                                                                      // add #4,r1
        a.Put(0x2F12);                                                                      // mov.l r1,@r15
        break;
    case Cell:
        a.Put(static_cast<uint16_t>(0x6003 | 1 << 8 | base << 4));                          // mov base,r1
        a.LoadLiteral(6, kCode + kCell);                                                    // mov.l @(L,pc),r6
        a.Put(0x2612);                                                                      // mov.l r1,@r6: the frame address, in a word of memory
        break;
    case Indexed: slot = 0xE600; break;     // mov #0,r6 in the slot
    default: break;
    }
    a.Call(kWriter, jsr, slot);
    if (shape == 2)
    {
        a.Put(0x4D10);                      // dt r13
        const int disp = (static_cast<int>(loopTop) - static_cast<int>(a.Here() + 4)) / 2;
        a.Put(static_cast<uint16_t>(0x8B00 | (disp & 0xFF)));   // bf loop
    }
    // The local back as the result (plus one, when the result is used).
    if (localKind) a.Put(kind == Stack ? (fp ? 0x50E1 : 0x50F1) : (fp ? 0x50E0 : 0x50F0));   // mov.l @(0,base),r0 (the second local, @(4,base), for Stack)
    else a.Put(0x60F2);                           // mov.l @r15,r0
    if (plus1) a.Put(0x7001);                     // add #1,r0
    if (shape == 1)
    {
        const size_t braAt = a.w.size();
        a.Put(0xA000);                      // bra END (patched)
        a.Put(0x0009);
        const uint32_t skipAt = a.Here();
        a.Put(0xE000);                      // SKIP: mov #0,r0
        a.w[btAt] = static_cast<uint16_t>(0x8900 | (((static_cast<int>(skipAt) - static_cast<int>(2 * btAt + 4)) / 2) & 0xFF));
        a.w[braAt] = static_cast<uint16_t>(0xA000 | (((static_cast<int>(a.Here()) - static_cast<int>(2 * braAt + 4)) / 2) & 0xFFF));
    }
    if (localKind)
    {
        if (fp)
        {
            a.Put(0x7E08);                  // add #8,r14
            a.Put(0x6FE3);                  // mov r14,r15
            a.Put(0x4F26);                  // lds.l @r15+,pr
            a.Put(0x000B);                  // rts
            a.Put(0x6EF6);                  //   mov.l @r15+,r14
        }
        else
        {
            a.Put(0x7F08);                  // add #8,r15
            a.Put(0x4F26);
            a.Put(0x000B);
            a.Put(shape == 2 ? 0x6DF6 : 0x0009);   //   mov.l @r15+,r13 (the loop's counter) or nop
        }
    }
    else
    {
        a.Put(0x7F04);                      // add #4,r15
        a.Put(0x4E2A);                      // lds r14,pr
        a.Put(0x000B);
        a.Put(0x6EF6);                      //   mov.l @r15+,r14
    }
    const uint32_t callerEnd = a.Here();
    std::vector<uint16_t> words = a.w;
    words.resize(0x110 / 2, 0x0009);
    words[kIndexWord / 2] = form == 1 ? 0xFFFF : 0x0000;       // the index: -4 (plus) or 4 (minus)
    words[kIndexWord / 2 + 1] = form == 1 ? 0xFFFC : 0x0004;
    for (const auto& lv : a.literalValues)
    {
        words[lv.first / 2] = static_cast<uint16_t>(lv.second >> 16);
        words[lv.first / 2 + 1] = static_cast<uint16_t>(lv.second);
    }
    Subject s;
    s.v2 = true;
    FnInfo caller;
    caller.layout.start = caller.layout.entry = kCode;
    caller.layout.end = kCode + callerEnd;
    caller.intent = caller.returns = true;
    caller.canary = kCanary;
    caller.canary2 = plus1 ? kCanary + 1 : 0;
    caller.canaryIfLent = !localKind;
    s.fns.push_back(caller);
    for (const auto& o : others)
    {
        for (size_t i = 0; i < o.second.size(); ++i) words[o.first / 2 + i] = o.second[i];
        FnInfo f;
        f.layout.start = f.layout.entry = kCode + o.first;
        f.layout.end = kCode + o.first + 2 * static_cast<uint32_t>(o.second.size());
        f.intent = true;
        f.returns = kind == Computed && o.first == kWriter;   // the others leave r0 alone
        s.fns.push_back(f);
    }
    s.words = words;
    s.codeWords = words.size();
    // The callers of the callees are decompiled after them: the harness lends in the order of the functions, last first.
    char buf[160];   // the longest text is 110 characters
    std::snprintf(buf, sizeof buf, "canary %d: %s, %s%s, %d earlier call(s)%s%s%s", variant, names[kind], fp ? "frame pointer, " : "", jsr ? "jsr" : "bsr", pre,
                  kind <= Unlent2 ? (form == 0 ? ", exact address" : form == 1 ? ", plus an index" : ", minus an index") : "",
                  shape == 1 ? ", under a branch" : shape == 2 ? ", in a loop" : "", plus1 ? ", result plus one" : "");
    label = buf;
    return s;
}

// ---- Area 3 round 8 (ledger A3-49, A3-50, A3-52, A3-53, A3-54) ------------------------------------------------------------------------
// A3-49, A3-50: a join of r0's entry value with a parameter, or r0 computed from its entry value on one path, where another alternative is a word from
// memory or a literal: lent `void` before the fix, withheld now. Seeds 112124, 117195, 400533, 66914, 77197 (grammar 2), 202318 and 207676 (grammar 2, specials)
// and the review's fixtures as words (V8, V10, W1, W3). A3-52: four seeds whose function runs on into the next, whose read of r14 or GBR comes after
// the call (60538, 308527, 405351, 413608, 69133, 69493; the review's shrunk module of 308527 has two functions at one address, an artefact of the shrinker, so the seeds
// are the pins). A3-53: seed 400785 beside 62956 (TestRound7Pins): the A2-49 exclusion covers both. A3-54: the claims check on synthetic runs, each claim broken one way.
void TestRound8Pins(Sh2Decompiler& d)
{
    auto report = [&](const char* name, const Report& rep, size_t want = 0) {
        std::printf("pin '%s': %zu finding(s)\n", name, rep.findings.size());
        for (const Finding& f : rep.findings) std::printf("  [%s] %s\n", f.sig.c_str(), f.detail.c_str());
        CHECK(rep.findings.size() == want);
    };
    for (uint64_t seed : { 112124ull, 117195ull, 400533ull, 66914ull, 77197ull, 60538ull, 308527ull, 405351ull, 413608ull, 69133ull, 69493ull })
        report(("A3-49/52 seed " + std::to_string(seed)).c_str(), EvaluateSubject(d, MakeSubject(MakeV2(seed)), true));
    {
        const bool saved = gSpecialReads;
        gSpecialReads = true;
        for (uint64_t seed : { 202318ull, 207676ull })
            report(("A3-49 seed " + std::to_string(seed) + " (specials)").c_str(), EvaluateSubject(d, MakeSubject(MakeV2(seed)), true));
        gSpecialReads = saved;
    }
    {
        // 400785: the call's result compared and left in r0 (A3-41): counted, not a finding
        const Report rep = EvaluateSubject(d, MakeSubject(MakeV2(400785)), true);
        const auto it = rep.counts.find("excluded.call-result-compared.return-dropped");
        report("A3-53 seed 400785", rep);
        CHECK(it != rep.counts.end() && it->second > 0);
    }
    {
        // A2-52: the register-only grammar draws sums of scaled terms (RuleCollectTerms' input) and the harness names the engine's wider-than-symbol
        // locals at the width of the write (A2-57). The draw is from a stream of its own, so SE_GENERATED_PRINTEDC_TERMS=0 gives the functions of
        // the rounds before; with it on, three seeds in ten have the item.
        int withTerms = 0, total = 0;
        for (long seed = 1; seed <= 600; ++seed)
        {
            const RegFn f = GenRegFn(static_cast<uint64_t>(seed), 0);
            ++total;
            for (const RegItem& it : f.items)
                if (it.shape == "terms") ++withTerms;
        }
        std::printf("pin 'A2-52: the register-only grammar draws sums of scaled terms': %d of %d functions\n", withTerms, total);
        CHECK(withTerms >= 100 && withTerms <= 260);
        // A2-58 and A2-59: the joins of equal computations and the loops that AND an accumulator with the pieces of a register, from a stream of
        // their own (SE_GENERATED_PRINTEDC_JOINS=0 gives round 8's functions): about 15 and 10 in a hundred.
        int withJoin = 0, withLoop = 0;
        for (long seed = 1; seed <= 600; ++seed)
            for (const RegItem& it : GenRegFn(static_cast<uint64_t>(seed), 0).items)
            {
                withJoin += it.shape == "join";
                withLoop += it.shape == "loopand";
            }
        std::printf("pin 'A2-58, A2-59: the register-only grammar draws joins and loops': %d and %d of %d functions\n", withJoin, withLoop, total);
        CHECK(withJoin >= 50 && withJoin <= 140);
        CHECK(withLoop >= 30 && withLoop <= 100);
        CHECK(UnderscoreNameType("  _Stack_4 = CONCAT31(Stack_4,param_2);", "_Stack_4") == "xunknown4");
        CHECK(UnderscoreNameType("  _Stack_4 = CONCAT44(a,b);", "_Stack_4") == "xunknown8");
        CHECK(UnderscoreNameType("  _Stack_4 = CONCAT11(a,b);", "_Stack_4") == "xunknown2");
        CHECK(UnderscoreNameType("  _Stack_4 = CONCAT31(a,b); _Stack_4 = CONCAT22(c,d); _Stack_4 = CONCAT11(e,f);", "_Stack_4") == "unsigned long long");   // two widths
        CHECK(UnderscoreNameType("  x = _Stack_4;", "_Stack_4") == "unsigned long long");   // no CONCAT writes it
    }
    static const ModulePin kWords[] = {
        { "A3-49 a (V8): r0 joins r6 and a loaded word", "2448 8903 6063 2558 8900 6052 000B 0009", "0/0/10", nullptr, "2", nullptr, nullptr },
        { "A3-49 b (V10): r0 joins r6 and a literal", "2448 8903 6063 2558 8900 D002 000B 0009 0009 0009 0000 1038", "0/0/10", nullptr, "2", nullptr, nullptr },
        { "A3-50 a (W1): r0 + a loaded word on one path", "2448 8901 6152 301C 000B 0009", "0/0/c", nullptr, "1", nullptr, nullptr },
        { "A3-50 b (W3): r0 + a literal on one path", "2448 8901 D102 301C 000B 0009 0009 0009 0000 1038", "0/0/c", nullptr, "1", nullptr, nullptr },
    };
    for (const ModulePin& m : kWords) report(m.name, EvaluateSubject(d, ModuleSubject(m), true));
    {
        // The claims check is wired into CheckFn: with the summaries damaged (SE_GENERATED_MUTATE=claims) some generated module reports it.
        const std::string saved = gMutateKind;
        const bool savedFlag = gMutate;
        gMutateKind = "claims";
        gMutate = true;
        bool reported = false;
        for (uint64_t seed = 2; seed <= 60 && !reported; seed += 2)
            for (const Finding& f : EvaluateSubject(d, MakeSubject(MakeV2(seed)), false).findings)
                reported = reported || f.sig.find("claim-wrong") != std::string::npos;
        gMutateKind = saved;
        gMutate = savedFlag;
        std::printf("pin 'A3-54: the claims check is called': %s\n", reported ? "reported" : "NOT reported");
        CHECK(reported);
    }
    {
        // The claims check (check 10b) on a run made by hand: each claim of the summary broken in the one way the run shows.
        RunOut run;
        run.finished = true;
        for (int r = 0; r < 16; ++r) run.init[r] = run.fin[r] = 0x1000u + r;
        run.init[15] = run.fin[15] = kStackTop;
        auto stores = [&](uint32_t addr) {
            TraceRec t;
            t.insn = kCode;
            t.write = true;
            t.size = 4;
            t.addr = addr;
            RunOut r = run;
            r.trace.push_back(t);
            return r;
        };
        CallEffects none;   // a function that preserves everything and stores nowhere
        none.preserved = (1u << 22) - 1;
        none.storesThrough = 0;
        none.outOrigins = 0;
        none.leaks = 0;
        none.frameValues = 0;
        none.frameWords = 0;
        none.spDelta = 0;
        std::string sig;
        CHECK(EffectClaimsContradicted(none, run, &sig).empty());
        // a store into r5's arena: needs r5's bit, or memory
        CHECK(!EffectClaimsContradicted(none, stores(kScratch5 + 8), &sig).empty() && sig == "claim-wrong:storesThrough");
        CallEffects viaR5 = none;
        viaR5.storesThrough = 1u << 5;
        CHECK(EffectClaimsContradicted(viaR5, stores(kScratch5 + 8)).empty());
        CHECK(!EffectClaimsContradicted(viaR5, stores(kScratch4 + 8)).empty());   // r4's arena is not r5's
        CallEffects viaMemory = none;
        viaMemory.storesThrough = origin::kMemory;
        CHECK(EffectClaimsContradicted(viaMemory, stores(kScratch4 + 8)).empty());
        // (round 11 fix) a store below r5's start, through r5: needs r5 in storesBelow
        {
            RunOut below = stores(kScratch5 - 8);
            below.init[5] = kScratch5;
            below.use.readFirst |= 1u << 5;
            CallEffects through = viaR5;
            through.storesBelow = 0;
            CHECK(!EffectClaimsContradicted(through, below, &sig).empty() && sig == "claim-wrong:storesBelow");
            through.storesBelow = 1u << 5;
            CHECK(EffectClaimsContradicted(through, below).empty());
            CallEffects atOrAbove = viaR5;
            atOrAbove.storesBelow = 0;
            RunOut above = stores(kScratch5 + 8);
            above.init[5] = kScratch5;
            above.use.readFirst |= 1u << 5;
            CHECK(EffectClaimsContradicted(atOrAbove, above).empty());   // at or above the start: no claim needed
        }
        // a store at or above the entry r15: needs kFrame and its word in frameWords (or kFrameAny)
        CHECK(!EffectClaimsContradicted(none, stores(kStackTop + 8), &sig).empty() && sig == "claim-wrong:storesThrough");
        CallEffects frame = none;
        frame.storesThrough = origin::kFrame;
        CHECK(!EffectClaimsContradicted(frame, stores(kStackTop + 8), &sig).empty() && sig == "claim-wrong:frameWords");
        frame.frameWords = 1u << 2;
        CHECK(EffectClaimsContradicted(frame, stores(kStackTop + 8)).empty());
        CHECK(!EffectClaimsContradicted(frame, stores(kStackTop + 12)).empty());   // word 3, not named
        frame.frameWords = CallEffects::kFrameAny;
        CHECK(EffectClaimsContradicted(frame, stores(kStackTop + 12)).empty());
        CHECK(EffectClaimsContradicted(none, stores(kStackTop - 8)).empty());   // the function's own frame is not asked about
        // a register that does not end as it began and holds a pointer into an arena, or into the stack
        RunOut ptr = run;
        ptr.fin[1] = kScratch6 + 4;
        CallEffects changes = none;
        changes.preserved &= ~2u;
        CHECK(!EffectClaimsContradicted(changes, ptr, &sig).empty() && sig == "claim-wrong:outOrigins");
        changes.outOrigins = 1u << 6;
        CHECK(EffectClaimsContradicted(changes, ptr).empty());
        changes.outOrigins = 1u << 5;   // another register's arithmetic can make the value (seed 12548): not contradicted
        CHECK(EffectClaimsContradicted(changes, ptr).empty());
        RunOut sp = run;
        sp.fin[2] = kStackTop - 16;
        CallEffects stack = none;
        stack.preserved &= ~4u;
        CHECK(!EffectClaimsContradicted(stack, sp, &sig).empty() && sig == "claim-wrong:outOrigins");
        stack.outOrigins = origin::kStack;
        CHECK(EffectClaimsContradicted(stack, sp).empty());
        // r15 ends where the summary says, or not
        RunOut popped = run;
        popped.fin[15] = kStackTop + 8;
        CallEffects pops = none;
        pops.preserved &= ~(1u << 15);
        pops.spDelta = 8;
        CHECK(EffectClaimsContradicted(pops, popped).empty());
        pops.spDelta = 4;
        CHECK(!EffectClaimsContradicted(pops, popped, &sig).empty() && sig == "claim-wrong:spDelta");
    }
}

// ---- The routed seeds: A3-55, A4-16, A4-17, A5-83 (found by the area 2 round 8 review beyond the nightly) ----------------------------------------
// 519315 (grammar 2): A3-49's literal variant, withheld since cee85f5. 608802 (grammar 1, specials): a loaded word or a constant, chosen by r3, which no
// signature passes; the engine drops the return and the read of r3 with it (the host now withholds: "chosen by r3"). 511038 (grammar 2): a switch whose
// index is a constant its guard excludes, read as a table and followed into uncaptured memory; the actions remove that block, and the function is no
// longer reported cut off by it. 609269 (grammar 2, specials): an unrecovered signed table hands the run to the cases, which come back to an instruction
// the text holds below the jump: not a join across a branch (HandedOverAt). 603025 (grammar 1): a pointer stepped and flipped between the partitions
// from an SCSP slot register no name is near: the cached reads are called cache reads.
void TestRoutedPins(Sh2Decompiler& d)
{
    auto report = [&](const char* name, const Report& rep, size_t want = 0) {
        std::printf("pin '%s': %zu finding(s)\n", name, rep.findings.size());
        for (const Finding& f : rep.findings) std::printf("  [%s] %s\n", f.sig.c_str(), f.detail.c_str());
        CHECK(rep.findings.size() == want);
    };
    auto count = [](const Report& rep, const char* name) {
        const auto it = rep.counts.find(name);
        return it == rep.counts.end() ? 0 : it->second;
    };
    const bool saved = gSpecialReads;
    gSpecialReads = false;   // (a seed is another program under the specials alphabet: each pin says which it is)
    // A3-55
    report("A3-55, grammar 2 seed 519315", EvaluateSubject(d, MakeSubject(MakeV2(519315)), true));
    {
        static const ModulePin m = { "A3-55, seed 519315 shrunk",
                                     "2F86 2F96 2FA6 2FC6 2FD6 0C2A 6843 6953 6A63 6063 4C2A 6DF6 6CF6 6AF6 69F6 68F6 000B 0009 7FF4 2448 8B00 D603 2558 8900 6063 7F0C 000B 0009 "
                                     "2D80 00C0",
                                     "0/0/24,24/24/38", nullptr, "1,2", "0,104000", "20000,0" };
        report(m.name, EvaluateSubject(d, ModuleSubject(m), true));
    }
    gSpecialReads = true;
    report("A3-55, grammar 1 seed 608802 (specials)", EvaluateSubject(d, MakeSubject(Generate(608802)), true));
    report("A3-55, seed 608802 shrunk (specials)", EvaluateSubject(d, WordsSubject("D704 8579 E2A3 3300 8B01 6023 210B 000B 0009 0009 2600 2000"), true));
    gSpecialReads = false;
    // A4-16
    report("A4-16, grammar 2 seed 511038", EvaluateSubject(d, MakeSubject(MakeV2(511038)), true));
    {
        static const ModulePin m = { "A4-16, seed 511038 shrunk",
                                     "D613 7FD0 E104 3616 8919 4608 C701 016E 412B 0009 0600 9028 0600 9028 0600 902E 0600 9032 0600 9038 D70A A009 0009 A007 0009 855F A004 0009 "
                                     "E03C A001 0009 D706 A001 0009 FFFF 6073 7F30 000B 0009 0009 C000 0010 25FE 0008 0600 1030",
                                     "0/0/2,2/2/4e", "10:6:5:3e:28,28,2e,32,38", "1,1", nullptr, nullptr, "44-46" };
        report(m.name, EvaluateSubject(d, ModuleSubject(m), true));
    }
    // A4-17
    gSpecialReads = true;
    report("A4-17, grammar 2 seed 609269 (specials)", EvaluateSubject(d, MakeSubject(MakeV2(609269)), true));
    {
        static const ModulePin m = { "A4-17, seed 609269 shrunk (specials)",
                                     "2F86 2F96 4F22 6843 6953 E105 3517 891D 4508 C703 015E 412B 0009 0009 0600 9038 0600 9038 0600 904C 0600 903C 0600 9040 0600 9044 0600 9048 "
                                     "A009 0009 A007 0009 A005 0009 A003 0009 A001 0009 2732 3110 89FF 6040 D302 4F26 69F6 68F6 432B 0009 0600 9064 2FE6 7FFC 6EF3 E103 3612 890F "
                                     "4608 C702 016E 412B 0009 0009 0600 9088 0600 9088 0600 908C A002 0009 A000 0009 6073 7E04 6FE3 6EF6 000B 0009",
                                     "0/0/60,64/64/9c", "16:5:6:4c:38,4c,3c,40,44,48:38;76:6:3:90:88,88,8c", "1,1", "80000,80000", "8,a0000" };
        report(m.name, EvaluateSubject(d, ModuleSubject(m), true));
    }
    gSpecialReads = false;
    // A5-83
    {
        const Report rep = EvaluateSubject(d, MakeSubject(Generate(603025)), true);
        report("A5-83, grammar 1 seed 603025", rep);
        std::printf("  cache.read.pointer.said %d\n", count(rep, "cache.read.pointer.said"));
        CHECK(count(rep, "cache.read.pointer.said") > 0);
    }
    gSpecialReads = saved;
}

// ===== Area 3, round 12 fix: the routed seeds that were the oracle's (A3-100, A3-101) =====
void TestRound12FixPins(Sh2Decompiler& d)
{
    auto count = [](const Report& rep, const char* name) {
        auto it = rep.counts.find(name);
        return it == rep.counts.end() ? 0 : it->second;
    };
    // A3-100 (grammar 2 seed 2007995, shrunk): a callee computes `stc gbr,r2 ; and r5,r2`, and GBR's start 0x06024100 AND r5's 0x06021100 is
    // r4's start 0x06020100 exactly, so r2 at the caller's return was taken for a pointer from r4 (`claim-wrong:outOrigins`). A value an AND,
    // OR or XOR of two other start values the run read lands on is no evidence of either (BitwiseOfOthers).
    {
        RunOut run;
        run.init[4] = 0x06020100u;
        run.init[5] = 0x06021100u;
        run.initState[0] = 0x06024100u;
        run.use.readFirst = (1u << 4) | (1u << 5) | (1u << 17);
        CHECK(PointerRegister(run, 0x06020100u) == -1);   // GBR & r5
        CHECK(PointerRegister(run, 0x06020104u) == -1);
        run.use.readFirst = (1u << 4) | (1u << 5);         // GBR not read: r4's start is r4's
        CHECK(PointerRegister(run, 0x06020100u) == 4);
        const ModulePin m = { "A3-100 grammar 2 seed 2007995 (shrunk)",
                              "2F86 2F96 2FA6 2FB6 4F22 6843 6953 6A63 6B73 6043 4F26 6BF6 6AF6 69F6 68F6 000B 0009 2F86 2F96 2FA6 4F22 7FF0 6843 6953 6A63 B00B E69E 6483 6593 66A3 606D 7F10 4F26 6AF6 69F6 68F6 000B 0009 7FFC 0212 E301 2259 4310 8901 AFFB 0009 6033 7F04 000B 0009",
                              "0/0/22,22/22/4c,4c/4c/64", nullptr, "1,1,1", "20000,0,0", "80000,20000,20000" };
        const Report rep = EvaluateSubject(d, ModuleSubject(m), true);
        std::printf("pin '%s': %zu finding(s)\n", m.name, rep.findings.size());
        for (const Finding& f : rep.findings) std::printf("  [%s] %s\n", f.sig.c_str(), f.detail.c_str());
        CHECK(rep.findings.empty());
    }
    // A3-101 (grammar 1 seed 2126605): `and r0,r0` in the slot of the rts and `mov r0,r0` write r0 with itself, so r0 was "written on every path"
    // and its final value, the perturbed start value passed through, made r0 an input that changes what the function returns
    // (`input-dropped:r0`). r0 that ends as it began in both runs is not compared.
    {
        const Report rep = EvaluateSubject(d, RawSubject("D605 6213 451B 73B8 621D 6161 C810 8B01 6003 334C 000B 2009 2600 1038"), true);
        std::printf("pin 'A3-101 grammar 1 seed 2126605': %zu finding(s)\n", rep.findings.size());
        for (const Finding& f : rep.findings) std::printf("  [%s] %s\n", f.sig.c_str(), f.detail.c_str());
        CHECK(rep.findings.empty());
        // ...and a function that does compute r0 from r0 is still found (the control: `add #1,r0` in the slot, a void signature lent)
        const Report cr = EvaluateSubject(d, RawSubject("C810 8B01 6003 334C 000B 7001"), true);
        std::printf("pin 'A3-101 control: r0 + 1 in the slot of the rts': %zu finding(s), %d withheld\n", cr.findings.size(), count(cr, "proto.withheld"));
        bool dropped = false;
        for (const Finding& f : cr.findings) dropped = dropped || f.sig.find("r0") != std::string::npos;
        CHECK(dropped || count(cr, "proto.withheld") >= 1);
    }
}

// ===== Area 3, round 13 fix: A3-103's two seeds (A3-111, A3-112) and A3-115, oracle false positives =====
void TestRound13FixPins(Sh2Decompiler& d)
{
    auto count = [](const Report& rep, const char* name) {
        auto it = rep.counts.find(name);
        return it == rep.counts.end() ? 0 : it->second;
    };
    // A3-115 (grammar 2 seed 18759, shrunk): `mov.w @r4,r0 ; mov.b @r5+,r0 ; swap.b r5,r1 ; mov #0x68,r0 ; rts`: r1 = r5's start + 1 with its
    // low bytes swapped, 0x06020111, 0x11 past r4's start, was taken for a pointer from r4 (`claim-wrong:outOrigins`). r1 is computed from r5,
    // which outOrigins names: a swap of another start value the run read is no evidence (BitwiseOfOthers).
    {
        RunOut run;
        run.init[4] = 0x06020100u;
        run.init[5] = 0x06021100u;
        run.use.readFirst = (1u << 4) | (1u << 5);
        CHECK(PointerRegister(run, 0x06020111u) == -1);   // swap.b of r5 + 1
        CHECK(PointerRegister(run, 0x06020104u) == 4);    // r4 + 4 is still r4's
        const Report rep = EvaluateSubject(d, RawSubject("6041 6054 6158 E068 000B 0009"), true);
        std::printf("pin 'A3-115 grammar 2 seed 18759 (shrunk)': %zu finding(s)\n", rep.findings.size());
        for (const Finding& f : rep.findings) std::printf("  [%s] %s\n", f.sig.c_str(), f.detail.c_str());
        CHECK(rep.findings.empty());
    }
    // A3-111 (grammar 2 seed 2310198): function 1 pushes and pops r1, and function 2, which it calls after, reads that freed word as an
    // uninitialised byte of its frame (`mov.b @r15,r2`); the run depends on r1 through a stale stack word (`input-dropped:r1`). The stack
    // refilled and scrubbed (RunSpec::stackFill) shows the function reads a stale word, and its dependences are not counted.
    {
        const ModulePin m = { "A3-111 grammar 2 seed 2310198",
                              "2F86 2F96 2FA6 2FB6 2FC6 0C2A 6843 6953 6A63 6B73 0222 6028 E104 3612 8914 4608 C701 016E 412B 0009 0600 9038 0600 9038 0600 9038 0600 903E 2501 A008 0009 2722 E018 0726 A003 0009 6050 E01C 005E 6493 6583 D30C 430B 0009 6483 6593 66A3 67B3 3750 8F01 E1B9 2712 E304 6070 1719 4310 8BFB 6063 4C2A 6CF6 6BF6 6AF6 69F6 68F6 000B 0009 0600 9088 2F86 2F96 2FA6 2FC6 2FD6 0C2A 7FFC 6843 6953 6A63 2F36 E310 2339 63F6 B026 64A3 6483 6593 66A3 A001 0009 17C5 2F36 6262 63F6 632E 4205 2F16 3238 2622 61F6 E108 6483 6523 E6F5 6783 D307 430B 0009 6483 6593 66A3 0312 7F04 4C2A 6DF6 6CF6 6AF6 69F6 68F6 000B 0009 0600 90F4 7FF4 461E 62F0 460A 2421 D602 2F11 000B 7F0C 0009 05F8 00E0",
                              "0/0/84,88/88/f0,f4/f4/106", "24:6:4:48:38,38,38,3e", "1,0,0", "a0000,a0000,a0000", "40000,0,2", "b2-b4" };
        const Report rep = EvaluateSubject(d, ModuleSubject(m), true);
        std::printf("pin '%s': %zu finding(s), stale-stack readers %d, input-dropped excluded %d\n", m.name, rep.findings.size(),
                    count(rep, "stale-stack-read.functions"), count(rep, "excluded.stale-stack-read.input-dropped"));
        for (const Finding& f : rep.findings) std::printf("  [%s] %s\n", f.sig.c_str(), f.detail.c_str());
        CHECK(rep.findings.empty());
        CHECK(count(rep, "excluded.stale-stack-read.input-dropped") >= 1);
        // ...and a function that reads its frame only where it wrote it first is no stale reader: A3-101's control is still found
        const Report cr = EvaluateSubject(d, RawSubject("C810 8B01 6003 334C 000B 7001"), true);
        CHECK(count(cr, "stale-stack-read.functions") == 0);
    }
    // A3-112 (grammar 2 seed 2317870): function 1 calls function 2 (`extu.w r4,r0` at its return) and both arms of the branch after the call
    // store the call's r0 before the `rts` (`mov.b r0,@(1,r5)`; `mov.l r0,@-r4 ; mov.b r0,@r5`): a result used and left in r0 as scratch,
    // A3-19's accepted shape, which the lent `void` says (`excluded.call-result-used.return-dropped`, not `return-dropped`).
    {
        const ModulePin m = { "A3-112 grammar 2 seed 2317870",
                              "2F86 2F96 2FA6 2FB6 2FD6 0D2A 7FC0 6843 6953 6A63 6B73 A001 0009 FFFF E465 6583 E69D 6763 B045 0009 6483 6593 66A3 67B3 020A 440A 6153 471A 6493 D108 0103 6573 6483 6593 66A3 67B3 7F40 4D2A 6DF6 6BF6 6AF6 69F6 68F6 000B 0009 0009 0000 0072 2F86 2F96 2FA6 2FD6 0D2A 6843 6953 6A63 0122 471A 8442 8447 442E 64A3 D10A 410B 0009 6483 6593 66A3 030A 2468 8902 8051 A004 E145 2406 2500 A000 0009 4D2A 6DF6 6AF6 69F6 000B 68F6 0600 90B2 604D 000B 7F08 7FF8 E3AB 2F30 470A AFF7 0009",
                              "0/0/5a,60/60/a8,ac/b2/be", nullptr, "0,1,1", "180000,1c0000,80000", "0,40000,0", "1a-1c" };
        const Report rep = EvaluateSubject(d, ModuleSubject(m), true);
        std::printf("pin '%s': %zu finding(s), call result used %d\n", m.name, rep.findings.size(), count(rep, "excluded.call-result-used.return-dropped"));
        for (const Finding& f : rep.findings) std::printf("  [%s] %s\n", f.sig.c_str(), f.detail.c_str());
        CHECK(rep.findings.empty());
        CHECK(count(rep, "excluded.call-result-used.return-dropped") >= 1);
    }
    // A3-123 (grammar 2 seed 2616394): function 0 byte-swaps r5's low 16 bits (`char *param_2`, 0x06021100 -> 0x06020011) and hands the result
    // to a callee that stores a byte through it. 0x06020011 lands in r4's arena (0x06020xxx) by chance, and the storesThrough arena check took
    // the store for one through r4, which storesThrough does not name (the store is through r5). The same oracle false positive as A3-100 and
    // A3-115, in the storesThrough path instead of outOrigins: BitwiseOfOthers now clears the arena attribution there too.
    {
        RunOut run;
        run.init[4] = 0x06020100u;
        run.init[5] = 0x06021100u;
        run.use.readFirst = (1u << 4) | (1u << 5);
        CallEffects viaR5 = CallEffects();
        viaR5.storesThrough = 1u << 5;
        viaR5.spDelta = 0;
        run.trace.push_back(TraceRec{ 0x0u, true, 1, 0x06020011u, 0x42u });   // a byte store at the swapped address
        std::string sig;
        const std::string why = EffectClaimsContradicted(viaR5, run, &sig);
        std::printf("pin 'A3-123 grammar 2 seed 2616394 (swap into r4 arena)': %s\n", why.empty() ? "no contradiction" : (sig + ": " + why).c_str());
        CHECK(why.empty());
        // ...and a genuine store through r4 is still demanded (not a swap of r5)
        RunOut r4run = run;
        r4run.trace.back() = TraceRec{ 0x0u, true, 1, 0x06020104u, 0x42u };
        CHECK(!EffectClaimsContradicted(viaR5, r4run, &sig).empty() && sig == std::string("claim-wrong:storesThrough"));
    }
}

// ===== Area 3, round 9 fix: the shapes of A3-56 to A3-59 and A3-62 as modules, the grammar's choices, and the claims of A3-63 =====
void TestRound9Pins(Sh2Decompiler& d)
{
    auto count = [](const Report& rep, const char* name) {
        const auto it = rep.counts.find(name);
        return it == rep.counts.end() ? 0 : it->second;
    };
    // Each module is the review's replay. The code before the fix lent it `void` and the checks found it (the finding in the comment); with the
    // return rule it is withheld, and nothing is found. (A3-58's two loaded words, D1, are the suite's: its replay's words sit in the arenas, whose
    // folded reads the ledger reports for the module, round 9's record.)
    struct Shape
    {
        const char* name;
        const char* words;
        const char* funcs;
    };
    static const Shape kShapes[] = {
        { "A3-56 tst r0,r0 chooses ([n:input-dropped:r0], [n:return-dropped])", "2008 8903 E001 210B A002 0009 E002 210B 000B 0009", "0/0/14" },
        { "A3-57 bt on the entry T ([n:return-dropped])", "8903 E001 210B A002 0009 E002 210B 000B 0009", "0/0/12" },
        { "A3-58 r4 + 1 or r4 + 2 by r3 ([n:return-dropped])", "6043 7001 3300 8B02 6043 7002 210B 000B 0009", "0/0/12" },
        { "A3-59 add r4,r0 under tst r5, r4 stored ([n:input-dropped:r0], [n:return-dropped])", "2558 8901 304C 2642 000B 0009", "0/0/c" },
        { "A3-62 tst r4,r4 chooses ([n:return-dropped])", "2448 8903 E001 210B A002 0009 E002 210B 000B 0009", "0/0/14" },
        { "A3-62 the word at r15 chooses ([n:return-dropped])", "63F2 2338 8903 E001 210B A002 0009 E002 210B 000B 0009", "0/0/16" },
    };
    for (const Shape& s : kShapes)
    {
        const ModulePin m = { s.name, s.words, s.funcs, nullptr, "1", nullptr, nullptr };
        const Report rep = EvaluateSubject(d, ModuleSubject(m), true);
        std::printf("pin '%s': %zu finding(s), %d withheld\n", s.name, rep.findings.size(), count(rep, "proto.withheld"));
        for (const Finding& f : rep.findings) std::printf("  [%s] %s\n", f.sig.c_str(), f.detail.c_str());
        CHECK(rep.findings.empty() && count(rep, "proto.withheld") >= 1);
    }
    // The grammar's choices (A3-64): the first grammar-2 seed that draws each chooser, decompiled and checked. A chooser the signature does not hold
    // is withheld; one it holds, or a constant, is the accepted void (`excluded.choice-held.return-dropped`).
    {
        const char* const kKinds[] = { "choice.by_r0", "choice.by_sr", "choice.by_scratch", "choice.by_param", "choice.by_const", "choice.by_stack" };
        for (const char* kind : kKinds)
        {
            long found = -1;
            for (long seed = 1; seed <= 20000 && found < 0; ++seed)
            {
                std::map<std::string, int> shapes;
                CountProgram(MakeV2(static_cast<uint64_t>(seed)), shapes);
                if (shapes[kind] > 0) found = seed;
            }
            std::printf("pin '%s': first drawn by grammar 2 seed %ld\n", kind, found);
            CHECK(found > 0);
            if (found < 0) continue;
            const Report rep = EvaluateSubject(d, MakeSubject(MakeV2(static_cast<uint64_t>(found))), true);
            for (const Finding& f : rep.findings) std::printf("  [%s] %s\n", f.sig.c_str(), f.detail.c_str());
            CHECK(rep.findings.empty());
        }
    }
    // The claims of A3-63, held by check 10b on a lent function's runs: `mov.l r4,@r15` spills the argument into its caller's home area (frameValues:
    // r4), `mov.l r5,@r4` stores a pointer to memory it does not track (leaks: r5), and r1 and r2 end as r4 + 4 and as the stack pointer (outOrigins:
    // r4, the stack). Each claim mutated as the review's m1, m2, m3 and m5 did is reported here.
    {
        const ModulePin m = { "A3-63: a lent function's frameValues, leaks and outOrigins", "2F42 2452 6143 7104 62F3 000B 0009", "0/0/e", nullptr, "0", nullptr, nullptr };
        const Report rep = EvaluateSubject(d, ModuleSubject(m), true);
        std::printf("pin '%s': %zu finding(s), %d lent, %d runs held to the claims\n", m.name, rep.findings.size(), count(rep, "proto.lent"), count(rep, "claims.checked"));
        for (const Finding& f : rep.findings) std::printf("  [%s] %s\n", f.sig.c_str(), f.detail.c_str());
        CHECK(rep.findings.empty() && count(rep, "proto.lent") >= 1 && count(rep, "claims.checked") > 0);
    }
}

// ---- A3-65: a copy of r15 walked down (patch 0056) ---------------------------------------------------------------------------------------------
// Seed 901303 (grammar 2), where the class was found: function 0 restores its registers and runs on into function 1, whose frame walk goes down
// over the words function 0's pushes freed; the text read the walk's byte as the saved r13 (`(char)((uint4)unaff_r13 >> 0x10)`). With the patch the
// save slot is a local the walk may write (`xStack_c = unaff_r13;` at the push, `xStack_c._1_1_` read after the walk), and check 12 takes the save
// before any call for no read of r13 (SavesBeforeCallsDropped). The walk the printed-C differential draws (GenWalkFn) is drawn in every form.
void TestA365Pins(Sh2Decompiler& d)
{
    auto count = [](const Report& rep, const char* name) {
        const auto it = rep.counts.find(name);
        return it == rep.counts.end() ? 0 : it->second;
    };
    {
        const ModulePin m = { "A3-65, seed 901303 shrunk", "2F86 2F96 2FD6 4F22 6843 6953 B00C 6593 6483 6593 D103 4F26 6DF6 69F6 68F6 412B 0009 0009 0600 9028 2FD6 7FF8 62F3 7208 E102 2216 4110 8BFC E200 84F1 1605 6073 7F08 6DF6 000B 0009",
                              "0/0/22,28/28/48", nullptr, "1,1", "0,0", "40000,0" };
        const Report rep = EvaluateSubject(d, ModuleSubject(m), true);
        std::printf("pin '%s': %zu finding(s)\n", m.name, rep.findings.size());
        for (const Finding& f : rep.findings) std::printf("  [%s] %s\n", f.sig.c_str(), f.detail.c_str());
        CHECK(rep.findings.empty());
        const bool savedSpecials = gSpecialReads;
        gSpecialReads = false;
        const Report seed = EvaluateSubject(d, MakeSubject(MakeV2(901303)), true);
        gSpecialReads = savedSpecials;
        std::printf("pin 'A3-65, grammar 2 seed 901303': %zu finding(s)\n", seed.findings.size());
        for (const Finding& f : seed.findings) std::printf("  [%s] %s\n", f.sig.c_str(), f.detail.c_str());
        CHECK(seed.findings.empty());
    }
    // A3-78, seed 1009895 (grammar 2) and its shrunk module: function 0 pushes r10, calls function 1 (lent, changes r10), pops and tail-jumps
    // into function 2, whose `mov.w @r15` reads the half of the slot r10 was pushed to. The text saves that half before the call
    // (`iStack_c = (int2)((uint4)unaff_r10 >> 0x10);`) and reads the local after it: right, and no read of r10 kept across the call.
    {
        const ModulePin m = { "A3-78, seed 1009895 shrunk",
                              "2F86 2F96 2FA6 2FC6 0C2A 7FF4 6843 6953 6A63 B00F 0009 6483 6593 66A3 020A D104 7F0C 4C2A 6CF6 6AF6 69F6 68F6 412B 0009 0600 9068 "
                              "2F86 2F96 2FA6 2FB6 2FC6 0C2A 7FD0 6843 6953 6A63 6B73 B00D 0009 6483 6593 66A3 67B3 7F30 4C2A 6CF6 6BF6 6AF6 69F6 68F6 000B 0009 "
                              "7FF4 61F1 2418 8B00 6262 62F3 720C E103 2216 4110 8BFC E200 7F0C 000B 0009",
                              "0/0/30,34/34/68,68/68/86", nullptr, "0,0,0", "184000,184000,184000", "0,4000,0" };
        const Report rep = EvaluateSubject(d, ModuleSubject(m), true);
        std::printf("pin '%s': %zu finding(s)\n", m.name, rep.findings.size());
        for (const Finding& f : rep.findings) std::printf("  [%s] %s\n", f.sig.c_str(), f.detail.c_str());
        CHECK(rep.findings.empty() && count(rep, "lent.changed.checked") > 0);
        const bool savedSpecials = gSpecialReads;
        gSpecialReads = false;
        const Report seed = EvaluateSubject(d, MakeSubject(MakeV2(1009895)), true);
        gSpecialReads = savedSpecials;
        std::printf("pin 'A3-78, grammar 2 seed 1009895': %zu finding(s)\n", seed.findings.size());
        for (const Finding& f : seed.findings) std::printf("  [%s] %s\n", f.sig.c_str(), f.detail.c_str());
        CHECK(seed.findings.empty());
    }
    // A3-78, seed 1418819 (the area 2 round 11 review) and its shrunk module: function 0 calls function 1 on one path and tail-jumps into it,
    // whose pushes of r10 and r12 print in function 0's text after the branch. The engine joins the entry value and the call's output under one
    // name (`unaff_r10 = extraout_r10;` after the call), so the pushes read `unaff_r10`: the entry value on the path without the call, the call's
    // output on the other. Right, and no read of r10 or r12 kept across the call.
    {
        const ModulePin m = { "A3-78, seed 1418819 shrunk",
                              "2F86 2F96 2FD6 2FE6 4F22 7FF0 6EF3 6843 6953 2458 8903 B00F 0009 6483 6593 D305 7E10 6FE3 4F26 6EF6 6DF6 69F6 68F6 432B 0009 0009 0600 9038 "
                              "2F86 2F96 2FA6 2FC6 0C2A 6843 6953 6A63 B00A 0009 6483 6593 66A3 4C2A 6CF6 6AF6 69F6 68F6 000B 0009 "
                              "2FD6 2FE6 7FF0 6EF3 62F3 7210 E103 2216 4110 8BFC E200 E0A6 7E10 6FE3 6EF6 6DF6 000B 0009",
                              "0/0/32,38/38/60,60/60/84", nullptr, "0,0,1", nullptr, nullptr };
        const Report rep = EvaluateSubject(d, ModuleSubject(m), true);
        std::printf("pin '%s': %zu finding(s)\n", m.name, rep.findings.size());
        for (const Finding& f : rep.findings) std::printf("  [%s] %s\n", f.sig.c_str(), f.detail.c_str());
        CHECK(rep.findings.empty() && count(rep, "lent.changed.checked") > 0);
        const bool savedSpecials = gSpecialReads;
        gSpecialReads = false;
        const Report seed = EvaluateSubject(d, MakeSubject(MakeV2(1418819)), true);
        gSpecialReads = savedSpecials;
        std::printf("pin 'A3-78, grammar 2 seed 1418819': %zu finding(s)\n", seed.findings.size());
        for (const Finding& f : seed.findings) std::printf("  [%s] %s\n", f.sig.c_str(), f.detail.c_str());
        CHECK(seed.findings.empty());
    }
    // check 12's reading of a save: before the first call it is no read of the register; after one, or a read elsewhere, it is
    const std::string head = "xunknown4 func_0x06009000(void)\n\n{\n  xunknown4 unaff_r13;\n  xunknown4 xStack_c;\n\n";
    CHECK(!WholeWord(SavesBeforeCallsDropped(head + "  xStack_c = unaff_r13;\n  func_0x06009028();\n  return xStack_c;\n}\n", "r13"), "unaff_r13"));
    CHECK(WholeWord(SavesBeforeCallsDropped(head + "  func_0x06009028();\n  xStack_c = unaff_r13;\n  return xStack_c;\n}\n", "r13"), "unaff_r13"));
    CHECK(WholeWord(SavesBeforeCallsDropped(head + "  xStack_c = unaff_r13;\n  func_0x06009028();\n  return unaff_r13;\n}\n", "r13"), "unaff_r13"));
    // A3-78: a save shown in part (a cast, a right shift by a constant) is the same save; an operation that combines it with anything is not
    CHECK(!WholeWord(SavesBeforeCallsDropped(head + "  xStack_c = (int2)((uint4)unaff_r13 >> 0x10);\n  func_0x06009028();\n  return xStack_c;\n}\n", "r13"), "unaff_r13"));
    CHECK(!WholeWord(SavesBeforeCallsDropped(head + "  xStack_c = (char)unaff_r13;\n  func_0x06009028();\n  return xStack_c;\n}\n", "r13"), "unaff_r13"));
    CHECK(WholeWord(SavesBeforeCallsDropped(head + "  func_0x06009028();\n  xStack_c = (int2)((uint4)unaff_r13 >> 0x10);\n  return xStack_c;\n}\n", "r13"), "unaff_r13"));
    CHECK(WholeWord(SavesBeforeCallsDropped(head + "  xStack_c = (int2)((uint4)unaff_r13 + 0x10);\n  func_0x06009028();\n  return xStack_c;\n}\n", "r13"), "unaff_r13"));
    CHECK(WholeWord(SavesBeforeCallsDropped(head + "  xStack_c = (int2)((uint4)unaff_r13 >> 0x10);\n  func_0x06009028();\n  return unaff_r13;\n}\n", "r13"), "unaff_r13"));
    // A3-78, seed 1418819: the entry value's variable given the call's output right after the call is no stale read; given it in another block,
    // after an earlier call, or from itself, or in a text with a loop, it is
    const std::string head2 = "xunknown4 func_0x06009000(void)\n\n{\n  xunknown4 unaff_r13;\n  xunknown4 extraout_r13;\n\n";
    CHECK(!WholeWord(SavesBeforeCallsDropped(head2 + "  if (c) {\n    func_0x06009028();\n    unaff_r13 = extraout_r13;\n  }\n  *p = unaff_r13;\n}\n", "r13"), "unaff_r13"));
    CHECK(WholeWord(SavesBeforeCallsDropped(head2 + "  if (c) {\n    func_0x06009028();\n  }\n  else {\n    unaff_r13 = extraout_r13;\n  }\n  *p = unaff_r13;\n}\n", "r13"), "unaff_r13"));
    CHECK(WholeWord(SavesBeforeCallsDropped(head2 + "  func_0x06009020();\n  if (c) {\n    func_0x06009028();\n    unaff_r13 = extraout_r13;\n  }\n  *p = unaff_r13;\n}\n", "r13"), "unaff_r13"));
    CHECK(WholeWord(SavesBeforeCallsDropped(head2 + "  func_0x06009028();\n  unaff_r13 = unaff_r13 + 1;\n  *p = unaff_r13;\n}\n", "r13"), "unaff_r13"));
    CHECK(WholeWord(SavesBeforeCallsDropped(head2 + "  func_0x06009028();\n  unaff_r13 = extraout_r13;\n  do {\n    *p = unaff_r13;\n    func_0x06009028();\n  } while (c);\n}\n", "r13"), "unaff_r13"));
    CHECK(WholeWord(SavesBeforeCallsDropped(head2 + "  func_0x06009028();\n  unaff_r13 = extraout_r13;\n  func_0x06009028();\n  *p = unaff_r13;\n}\n", "r13"), "unaff_r13"));
    // the walk functions of the printed-C differential: every width, both directions, the read back up, within the first 400 seeds
    {
        std::map<std::string, int> drawn;
        int walks = 0;
        for (uint64_t seed = 1; seed <= 400; ++seed)
        {
            RegFn f;
            if (!GenWalkFn(seed, f)) continue;
            ++walks;
            for (const RegItem& it : f.items) ++drawn[it.shape];
        }
        std::printf("pin 'A3-65 walk draws': %d walk functions in 400 seeds:", walks);
        for (const auto& k : drawn) if (k.first.compare(0, 5, "walk.") == 0) std::printf(" %s=%d", k.first.c_str(), k.second);
        std::printf("\n");
        for (const char* shape : { "walk.down.b", "walk.down.w", "walk.down.l", "walk.up.b", "walk.up.w", "walk.up.l", "walk.down.l.readback" })
            CHECK(drawn[shape] > 0);
    }
    // A3-80: the walk pointer kept in a stack local, with and without a call, at every width, moved down after the read, and the controls
    {
        std::map<std::string, int> drawn;
        int spills = 0;
        for (uint64_t seed = 1; seed <= 400; ++seed)
        {
            CCase c;
            std::string shape;
            if (!GenSpillWalkFn(seed, c, shape)) continue;
            ++spills;
            ++drawn[shape.substr(0, shape.find('.', 6) == std::string::npos ? shape.size() : shape.find('.', 6) + 2)];
            if (shape.find(".lowered") != std::string::npos) ++drawn["lowered"];
            if (shape.find(".control") != std::string::npos) ++drawn["control"];
        }
        std::printf("pin 'A3-80 spill draws': %d spill functions in 400 seeds:", spills);
        for (const auto& k : drawn) std::printf(" %s=%d", k.first.c_str(), k.second);
        std::printf("\n");
        for (const char* shape : { "spill.call.b", "spill.call.w", "spill.call.l", "spill.nocall.b", "spill.nocall.w", "spill.nocall.l", "lowered", "control" })
            CHECK(drawn[shape] > 0);
    }
    // Round 11 fix: the laundered walks draw every step in each half, every callee form, the caller-side step and the join
    {
        std::map<std::string, int> drawn;
        int n = 0;
        for (uint64_t seed = 1; seed <= 2000; ++seed)
        {
            CCase c;
            std::string shape;
            if (!GenLaunderWalkFn(seed, c, shape)) continue;
            ++n;
            const bool self = shape.compare(0, 13, "launder.self.") == 0;
            for (const char* st : kLaunderSteps)
                if (shape.find(std::string(".") + st) != std::string::npos) ++drawn[std::string(self ? "self." : "call.") + st];
            for (const char* fm : { "direct", "wrapper", "thunk", "lowered" })
                if (shape.find(std::string(".") + fm + ".") != std::string::npos) ++drawn[fm];
            if (shape.find(".caller") != std::string::npos) ++drawn["caller"];
            if (shape.find(".join") != std::string::npos) ++drawn["join"];
            if (shape.find(".walk.") != std::string::npos) ++drawn["walk"];
            if (shape.find(".disp.") != std::string::npos) ++drawn["disp"];
        }
        std::printf("pin 'round 11 laundered-walk draws': %d functions in 2000 seeds:", n);
        for (const auto& k : drawn) std::printf(" %s=%d", k.first.c_str(), k.second);
        std::printf("\n");
        for (const char* st : kLaunderSteps)
        {
            CHECK(drawn[std::string("self.") + st] > 0);
            CHECK(drawn[std::string("call.") + st] > 0);
        }
        for (const char* k : { "direct", "wrapper", "thunk", "lowered", "caller", "join", "walk", "disp" }) CHECK(drawn[k] > 0);
    }
    // Round 12 fix: the escape stream draws every shape of A3-89 to A3-94 and A3-97 (each with the variants its rule is held by)
    {
        std::map<std::string, int> drawn;
        int n = 0;
        for (uint64_t seed = 1; seed <= 2000; ++seed)
        {
            CCase c;
            std::string shape;
            if (!GenEscapeWalkFn(seed, c, shape)) continue;
            ++n;
            ++drawn[shape.substr(0, shape.rfind('.'))];
            ++drawn[shape];
        }
        std::printf("pin 'round 12 escape draws': %d functions in 2000 seeds, %zu kinds\n", n, drawn.size());
        for (const char* k : { "escape.keep.global.lent", "escape.keep.global.unlent", "escape.keep.global.lent.ret", "escape.keep.global.unlent.ret",
                               "escape.keep.arg2.lent", "escape.keep.arg2.unlent", "escape.keep.outgoing", "escape.back.r1", "escape.back.r9",
                               "escape.back.r10", "escape.back.mach", "escape.back.gbr", "escape.chain.r1.call", "escape.chain.r1.jump",
                               "escape.chain.r1.callback", "escape.chain.r4.call", "escape.chain.r4.jump", "escape.chain.r4.callback", "escape.chain.r1.call.counted",
                               "escape.chain.r1.jump.counted", "escape.chain.r1.callback.counted",
                               "escape.struct.none", "escape.struct.mask", "escape.struct.swap2", "escape.struct.stack.none", "escape.struct.stack.mask", "escape.struct.stack.swap2", "escape.callerframe.none",
                               "escape.callerframe.swap2", "escape.callerframe.mask", "escape.spare", "escape.rewrite.load.lent", "escape.rewrite.load.unlent",
                               "escape.rewrite.ret.lent", "escape.rewrite.ret.unlent", "escape.rewrite.r9.lent", "escape.rewrite.r9.unlent" })
            CHECK(drawn[k] > 0);
        for (const auto& k : kEscapeNegShift)
            CHECK(drawn[std::string("escape.") + k] > 0);
    }
    // Round 13 fix (A3-114): the stream of A3-104 to A3-106 draws each shape and variant
    {
        std::map<std::string, int> drawn;
        int n = 0;
        for (uint64_t seed = 1; seed <= 2000; ++seed)
        {
            CCase c;
            std::string shape;
            if (!GenEscape13Fn(seed, c, shape)) continue;
            ++n;
            ++drawn[shape.substr(0, shape.rfind('.'))];
            ++drawn[shape];
        }
        std::printf("pin 'round 13 escape draws': %d functions in 2000 seeds, %zu kinds\n", n, drawn.size());
        for (uint64_t seed = 1; seed <= 2000; ++seed)
        {
            CCase c;
            std::string shape;
            if (GenIndexWalk13Fn(seed, c, shape)) ++drawn[shape];
        }
        for (const char* k : { "escape13.index.add", "escape13.index.sub", "escape13.twoargs.lent", "escape13.twoargs.unlent", "escape13.stackkeep.lent", "escape13.stackkeep.unlent",
                               "escape13.stackkeep.lent.r4r7", "escape13.stackkeep.unlent.r4r7",
                               "escape13.reload.swap2", "escape13.reload.mask", "escape13.reload.global" })
            CHECK(drawn[k] > 0);
    }
    // Round 14 fix (A3-121): the stream of A3-117 to A3-119, A3-122 and A3-126 draws each shape and variant (the cap one seed in about 800)
    {
        std::map<std::string, int> drawn;
        int n = 0;
        for (uint64_t seed = 1; seed <= 40000; ++seed)
        {
            CCase c;
            std::string shape;
            if (!GenWalk14Fn(seed, c, shape)) continue;
            ++n;
            ++drawn[shape];
            ++drawn[shape.substr(0, shape.rfind('.'))];
        }
        std::printf("pin 'round 14 walk draws': %d functions in 40000 seeds, %zu kinds, %d of the cap\n", n, drawn.size(), drawn["walk14.cap"]);
        for (const char* k : { "walk14.idx.down.c", "walk14.idx.down.u", "walk14.idx.subup.c", "walk14.idx.subup.u", "walk14.idx.negup.c", "walk14.idx.negup.u",
                               "walk14.stride.pool.add", "walk14.stride.pool.sub", "walk14.stride.poolw.add", "walk14.stride.poolw.sub",
                               "walk14.stride.reload.add", "walk14.stride.reload.sub", "walk14.stride.global.add", "walk14.stride.global.sub",
                               "walk14.stackarg.r6.walk", "walk14.stackarg.r7.walk", "walk14.stackarg.r6r7.walk",
                               "walk14.stackarg.r6.callee", "walk14.stackarg.r7.callee", "walk14.stackarg.r6r7.callee", "walk14.cap" })
            CHECK(drawn[k] > 0);
    }
    // A3-124 fix (patch 0068): the frame124 stream draws each frame base, fill, call and width (the bytes fill has no call)
    {
        std::map<std::string, int> drawn;
        int n = 0;
        for (uint64_t seed = 1; seed <= 4000; ++seed)
        {
            CCase c;
            std::string shape;
            if (!GenFrame124Fn(seed, c, shape)) continue;
            ++n;
            ++drawn[shape];
        }
        std::printf("pin 'frame124 draws': %d functions in 4000 seeds, %zu kinds\n", n, drawn.size());
        for (const char* base : { "r15", "r14" })
            for (const char* fill : { "words", "bytes" })
                for (const char* call : { "nocall", "call", "callr" })
                    for (const char* w : { "b", "w", "l", "mixed" })
                    {
                        if (std::string(fill) == "bytes" && std::string(call) != "nocall") continue;
                        CHECK(drawn[std::string("frame124.") + base + "." + fill + "." + call + "." + w] > 0);
                    }
    }
}

void TestCanary(Sh2Decompiler& d)
{
    const long n = EnvNumber("SE_GENERATED_CANARY", 240);
    const long only = EnvNumber("SE_GENERATED_CANARY_ONLY", -1);
    std::map<std::string, int> counts;
    int failing = 0;
    for (long v = only >= 0 ? only : 0; v < (only >= 0 ? only + 1 : n); ++v)
    {
        std::string label;
        const Subject s = MakeCanarySubject(static_cast<int>(v), label);
        Report rep = EvaluateSubject(d, s, only >= 0);
        for (const auto& kv : rep.counts) counts[kv.first] += kv.second;
        if (only >= 0) std::printf("%s", rep.text.c_str());
        if (!rep.findings.empty())
        {
            ++failing;
            std::printf("canary finding: %s [%s]\n", label.c_str(), rep.findings[0].sig.c_str());
            if (failing <= 3)
            {
                const Report again = EvaluateSubject(d, s, true);
                std::printf("CANARY FAILURE %s\n%s", label.c_str(), again.text.c_str());
                for (const Finding& f : rep.findings) std::printf("  [%s] %s\n", f.sig.c_str(), f.detail.c_str());
            }
        }
    }
    std::printf("canary: %ld programs, %d failing, lent prototypes %d, withheld %d (SE_GENERATED_CANARY=<n>, SE_GENERATED_CANARY_ONLY=<k>)\n",
                only >= 0 ? 1L : n, failing, counts["proto.lent"], counts["proto.withheld"]);
    CHECK(failing == 0);
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
        RUN(TestLeftoverSeedPins(d));
        RUN(TestRound7Pins(d));
        RUN(TestA582Pins(d));
        RUN(TestRound8Pins(d));
        RUN(TestRoutedPins(d));
        RUN(TestRound9Pins(d));
        RUN(TestRound12FixPins(d));
        RUN(TestRound13FixPins(d));
        RUN(TestA365Pins(d));
        if (EnvNumber("SE_GENERATED_CANARY", 240) > 0) RUN(TestCanary(d));
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
                    "spills=%d frame walks=%d locals ld/st=%d/%d (stack accesses exempt %d) calls bsr/jsr/bsrf=%d/%d/%d (arg in slot %d, args restored %d) "
                    "tables=%d (entries %d, cases %d, shared entries %d, default entries %d; destinations traced %d, printed %d, inert %d, warned %d) "
                    "bra loops fwd/back=%d/%d data skips=%d shared exits=%d exits below the entry=%d slot pops=%d ret in slot=%d; "
                    "lent prototypes=%d (void %d, returning %d) withheld=%d; parameter checks=%d (candidates %d, perturbed runs %d, insensitive %d); "
                    "preserved checks=%d; flow: instructions executed=%d, into-function said/silent=%d/%d, outside body but named=%d (after a tail jump %d); runs=%d\n",
                    n("module.v2"), n("fn"), n("fn.end.rts"), n("fn.end.tail"), n("fn.end.fall"), n("fn.returns"), n("fn.void"), n("fn.pushed"),
                    n("fn.pr.stack"), n("fn.pr.reg"), n("fn.frame"), n("fn.fp"), n("spill"), n("frame.walk"), n("stack.ld.plain") + n("stack.ld.disp") + n("stack.ld.indexed"),
                    n("stack.st.plain") + n("stack.st.disp") + n("stack.st.indexed"), c("stack.exempt"), n("call.bsr"), n("call.jsr"), n("call.bsrf"), n("call.arg_in_slot"),
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
