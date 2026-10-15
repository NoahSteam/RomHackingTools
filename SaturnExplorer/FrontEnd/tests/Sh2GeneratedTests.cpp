// The decompiler against the emulator: random SH-2 functions from a small grammar, run on the
// vendored p-code emulator for the memory accesses the code really makes (the ground truth),
// then decompiled; every access the emulator saw must be in the decompiler's access ledger
// (DecompileResult::accesses), unless it is one the decompiler is allowed to fold or has said it
// removed. This is the review program's instrument for finding failure classes without a human
// reviewer (Docs/Decompiler/REVIEW_PROGRAM.md, Phase 0): a shape the hand-written fixtures never
// thought of shows up as a seed that fails.
//
// The grammar (generation is a pure function of the seed):
//   * ALU: mov #imm, mov, add, sub, and, or, xor, shll2, extu.b, extu.w -- into r0-r3 only.
//   * loads and stores of every width through r4/r5 (pointer parameters into scratch RAM) and
//     r6/r7 (pointers loaded from the literal pool: a hardware register, an ordinary RAM address
//     or a mailbox the request marks volatile), as @Rn, @(disp,Rn), @(r0,Rn), @Rm+ / @-Rn (r4/r5
//     only) and tas.b. r15 is never touched.
//   * a forward conditional branch (tst/cmp, then bt/bf or bt/s/bf/s with a delay-slot
//     instruction) over one to three instructions, which may reload a pointer register.
//   * a counted loop: mov #n,r3 ; body ; dt r3 ; bf.
//   * a mailbox write-then-poll: store a value, then read until another processor changes it.
//   * rts with a nop or an instruction in its delay slot. A delay slot never holds a
//     PC-relative load or a branch (the decompiler declares that unsupported, by policy).
//
// The check, per seed (and again with the mailbox marked volatile when the program has one):
//   1. Decompile returns Ok, and the result is not unsupported, unimplemented or truncated.
//   2. Every (instruction, read or write, size) the emulator traced has an entry in
//      DecompileResult::accesses, or is an access to ordinary memory at a constant address (the
//      decompiler folds those into variables: every pointer reaching it came from a literal that
//      is not a hardware address and not memory the request marked), or is a literal-pool load
//      (folded by design), or sits in code the output says it removed ("here is missing").
//      Everything else is a dropped access.
//   3. Of the accesses listed: each is printed exactly once, none is made twice on a path, none
//      is printed only on some of the paths that reach it, and the ledger's ordering invariants
//      (Sh2DecompilerTests.cpp's Ledger) hold.
// The ground truth is three emulator runs with different inputs (registers, what hardware reads
// return), united. The "other processor" of a mailbox poll answers a polled hardware register
// always and a polled address the request marks volatile only in that request's run: an unmarked
// work-RAM poll folds in the decompiler by design (DECOMPILER_GUIDE.md), so its run has no one
// answering and stops at the step limit.
//
// usage: SaturnExplorerSh2GeneratedTests <scratch dir>
//   SE_GENERATED_SEEDS=<n>   how many seeds (default 300)
//   SE_GENERATED_FIRST=<k>   the first seed (default 1): a nightly run moves it
//   SE_GENERATED_SEED=<k>    one seed, with everything printed
//   SE_GENERATED_SHRINK=<n>  how many findings to reduce (default 3; 0 none; large: all)
//   SE_GENERATED_VERBOSE=1   print the full report of every failing seed, not just the first few
//   SE_GENERATED_MUTATE=1    delete one ledger entry per decompilation; the run passes only if that is reported
//   SE_GENERATED_FENCE=selfops|folds  keep a known open class out of the grammar (see gFenceFolds)
//   SE_GENERATED_WORDS="D102 6010 ..."  check one hand-written function (hex words from the entry,
//                            literal pool among them) instead of generated ones; with
//                            SE_GENERATED_VOLATILE=<first>-<last> and SE_GENERATED_POLL=<addr>[:until-set]
#include "DecompilerTestSupport.h"

#include "Decompiler/SaturnArchitecture.h"
#include "Decompiler/SaturnHardwareMap.h"
#include "Decompiler/Sh2Decompiler.h"
#include "emulate.hh"
#include "memstate.hh"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <functional>
#include <map>
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

// ---- The program model ----------------------------------------------------------------------

enum class LitKind : uint8_t { Hw, Ram, Mbox };
const char* KindName(LitKind k) { return k == LitKind::Hw ? "hardware" : k == LitKind::Ram ? "RAM" : "mailbox"; }

struct Lit
{
    uint32_t value = 0;
    LitKind kind = LitKind::Ram;
    int width = 4;   // the width accesses through it prefer (a register's documented width)
};

struct Item
{
    enum Type : uint8_t { Alu, Mem, LitPtr, Skip, Loop, Poll } type = Alu;
    const char* shape = "";
    std::vector<uint16_t> words;   // Alu, Mem: the instruction words (an indexed access: mov #k,r0 first)
    int base = -1;                 // Mem: the pointer register
    int reg = 6, lit = 0;          // LitPtr, Poll: the pointer register and its literal
    uint16_t cond = 0;             // Skip: the tst or cmp that sets T
    bool onTrue = true;            // Skip: bt (else bf)
    bool delayed = false;          // Skip: bt/s, bf/s with 'slot'
    int counter = 3, count = 2;    // Loop
    int width = 2;                 // Poll
    bool pollWrite = true;         // Poll: stores 'pollValue' first
    int pollValue = 1;
    bool untilSet = false;         // Poll: waits for bit 0 to become set (else clear)
    std::vector<Item> slot;        // Skip with delayed: its one single-word instruction
    std::vector<Item> kids;        // Skip: the skipped instructions; Loop: the body
};

struct Program
{
    std::vector<Lit> lits;
    uint32_t mbox = 0x26001000u;   // the mailbox the request marks volatile, 16 bytes
    std::vector<Item> items;
    std::vector<Item> tail;        // the rts delay slot: empty (nop) or one instruction
};

// ---- Generation ----------------------------------------------------------------------------

struct Gen
{
    Rng rng;
    Program p;
    int align[8];      // the natural alignment of every literal that may be in a register
    int pref[8];       // ...and the width of the latest
    bool everConst[8] = {};   // a register that has been given a constant (folds fence)
    Gen(uint64_t seed) : rng(seed)
    {
        for (int i = 0; i < 8; ++i) align[i] = 4, pref[i] = 4;
        p.mbox = 0x26001000u + 0x10u * rng.Below(4);
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
        Item it;
        it.type = Item::LitPtr;
        it.reg = reg;
        it.lit = static_cast<int>(p.lits.size());
        it.shape = kind == LitKind::Hw ? "litptr.hw" : kind == LitKind::Ram ? "litptr.ram" : "litptr.mbox";
        p.lits.push_back(NewLit(kind));
        align[reg] = std::min(align[reg], Alignment(p.lits.back().value));
        pref[reg] = p.lits.back().width;
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
        return w;
    }

    Item MakeAlu(int avoid)
    {
        Item it;
        it.type = Item::Alu;
        auto dest = [&] {
            int d = static_cast<int>(rng.Below(4));
            return d == avoid ? (d + 1) & 3 : d;
        };
        const int n = dest();
        int m = static_cast<int>(rng.Below(6));
        if (gFenceSelfOps && m == n) m = (m + 1) % 6;
        const uint32_t imm = rng.Below(256);
        uint32_t choice = rng.Below(11);
        if (gFenceFolds)
        {
            static const uint32_t kSafe[] = { 0, 1, 2, 8, 9, 10 };
            choice = kSafe[rng.Below(6)];
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
        return it;
    }

    // A load or store. 'single': exactly one instruction word (a delay slot).
    Item MakeMem(int avoid, bool single)
    {
        Item it;
        it.type = Item::Mem;
        const int base = 4 + static_cast<int>(rng.Below(4));
        const bool write = rng.Chance(45);
        int w = PickWidth(base);
        const int sz = w == 1 ? 0 : w == 2 ? 1 : 2;
        int form = static_cast<int>(rng.Below(100));
        form = form < 40 ? 0 : form < 70 ? 1 : form < 85 ? 2 : form < 94 ? 3 : 4;   // plain disp indexed inc tas
        if (single && form == 2) form = 0;
        if (form == 3 && base >= 6) form = 0;
        int data = static_cast<int>(rng.Below(4));
        if (!write && data == avoid) data = (data + 1) & 3;
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
        return it;
    }

    Item MakeSimple(int avoid, bool single, bool allowLit)
    {
        const uint32_t r = rng.Below(100);
        if (allowLit && r < 12) return MakeLitPtr(6 + static_cast<int>(rng.Below(2)), PickKind());
        return r < 50 ? MakeAlu(avoid) : MakeMem(avoid, single);
    }

    Item MakeSkip()
    {
        Item it;
        it.type = Item::Skip;
        const int n = static_cast<int>(rng.Below(4));
        int m = static_cast<int>(rng.Below(4));
        if (gFenceSelfOps && m == n) m = (m + 1) & 3;
        uint32_t form = rng.Below(5);
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
        switch (form)
        {
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
        const int kids = 1 + static_cast<int>(rng.Below(3));
        for (int i = 0; i < kids; ++i) it.kids.push_back(MakeSimple(-1, false, true));
        return it;
    }

    Item MakeLoop()
    {
        Item it;
        it.type = Item::Loop;
        it.shape = "loop";
        it.count = 2 + static_cast<int>(rng.Below(3));
        everConst[it.counter] = true;
        const int kids = 1 + static_cast<int>(rng.Below(3));
        for (int i = 0; i < kids; ++i) it.kids.push_back(rng.Chance(40) ? MakeAlu(it.counter) : MakeMem(it.counter, false));
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

    Program Run()
    {
        p.items.push_back(MakeLitPtr(6, PickKind()));
        if (rng.Chance(70)) p.items.push_back(MakeLitPtr(7, PickKind()));
        const int n = 3 + static_cast<int>(rng.Below(12));
        bool polled = false;
        for (int i = 0; i < n; ++i)
        {
            const uint32_t r = rng.Below(100);
            if (r < 22) p.items.push_back(MakeAlu(-1));
            else if (r < 58) p.items.push_back(MakeMem(-1, false));
            else if (r < 64) p.items.push_back(MakeLitPtr(6 + static_cast<int>(rng.Below(2)), PickKind()));
            else if (r < 80) p.items.push_back(MakeSkip());
            else if (r < 87) p.items.push_back(MakeLoop());
            else if (!polled)
            {
                p.items.push_back(MakePoll());
                polled = true;
            }
            else p.items.push_back(MakeMem(-1, false));
        }
        if (rng.Chance(30)) p.tail.push_back(MakeSimple(-1, true, false));
        return p;
    }
};

Program Generate(uint64_t seed)
{
    Gen g(seed);
    return g.Run();
}

// ---- Assembly -------------------------------------------------------------------------------

struct Assembled
{
    std::vector<uint16_t> words;   // the code, any padding, then the literal pool
    size_t codeWords = 0;          // without the pool
    std::vector<uint32_t> litAddr; // per program literal; 0 when unused
    uint32_t poolStart = 0;
};

struct Emitter
{
    struct W
    {
        uint16_t w = 0;
        int fix = 0;   // 1: branch to 'label' (low byte), 2: pool load of literal 'lit' (low byte)
        int label = 0, lit = 0;
    };
    const Program& p;
    std::vector<W> w;
    std::vector<int> labelAt;

    explicit Emitter(const Program& prog) : p(prog) {}
    int NewLabel() { labelAt.push_back(-1); return static_cast<int>(labelAt.size()) - 1; }
    void Place(int label) { labelAt[label] = static_cast<int>(w.size()); }
    void Put(uint16_t word)
    {
        W x;
        x.w = word;
        w.push_back(x);
    }
    void PutBranch(uint16_t opcode, int label)
    {
        W x;
        x.w = opcode;
        x.fix = 1;
        x.label = label;
        w.push_back(x);
    }
    void PutLitLoad(int reg, int lit)
    {
        W x;
        x.w = static_cast<uint16_t>(0xD000 | reg << 8);
        x.fix = 2;
        x.lit = lit;
        w.push_back(x);
    }
    void EmitList(const std::vector<Item>& items)
    {
        for (const Item& it : items) Emit(it);
    }

    void Emit(const Item& it)
    {
        switch (it.type)
        {
        case Item::Alu:
            for (uint16_t x : it.words) Put(x);
            break;
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
        }
    }
};

Assembled Assemble(const Program& p)
{
    Emitter e(p);
    e.EmitList(p.items);
    e.Put(0x000B);   // rts
    if (p.tail.empty()) e.Put(0x0009);
    else e.EmitList(p.tail);
    Assembled a;
    a.codeWords = e.w.size();
    if (a.codeWords & 1) e.Put(0x0009);   // aligns the literal pool
    a.poolStart = kCode + 2 * static_cast<uint32_t>(e.w.size());
    a.litAddr.assign(p.lits.size(), 0);
    uint32_t next = a.poolStart;
    std::vector<uint32_t> order;
    for (const Emitter::W& x : e.w)
        if (x.fix == 2 && a.litAddr[x.lit] == 0)
        {
            a.litAddr[x.lit] = next;
            order.push_back(static_cast<uint32_t>(x.lit));
            next += 4;
        }
    for (size_t i = 0; i < e.w.size(); ++i)
    {
        Emitter::W& x = e.w[i];
        const uint32_t at = kCode + 2 * static_cast<uint32_t>(i);
        if (x.fix == 1)
        {
            const int disp = (e.labelAt[x.label] - static_cast<int>(i) - 2);   // words from PC+4
            x.w = static_cast<uint16_t>((x.w & 0xFF00) | (disp & 0xFF));
        }
        else if (x.fix == 2)
        {
            const int disp = static_cast<int>((a.litAddr[x.lit] - ((at & ~3u) + 4)) / 4);
            x.w = static_cast<uint16_t>((x.w & 0xFF00) | (disp & 0xFF));
        }
        a.words.push_back(x.w);
    }
    for (uint32_t l : order)
    {
        a.words.push_back(static_cast<uint16_t>(p.lits[l].value >> 16));
        a.words.push_back(static_cast<uint16_t>(p.lits[l].value));
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
    static bool IsRam(uint32_t a)
    {
        const uint32_t top = a >> 24;
        return top == 0x06 || top == 0x07 || top == 0x26 || top == 0x27;
    }
    static uint32_t Canon(uint32_t a) { return a < 0xC0000000u ? (a & 0x0FFFFFFFu) : a; }

    uint64_t Read(uint32_t a, int size, uint32_t insn)
    {
        trace.push_back({ insn, false, size, a });
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
        trace.push_back({ insn, true, size, a });
        if (IsRam(a)) Store(a, size, v);
    }
};

// LOAD and STORE are where the memory accesses happen; hooking them gives the exact width and the
// p-code op's instruction (a delay slot's own address, as the decompiler's ops carry), which a
// MemoryBank cannot: it sees aligned words.
class TracingEmulate : public ghidra::EmulatePcodeCache
{
public:
    TracingEmulate(ghidra::Translate* t, ghidra::MemoryState* s, ghidra::BreakTable* b, Env* env)
        : ghidra::EmulatePcodeCache(t, s, b), mEnv(env)
    {
    }

protected:
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
};

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
        mTemps.reset(new ghidra::MemoryHashOverlay(t->getUniqueSpace(), 8, 4096, 4096, nullptr));
        mState.reset(new ghidra::MemoryState(t));
        mState->setMemoryBank(mRamState.get());
        mState->setMemoryBank(mRegs.get());
        mState->setMemoryBank(mTemps.get());
        mBreaks.reset(new ghidra::BreakTableCallBack(t));
        mEmu.reset(new TracingEmulate(t, mState.get(), mBreaks.get(), env));
    }
    ~Machine()
    {
        mEmu.reset();
        mBreaks.reset();
        mState.reset();
    }
    void Set(const char* reg, uint32_t v) { mState->setValue(reg, v); }

    // From the entry until the return address (or the step limit). False if it did not return.
    bool Run(uint32_t start, uint32_t stop, int limit, std::string* error)
    {
        try
        {
            mEmu->setExecuteAddress(ghidra::Address(mRam, start));
            mEmu->setHalt(false);
            for (int i = 0; i < limit; ++i)
            {
                mEmu->executeInstruction();
                if (mEmu->getExecuteAddress().getOffset() == stop) return true;
            }
        }
        catch (ghidra::LowlevelError& e)
        {
            if (error) *error = e.explain;
        }
        return false;
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
};

// What is checked: the code and its literal pool as words from kCode, what the emulator's other
// processor does, and the volatile range the second request marks. A generated program makes one
// (MakeSubject); so does a raw word list from the environment, which is how a finding is reduced
// by hand.
struct Subject
{
    std::vector<uint16_t> words;
    size_t codeWords = 0;                  // the words before the pool (the disassembly stops there)
    std::vector<Env::Poll> polls;          // addresses read busy, then ready
    bool markVolatile = false;             // a second request marks [volFirst, volLast] volatile
    uint32_t volFirst = 0, volLast = 0;
    std::string literals;                  // for the report
};

Subject MakeSubject(const Program& p)
{
    Subject s;
    const Assembled a = Assemble(p);
    s.words = a.words;
    s.codeWords = a.codeWords;
    std::function<void(const std::vector<Item>&)> findPoll = [&](const std::vector<Item>& items) {
        for (const Item& it : items)
            if (it.type == Item::Poll)
                s.polls.push_back({ Env::Canon(p.lits[it.lit].value), it.untilSet ? 0u : 1u, it.untilSet ? 1u : 0u, 0, p.lits[it.lit].value });
    };
    findPoll(p.items);
    std::ostringstream o;
    for (size_t i = 0; i < p.lits.size(); ++i)
        if (a.litAddr[i])
        {
            o << " [" << Hex(a.litAddr[i]) << "]=" << Hex(p.lits[i].value) << " (" << KindName(p.lits[i].kind) << ")";
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
    if (!s.literals.empty()) o << "  literals:" << s.literals << "\n";
    if (s.markVolatile) o << "  second request marks volatile: " << Hex(s.volFirst) << "-" << Hex(s.volLast) << "\n";
    for (const Env::Poll& p : s.polls)
        o << "  polled address " << Hex(p.addr) << ": reads " << p.busy << " " << kPollBusyReads << " times, then " << p.ready << "\n";
    for (const std::string& line : d.Disassemble(snap, kCode, static_cast<int>(s.codeWords)))
        o << "    " << line << "\n";
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

// One decompilation, checked against the trace.
void Check(Sh2Decompiler& d, const std::shared_ptr<MemorySnapshot>& snap, const Subject& subject,
           const std::vector<TraceRec>& trace, bool volatileRun, Report& rep, bool wantText)
{
    DecompileRequest q;
    q.entry = kCode;
    std::vector<HardwareRange> marked;
    if (volatileRun)
    {
        q.volatileMemory.emplace_back(subject.volFirst, subject.volLast);
        marked = VolatileAliases(subject.volFirst, subject.volLast);
    }
    const std::string mode = volatileRun ? "v:" : "n:";
    const auto t0 = std::chrono::steady_clock::now();
    DecompileResult r = d.Decompile(snap, q);
    // The check checked: SE_GENERATED_MUTATE=1 deletes the first access of every ledger, which the
    // check must then report (the run passes only if it does).
    if (gMutate && !r.accesses.empty()) r.accesses.erase(r.accesses.begin());
    rep.decompileMs += dts::MsSince(t0);
    auto add = [&](const std::string& sig, const std::string& detail) { rep.findings.push_back({ mode + sig, detail }); };

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
        // (instruction, direction, size) -> the addresses it touched
        std::map<std::tuple<uint32_t, bool, int>, std::vector<uint32_t>> seen;
        for (const TraceRec& t : trace) seen[std::make_tuple(t.insn, t.write, t.size)].push_back(t.addr);
        for (const auto& entry : seen)
        {
            const uint32_t insn = std::get<0>(entry.first);
            const bool write = std::get<1>(entry.first);
            const int size = std::get<2>(entry.first);
            const std::vector<uint32_t>& addrs = entry.second;
            const size_t index = (insn - kCode) / 2;
            if (insn < kCode || index >= subject.codeWords)
            {
                add("trace-outside", "the emulator accessed memory from " + Hex(insn) + ", outside the function");
                continue;
            }
            // A PC-relative load (mov.l or mov.w @(disp,pc)): a literal-pool word, folded by design.
            const uint16_t word = subject.words[index];
            if (!write && ((word & 0xF000) == 0xD000 || (word & 0xF000) == 0x9000)) continue;
            bool listed = false;
            for (const MemoryAccess& m : r.accesses)
                if (m.insn == insn && m.write == write && m.size == size) listed = true;
            if (listed) continue;

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

            std::string provenance;
            for (const std::string& f : from) provenance += (provenance.empty() ? "" : "+") + f;
            const bool removed = std::binary_search(r.removedInsns.begin(), r.removedInsns.end(), insn);
            add(std::string("dropped:") + (write ? "W" : "R") + std::to_string(size) + ":" + provenance + (removed ? ":removed-unsaid" : ""),
                std::string("the ") + (write ? "write" : "read") + " of " + std::to_string(size) + " byte(s) at " + Hex(insn) + " (" +
                    Hex(addrs[0]) + (addrs.size() > 1 ? ", ..." : "") + "; " + provenance + ") has no access in the output" +
                    (removed ? "; its code was removed and the output does not say so" : ""));
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
        for (size_t i = 0; i < got.size(); ++i)
            for (size_t j = i + 1; j < got.size(); ++j)
            {
                const MemoryAccess& x = got[i];
                const MemoryAccess& y = got[j];
                if (Unordered(x, y)) add("same-statement", Hex(x.insn) + " and " + Hex(y.insn) + " are read in one statement");
                else if (OutOfFlowOrder(fr, x, y))
                    add("out-of-order", Hex(x.insn) + " and " + Hex(y.insn) + " are out of the code's order in block " + std::to_string(x.block));
                else if (FoldedIntoSwitch(fr, x, y))
                    add("folded-into-switch", Hex(x.insn) + " and " + Hex(y.insn) + " were joined into a switch's block across a branch");
            }
        for (const MemoryAccess& m : got)
        {
            std::string what;
            if (PrintedPast(m, got, r.calls, &what)) add("printed-past", "the read at " + Hex(m.insn) + " is printed past " + what);
        }
    }

    if (wantText)
    {
        std::ostringstream o;
        o << "  --- " << (volatileRun ? "range marked volatile" : "default request") << ": status " << int(r.status) << ", "
          << r.elapsedMs << " ms\n";
        for (const std::string& w : r.warnings) o << "  warning: " << w << "\n";
        for (const HardwareNote& n : r.hardwareNotes) o << "  note " << Hex(n.address) << (n.caution ? " (caution)" : "") << ": " << n.text << "\n";
        if (!r.removedInsns.empty())
        {
            o << "  removed:";
            for (uint32_t x : r.removedInsns) o << " " << Hex(x);
            o << "\n";
        }
        o << "  ledger:\n";
        for (const MemoryAccess& m : r.accesses) o << "    " << AccessText(m) << "\n";
        o << "  decompiled:\n";
        for (const Line& l : r.lines) o << "    " << l.text << "\n";
        rep.text += o.str();
    }
}

// Emulates the subject three times, decompiles it, and checks the decompilation against the trace.
Report EvaluateSubject(Sh2Decompiler& d, const Subject& s, bool wantText)
{
    Report rep;
    std::vector<uint8_t> hw;
    for (size_t i = 0; i < s.words.size(); ++i) dts::Put16(hw, kCode - 0x06000000u + 2 * i, s.words[i]);
    dts::Put16(hw, kSentinel - 0x06000000u, 0x0009);   // the return address must decode
    dts::Put16(hw, kSentinel - 0x06000000u + 2, 0x0009);
    auto snap = dts::MakeSnapshot(hw);

    // The ground truth: the same code, three runs with different inputs (so more of the branches
    // are taken one way or the other), their traces united. The other processor answers a poll of
    // hardware always, and a poll of memory the request marks volatile; a poll of unmarked work RAM
    // is ordinary memory to the decompiler (it folds), so the run it is compared with has no one
    // answering, and may never leave the loop (the step limit ends it: what follows did not run).
    std::string engineError;
    auto groundTruth = [&](bool volatileRun) {
        std::vector<Env::Poll> polls;
        for (const Env::Poll& poll : s.polls)
        {
            bool marked = false;
            if (volatileRun && s.markVolatile)
                for (const HardwareRange& h : VolatileAliases(s.volFirst, s.volLast))
                    if (h.first <= poll.raw && poll.raw <= h.last) marked = true;
            if (InHardwareRange(poll.addr) || marked) polls.push_back(poll);
        }
        std::set<TraceRec> united;
        for (int mode = 0; mode < 3; ++mode)
        {
            Env env;
            env.ram = snap->regions[1].bytes;   // HWRAM
            env.hwMode = mode;
            env.polls = polls;
            Machine m(snap, &env);
            Rng init(0xABCD00u + mode);
            for (int r = 0; r < 4; ++r) m.Set(("r" + std::to_string(r)).c_str(), mode == 0 ? 0u : init.Next());
            m.Set("r4", kScratch4);
            m.Set("r5", kScratch5);
            m.Set("r6", kScratch6);
            m.Set("r7", kScratch7);
            m.Set("r15", 0x06030000u);
            m.Set("pr", kSentinel);
            std::string err;
            if (!m.Run(kCode, kSentinel, kStepLimit, &err))
            {
                ++rep.unfinishedRuns;
                if (!err.empty()) engineError = err;
            }
            united.insert(env.trace.begin(), env.trace.end());
        }
        return std::vector<TraceRec>(united.begin(), united.end());
    };
    const std::vector<TraceRec> trace = groundTruth(false);
    std::set<std::tuple<uint32_t, bool, int>> kinds;
    for (const TraceRec& t : trace) kinds.insert(std::make_tuple(t.insn, t.write, t.size));
    Check(d, snap, s, trace, false, rep, wantText);
    std::vector<TraceRec> traceV;
    if (s.markVolatile)
    {
        ++rep.volatileRuns;
        traceV = groundTruth(true);
        for (const TraceRec& t : traceV) kinds.insert(std::make_tuple(t.insn, t.write, t.size));
        Check(d, snap, s, traceV, true, rep, wantText);
    }
    rep.traced = kinds.size();
    if (!engineError.empty()) rep.findings.push_back({ "emulator-error", "the emulator stopped: " + engineError });
    if (wantText)
    {
        std::ostringstream o;
        o << SubjectText(s, d, snap) << "  trace (instruction, direction, size, address):\n";
        for (const TraceRec& t : trace) o << "    " << Hex(t.insn) << " " << (t.write ? "write" : "read ") << " " << t.size << " " << Hex(t.addr) << "\n";
        if (s.markVolatile && traceV != trace)
        {
            o << "  trace with the mailbox marked (the other processor answers it):\n";
            for (const TraceRec& t : traceV) o << "    " << Hex(t.insn) << " " << (t.write ? "write" : "read ") << " " << t.size << " " << Hex(t.addr) << "\n";
        }
        rep.text = o.str() + rep.text;
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
            if (it.type == Item::Skip || it.type == Item::Loop)
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
            if (cur.type == Item::Loop && cur.count > 1)
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
            if (!list[i].kids.empty() && Pass(list[i].kids)) changed = true;
        }
        return changed;
    }

    Program Run()
    {
        while (evals < budget)
        {
            bool changed = Pass(prog.items);
            if (!prog.tail.empty())
            {
                Item saved = prog.tail[0];
                prog.tail.clear();
                if (Still()) changed = true;
                else prog.tail.push_back(saved);
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
        CountShapes(it.slot, shapes);
        CountShapes(it.kids, shapes);
    }
}

long EnvNumber(const char* name, long fallback)
{
    const char* v = std::getenv(name);
    return v && *v ? std::strtol(v, nullptr, 10) : fallback;
}

void TestGenerated()
{
    Sh2Decompiler d(gConfig);
    gMutate = EnvNumber("SE_GENERATED_MUTATE", 0) != 0;
    if (const char* fence = std::getenv("SE_GENERATED_FENCE"))
    {
        gFenceFolds = std::strstr(fence, "folds") != nullptr;
        gFenceSelfOps = gFenceFolds || std::strstr(fence, "selfops") != nullptr;
    }
    if (const char* raw = std::getenv("SE_GENERATED_WORDS"))
    {
        // A hand-written (or pasted) function instead of generated ones: hex words from kCode, the
        // literal pool among them. SE_GENERATED_VOLATILE=<first>-<last> adds the second request
        // that marks that range; SE_GENERATED_POLL=<address>[:until-set] makes that address read
        // busy a few times and then ready.
        Subject s;
        std::istringstream in(raw);
        std::string tok;
        while (in >> tok) s.words.push_back(static_cast<uint16_t>(std::strtoul(tok.c_str(), nullptr, 16)));
        s.codeWords = s.words.size();
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
        const Report rep = EvaluateSubject(d, s, true);
        std::printf("%s", rep.text.c_str());
        for (const Finding& f : rep.findings) std::printf("  [%s] %s\n", f.sig.c_str(), f.detail.c_str());
        std::printf("%zu finding(s)\n", rep.findings.size());
        CHECK(rep.findings.empty());
        return;
    }
    const long single = EnvNumber("SE_GENERATED_SEED", 0);
    const long seeds = single ? 1 : EnvNumber("SE_GENERATED_SEEDS", 300);
    const long first = single ? single : EnvNumber("SE_GENERATED_FIRST", 1);
    const long shrinkCount = EnvNumber("SE_GENERATED_SHRINK", 3);
    const bool verbose = single || EnvNumber("SE_GENERATED_VERBOSE", 0) != 0;
    const int detailLimit = 3;   // failing seeds printed in full without SE_GENERATED_VERBOSE

    std::map<std::string, int> shapes, classes;
    int failing = 0, totalFindings = 0, unfinished = 0, volatileRuns = 0, shrunk = 0;
    size_t traced = 0;
    double decompileMs = 0;
    const auto t0 = std::chrono::steady_clock::now();
    for (long seed = first; seed < first + seeds; ++seed)
    {
        const Program p = Generate(static_cast<uint64_t>(seed));
        CountShapes(p.items, shapes);
        if (!p.tail.empty()) ++shapes["rts.slot_insn"];
        const bool detail = verbose || failing < detailLimit;
        Report rep = Evaluate(d, p, detail);
        unfinished += rep.unfinishedRuns;
        volatileRuns += rep.volatileRuns;
        traced += rep.traced;
        decompileMs += rep.decompileMs;
        if (single) std::printf("%s", rep.text.c_str());
        if (rep.findings.empty()) continue;

        ++failing;
        totalFindings += static_cast<int>(rep.findings.size());
        std::set<std::string> sigs;
        for (const Finding& f : rep.findings) sigs.insert(f.sig);
        for (const std::string& s : sigs) ++classes[s];
        std::printf("FINDING seed %ld:\n", seed);
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
    char covered[640];
    std::snprintf(covered, sizeof covered,
                  "alu=%d loads=%d stores=%d (plain %d disp %d indexed %d inc/dec %d) tas=%d widths b/w/l=%d/%d/%d literal pointers hw/ram/mailbox=%d/%d/%d "
                  "skips=%d (delayed %d) loops=%d polls=%d (write first %d, until set %d; mailbox %d hw %d ram %d) rts slot insn=%d",
                  sum("alu."), sum("mem.ld."), sum("mem.st."), n("mem.ld.plain") + n("mem.st.plain"), n("mem.ld.disp") + n("mem.st.disp"),
                  n("mem.ld.indexed") + n("mem.st.indexed"), n("mem.ld.inc") + n("mem.st.dec"), n("mem.tas"), n("width.1"), n("width.2"),
                  n("width.4"), n("litptr.hw") + n("poll.hw"), n("litptr.ram") + n("poll.ram"), n("litptr.mbox") + n("poll.mbox"), sum("skip."),
                  n("skip.bt_s") + n("skip.bf_s"), n("loop"), sum("poll.h") + sum("poll.r") + sum("poll.m"), n("poll.write_first"),
                  n("poll.until_set"), n("poll.mbox"), n("poll.hw"), n("poll.ram"), n("rts.slot_insn"));
    std::printf("finding classes (seeds):");
    for (const auto& c : classes) std::printf(" %s=%d", c.first.c_str(), c.second);
    std::printf("\nshapes:");
    for (const auto& s : shapes) std::printf(" %s=%d", s.first.c_str(), s.second);
    std::printf("\n");
    const double ms = dts::MsSince(t0);
    std::printf("SUMMARY: seeds=%ld first=%ld shapes covered: %s; second requests (mailbox marked)=%d; traced accesses=%zu; "
                "unfinished emulator runs=%d; failing seeds=%d; findings=%d (%.0f ms total, %.0f ms in the decompiler, %.1f ms per seed)\n",
                seeds, first, covered, volatileRuns, traced, unfinished, failing, totalFindings, ms, decompileMs, ms / double(seeds));
    if (gMutate)
    {
        std::printf("mutation check: %d of %ld seeds reported the deleted access\n", failing, seeds);
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
    try
    {
        RUN(TestGenerated());
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
    return dts::Finish("Sh2GeneratedTests", start);
}
