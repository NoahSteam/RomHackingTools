// rulefuzz13: every integer rule of the engine's pool, by value, on random p-code graphs.
//
// Built for area 2 round 13 after A2-80 (RuleRightShiftAnd) and A2-81 (RuleSLess2Zero::getHiBit): two rules whose constant/shift/high-bit arithmetic was
// wrong only for edge values. The tool builds a random expression graph of integer ops (sizes 1, 2, 4, 8) over a few input varnodes, with constants
// drawn from the edges of each width (0, 1, the top bit, all ones, width-1, width, width+1, 2*width, ...), puts it in a basic block with a sink COPY,
// computes the non-zero masks (Funcdata::calcNZMask), and then runs the pool's rules to a fixpoint, one application at a time. After every application
// (whether it reports a change or not) the graph is evaluated by an independent evaluator (the definition of each op written here) for a set of input
// vectors and compared with the value of the original graph: the first application that changes a value is the finding. It also checks that every
// varnode's non-zero mask holds every value it takes (an unsound mask is what a rule trusts).
//
// usage: rulefuzz13 <scratch dir> <first seed> <count> [rule name filter] [depth]
//   env  RF_VERBOSE=1       print the graph of every finding
//        RF_ONLY=<name>     run only that rule
//        RF_NOMASK=1        skip the nzmask soundness check
// build: see build13.sh
#include "Decompiler/SpecBundle.h"
#include "libdecomp.hh"
#include "loadimage.hh"
#include "opbehavior.hh"
#include "rangeutil.hh"
#include "ruleaction.hh"
#include "subflow.hh"
#include "sleigh_arch.hh"
#include "funcdata.hh"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <sstream>
#include <string>
#include <vector>

using namespace ghidra;

namespace {

const uint32_t kBase = 0x06004000u;

class RegionImage : public LoadImage
{
public:
    RegionImage() : LoadImage("rf") {}
    void loadFill(uint1* ptr, int4 size, const Address& addr) override
    {
        (void)addr;
        std::memset(ptr, 0, size);
    }
    std::string getArchType() const override { return "saturn"; }
    void adjustVma(long) override {}
};

class RfArch : public SleighArchitecture
{
public:
    RfArch(RegionImage* image, std::ostream* err) : SleighArchitecture("rf", "SuperH:BE:32:SH-2:default", err), mImage(image) {}

private:
    void buildLoader(DocumentStorage&) override
    {
        collectSpecFiles(*errorstream);
        loader = mImage;
    }
    void resolveArchitecture() override
    {
        archid = getTarget();
        SleighArchitecture::resolveArchitecture();
    }
    void postSpecFile() override { Architecture::postSpecFile(); }
    RegionImage* mImage;
};

// ---- the definition of each op ---------------------------------------------------------------------------------------------------------------
uint64_t Mask(int size) { return size >= 8 ? ~static_cast<uint64_t>(0) : ((static_cast<uint64_t>(1) << (8 * size)) - 1); }
int64_t Signed(uint64_t v, int size)
{
    if (size >= 8) return static_cast<int64_t>(v);
    const uint64_t sign = static_cast<uint64_t>(1) << (8 * size - 1);
    return static_cast<int64_t>((v ^ sign) - sign);
}

bool gWide = false;
bool gSh2 = false;       // only the ops and shift amounts the SH-2 spec emits (no carry/borrow ops, no counts, no division, amounts 1 2 8 16 31 32)
bool gUndef = false;      // a division by zero or an overflowing division was met: the environment is not used
bool gUnsupported = false;
OpCode gUnsupportedOp = CPUI_COPY;

struct Env
{
    std::map<std::pair<uint64_t, int>, uint64_t> v;      // (register offset, size) -> value
};

uint64_t Eval(const Varnode* vn, const Env& env, int depth = 0)
{
    const int size = vn->getSize();
    const uint64_t m = Mask(size);
    if (vn->isConstant()) return vn->getOffset() & m;
    if (depth > 200) { gUnsupported = true; return 0; }
    const PcodeOp* d = vn->getDef();
    if (d == nullptr)
    {
        auto it = env.v.find({ vn->getOffset(), size });
        if (it == env.v.end()) { gUnsupported = true; gUnsupportedOp = CPUI_MULTIEQUAL; return 0; }
        return it->second & m;
    }
    const int n = d->numInput();
    const uint64_t a = n > 0 ? Eval(d->getIn(0), env, depth + 1) : 0, b = n > 1 ? Eval(d->getIn(1), env, depth + 1) : 0;
    const int sa = n > 0 ? d->getIn(0)->getSize() : 0, sb = n > 1 ? d->getIn(1)->getSize() : 0;
    const uint64_t wa = 8 * static_cast<uint64_t>(sa);
    switch (d->code())
    {
    case CPUI_COPY: return a & m;
    case CPUI_INT_ADD: return (a + b) & m;
    case CPUI_INT_SUB: return (a - b) & m;
    case CPUI_INT_AND: return a & b;
    case CPUI_INT_OR: return a | b;
    case CPUI_INT_XOR: return a ^ b;
    case CPUI_INT_NEGATE: return ~a & m;
    case CPUI_INT_2COMP: return (0 - a) & m;
    case CPUI_INT_MULT: return (a * b) & m;
    case CPUI_INT_ZEXT: return a;
    case CPUI_INT_SEXT: return static_cast<uint64_t>(Signed(a, sa)) & m;
    case CPUI_SUBPIECE: return b >= 8 ? 0 : (a >> (8 * b)) & m;
    case CPUI_PIECE: return ((sb >= 8 ? 0 : (a << (8 * sb))) | b) & m;
    case CPUI_INT_LEFT: return b >= static_cast<uint64_t>(8 * size) ? 0 : (a << b) & m;
    case CPUI_INT_RIGHT: return b >= wa ? 0 : a >> b;
    case CPUI_INT_SRIGHT: return static_cast<uint64_t>(Signed(a, sa) >> (b >= wa ? wa - 1 : b)) & m;
    case CPUI_INT_DIV: if (b == 0) { gUndef = true; return 0; } return (a / b) & m;
    case CPUI_INT_REM: if (b == 0) { gUndef = true; return 0; } return (a % b) & m;
    case CPUI_INT_SDIV:
    {
        const int64_t x = Signed(a, sa), y = Signed(b, sb);
        if (y == 0 || (y == -1 && static_cast<uint64_t>(x) == (static_cast<uint64_t>(1) << (wa - 1)))) { gUndef = true; return 0; }
        return static_cast<uint64_t>(x / y) & m;
    }
    case CPUI_INT_SREM:
    {
        const int64_t x = Signed(a, sa), y = Signed(b, sb);
        if (y == 0 || (y == -1 && static_cast<uint64_t>(x) == (static_cast<uint64_t>(1) << (wa - 1)))) { gUndef = true; return 0; }
        return static_cast<uint64_t>(x % y) & m;
    }
    case CPUI_INT_CARRY: return ((a + b) & Mask(sa)) < a;
    case CPUI_INT_SCARRY:
    {
        const __int128 s = static_cast<__int128>(Signed(a, sa)) + Signed(b, sa), lim = static_cast<__int128>(1) << (wa - 1);
        return (s < -lim || s >= lim) ? 1 : 0;
    }
    case CPUI_INT_SBORROW:
    {
        const __int128 s = static_cast<__int128>(Signed(a, sa)) - Signed(b, sa), lim = static_cast<__int128>(1) << (wa - 1);
        return (s < -lim || s >= lim) ? 1 : 0;
    }
    case CPUI_INT_EQUAL: return a == b;
    case CPUI_INT_NOTEQUAL: return a != b;
    case CPUI_INT_LESS: return a < b;
    case CPUI_INT_LESSEQUAL: return a <= b;
    case CPUI_INT_SLESS: return Signed(a, sa) < Signed(b, sb);
    case CPUI_INT_SLESSEQUAL: return Signed(a, sa) <= Signed(b, sb);
    case CPUI_BOOL_NEGATE: return (a ^ 1) & 1;
    case CPUI_BOOL_AND: return a & b & 1;
    case CPUI_BOOL_OR: return (a | b) & 1;
    case CPUI_BOOL_XOR: return (a ^ b) & 1;
    case CPUI_POPCOUNT: return static_cast<uint64_t>(__builtin_popcountll(a)) & m;
    case CPUI_LZCOUNT:
    {
        int c = 0;
        for (int i = static_cast<int>(wa) - 1; i >= 0 && !((a >> i) & 1); --i) ++c;
        return static_cast<uint64_t>(c) & m;
    }
    default: break;
    }
    gUnsupported = true;
    gUnsupportedOp = d->code();
    return 0;
}

// ---- random ----------------------------------------------------------------------------------------------------------------------------------
struct Rng
{
    uint64_t s;
    explicit Rng(uint64_t seed) : s(seed * 0x9E3779B97F4A7C15ull + 12345) { next(); next(); }
    uint64_t next()
    {
        uint64_t z = (s += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    int below(int n) { return static_cast<int>(next() % static_cast<uint64_t>(n)); }
    bool chance(int percent) { return below(100) < percent; }
};

std::vector<uint64_t> EdgeConsts(int size, Rng& r)
{
    const uint64_t m = Mask(size);
    const int w = 8 * size;
    std::vector<uint64_t> v = { 0, 1, 2, 3, 4, 5, 7, 8, 15, 16, 255 & m, 0x7f & m, 0x80 & m, 0xf0 & m, 0x0f & m, 0xff00 & m, 0xffff & m, 0x8000 & m, 0x7fff & m, 0xffff0000ull & m,
                                m, m - 1, m - 2, m >> 1, (m >> 1) + 1, (m >> 1) + 2, (m >> 1) - 1, 0x80000000ull & m, 0x7fffffffull & m, 0xfffffffeull & m, 0xff000000ull & m, 0x00ffffffull & m,
                                0x55555555ull & m, 0xaaaaaaaaull & m, 0x0000ffffull & m, 0x01010101ull & m,
                                static_cast<uint64_t>(w), static_cast<uint64_t>(w - 1), static_cast<uint64_t>(w + 1), static_cast<uint64_t>(2 * w), 31, 32, 33, 63, 64, 65 };
    for (int k = 0; k < w; ++k) { v.push_back((static_cast<uint64_t>(1) << k) & m); v.push_back(((static_cast<uint64_t>(1) << k) - 1) & m); v.push_back((m << k) & m); }
    for (int k = 0; k < 4; ++k) v.push_back(r.next() & m);
    return v;
}

struct Builder
{
    Funcdata* fd;
    AddrSpace* reg;
    BlockBasic* bb;
    Address entry;
    Rng& rng;
    int maxDepth;
    std::map<int, std::vector<Varnode*>> inputs;       // by size
    std::map<int, std::vector<Varnode*>> made;         // sub-expressions, by size, for reuse
    std::vector<std::pair<uint64_t, int>> inputKeys;
    std::vector<Varnode*> boolInputs;
    std::set<uint64_t> boolKeys;
    uint64_t nextReg = 0x100;
    std::set<uint64_t> constSeen;
    int budget;

    Builder(Funcdata* f, AddrSpace* r, BlockBasic* b, const Address& e, Rng& g, int d) : fd(f), reg(r), bb(b), entry(e), rng(g), maxDepth(d), budget(14) {}

    Varnode* input(int size)
    {
        auto& v = inputs[size];
        if (v.size() < 3 && (v.empty() || rng.chance(35)))
        {
            Varnode* vn = fd->newVarnode(size, reg, nextReg);
            nextReg += 16;
            vn = fd->setInputVarnode(vn);
            v.push_back(vn);
            inputKeys.push_back({ vn->getOffset(), size });
            return vn;
        }
        return v[rng.below(static_cast<int>(v.size()))];
    }
    uint64_t constant(int size)
    {
        const uint64_t m = Mask(size);
        const int r = rng.below(100);
        if (r < 12) return (m >> 1) + 1;
        if (r < 17) return m;
        if (r < 21) return 0;
        if (r < 25) return 1;
        if (r < 28) return m >> 1;
        std::vector<uint64_t> e = EdgeConsts(size, rng);
        return e[rng.below(static_cast<int>(e.size()))];
    }
    Varnode* konst(int size)
    {
        const uint64_t c = constant(size);
        constSeen.insert(c);
        return fd->newConstant(size, c);
    }
    Varnode* shiftAmount()
    {
        if (gSh2)
        {
            static const uint64_t sh2amounts[] = { 1, 1, 2, 8, 16, 31, 32 };
            return fd->newConstant(4, sh2amounts[rng.below(7)]);
        }
        static const uint64_t amounts[] = { 0, 1, 1, 2, 3, 4, 7, 8, 8, 9, 15, 16, 16, 17, 23, 24, 31, 31, 32, 32, 33, 40, 48, 56, 63, 64, 65, 100, 255, 0x80000000ull, 0xffffffffull, 0x100000000ull };
        uint64_t a = amounts[rng.below(sizeof(amounts) / sizeof(amounts[0]))];
        // SH-2 code reaches shift amounts up to 32 (shll16, the << 32 of mac) and `rm & 0x1f`; the huge amounts (>= 2^31) are the latent class unless RF_WIDE=1
        if (!gWide && a >= 0x80000000ull) a = 64 + rng.below(200);
        int sz = a >= 0x100000000ull ? 8 : (a >= 256 ? 4 : (rng.chance(50) ? 1 : 4));
        return fd->newConstant(sz, a);
    }
    PcodeOp* emit(OpCode opc, int outSize, Varnode* a, Varnode* b = nullptr, Varnode* c = nullptr)
    {
        PcodeOp* op = fd->newOp(c ? 3 : (b ? 2 : 1), entry);
        fd->opSetOpcode(op, opc);
        fd->newUniqueOut(outSize, op);
        fd->opSetInput(op, a, 0);
        if (b) fd->opSetInput(op, b, 1);
        if (c) fd->opSetInput(op, c, 2);
        fd->opInsertEnd(op, bb);
        return op;
    }
    Varnode* boolInput()
    {
        auto& v = boolInputs;
        if (v.size() < 2 && (v.empty() || rng.chance(40)))
        {
            Varnode* vn = fd->newVarnode(1, reg, nextReg);
            nextReg += 16;
            vn = fd->setInputVarnode(vn);
            vn->updateType(fd->getArch()->types->getBase(1, TYPE_BOOL), true, true);
            v.push_back(vn);
            inputKeys.push_back({ vn->getOffset(), 1 });
            boolKeys.insert(vn->getOffset());
            return vn;
        }
        return v[rng.below(static_cast<int>(v.size()))];
    }
    Varnode* genBool(int depth)
    {
        if (depth <= 0 || rng.chance(20)) return boolInput();
        Varnode* out;
        const int k = rng.below(10);
        if (k < 5 || budget <= 0) { --budget; out = genCmp(depth); }
        else if (k < 6 && !gSh2) { --budget; out = genCarry(depth); }
        else if (k < 7) { Varnode* a = genBool(depth - 1); out = emit(CPUI_BOOL_NEGATE, 1, a)->getOut(); }
        else
        {
            --budget;
            Varnode* a = genBool(depth - 1);
            Varnode* b = genBool(depth - 1);
            out = emit(k == 7 ? CPUI_BOOL_AND : (k == 8 ? CPUI_BOOL_OR : CPUI_BOOL_XOR), 1, a, b)->getOut();
        }
        return out;
    }
    Varnode* genCmp(int depth)
    {
        static const OpCode ops[] = { CPUI_INT_EQUAL, CPUI_INT_NOTEQUAL, CPUI_INT_LESS, CPUI_INT_LESSEQUAL, CPUI_INT_SLESS, CPUI_INT_SLESSEQUAL };
        const int s = 1 << rng.below(4);
        Varnode* a = gen(s, depth - 1);
        Varnode* b = rng.chance(60) ? konst(s) : gen(s, depth - 1);
        if (rng.chance(30)) b = fd->newConstant(s, rng.chance(60) ? 0 : Mask(s));        // the sign tests
        if (a->isConstant() && b->isConstant()) a = input(s);
        if (rng.chance(25)) std::swap(a, b);
        OpCode opc = ops[rng.below(6)];
        if (b->isConstant() && rng.chance(40)) opc = rng.chance(70) ? CPUI_INT_SLESS : CPUI_INT_SLESSEQUAL;
        return emit(opc, 1, a, b)->getOut();
    }
    Varnode* genCarry(int depth)
    {
        static const OpCode ops[] = { CPUI_INT_CARRY, CPUI_INT_SCARRY, CPUI_INT_SBORROW };
        const int s = 1 << rng.below(4);
        Varnode* a = gen(s, depth - 1);
        Varnode* b = rng.chance(60) ? konst(s) : gen(s, depth - 1);
        if (a->isConstant() && b->isConstant()) a = input(s);
        return emit(ops[rng.below(3)], 1, a, b)->getOut();
    }
    // A leaf that is a small motif over an input: the places where rules look at masks, signs and bits (a masked sign, a shifted sign, a widened bool, ...)
    Varnode* motif(int size)
    {
        const int w = 8 * size;
        Varnode* x = input(size);
        const uint64_t m = Mask(size);
        switch (rng.below(16))
        {
        case 0: return emit(CPUI_INT_AND, size, x, fd->newConstant(size, (m >> 1) + 1))->getOut();                   // x & hibit
        case 1: return emit(CPUI_INT_AND, size, x, fd->newConstant(size, m >> 1))->getOut();                          // x & ~hibit
        case 2: return emit(CPUI_INT_SRIGHT, size, x, fd->newConstant(4, w - 1))->getOut();                           // sign
        case 3: return emit(CPUI_INT_RIGHT, size, x, fd->newConstant(4, w - 1))->getOut();                            // 0 or 1
        case 4:
        {
            Varnode* b = genBool(1);
            if (size == 1) return emit(CPUI_INT_LEFT, 1, b, fd->newConstant(4, 7))->getOut();
            Varnode* z = emit(CPUI_INT_ZEXT, size, b)->getOut();
            return emit(CPUI_INT_LEFT, size, z, fd->newConstant(4, w - 1))->getOut();                                  // bool << (w-1)
        }
        case 5:
            if (size >= 2) { Varnode* a = input(size / 2); return emit(rng.chance(50) ? CPUI_INT_SEXT : CPUI_INT_ZEXT, size, a)->getOut(); }
            return emit(CPUI_INT_AND, size, x, konst(size))->getOut();
        case 6: return emit(CPUI_INT_AND, size, x, konst(size))->getOut();
        case 7: return emit(CPUI_INT_OR, size, x, konst(size))->getOut();
        case 8: return emit(CPUI_INT_NEGATE, size, x)->getOut();
        case 9: return emit(CPUI_INT_2COMP, size, x)->getOut();
        case 10: return emit(CPUI_INT_MULT, size, x, konst(size))->getOut();
        case 11:
            if (size >= 2) { const int lo = 1 + rng.below(size - 1); return emit(CPUI_PIECE, size, input(size - lo), input(lo))->getOut(); }
            return x;
        case 12: return emit(CPUI_INT_LEFT, size, x, shiftAmount())->getOut();
        case 13: return emit(CPUI_INT_RIGHT, size, x, shiftAmount())->getOut();
        case 14: return emit(CPUI_INT_SRIGHT, size, x, shiftAmount())->getOut();
        default:
            if (size <= 4) return emit(CPUI_SUBPIECE, size, input(size * 2), fd->newConstant(4, rng.below(size + 1)))->getOut();
            return x;
        }
    }
    Varnode* leaf(int size)
    {
        if (rng.chance(28) && budget > 0) { --budget; return motif(size); }
        if (rng.chance(55)) return input(size);
        return konst(size);
    }
    Varnode* gen(int size, int depth)
    {
        if (depth <= 0 || budget <= 0 || rng.chance(depth == maxDepth ? 0 : 22)) return leaf(size);
        auto& pool = made[size];
        if (!pool.empty() && rng.chance(12)) return pool[rng.below(static_cast<int>(pool.size()))];
        --budget;
        Varnode* out = genOp(size, depth);
        if (!out->isConstant()) pool.push_back(out);
        return out;
    }
    Varnode* genOp(int size, int depth)
    {
        // candidate op kinds for an output of this size
        enum { BIN, SHIFT, UN, EXT, SUB, PIECEOP, CMP, BOOLOP, CARRY, COUNT, DIVOP, SELF } kinds[16];
        int nk = 0;
        kinds[nk++] = BIN; kinds[nk++] = BIN; kinds[nk++] = BIN;
        kinds[nk++] = SHIFT; kinds[nk++] = SHIFT;
        kinds[nk++] = UN;
        if (size >= 2) { kinds[nk++] = EXT; kinds[nk++] = EXT; kinds[nk++] = PIECEOP; }
        if (size <= 4) { kinds[nk++] = SUB; kinds[nk++] = SUB; }
        if (size == 1) { kinds[nk++] = CMP; kinds[nk++] = CMP; kinds[nk++] = CMP; kinds[nk++] = BOOLOP; if (!gSh2) kinds[nk++] = CARRY; }
        if (!gSh2) { kinds[nk++] = COUNT; kinds[nk++] = DIVOP; }
        switch (kinds[rng.below(nk)])
        {
        case BIN:
        {
            static const OpCode ops[] = { CPUI_INT_ADD, CPUI_INT_ADD, CPUI_INT_SUB, CPUI_INT_AND, CPUI_INT_AND, CPUI_INT_OR, CPUI_INT_XOR, CPUI_INT_MULT };
            const OpCode opc = ops[rng.below(sizeof(ops) / sizeof(ops[0]))];
            Varnode* a = gen(size, depth - 1);
            Varnode* b = rng.chance(45) ? konst(size) : (rng.chance(25) ? motif(size) : gen(size, depth - 1));
            if (a->isConstant() && b->isConstant()) a = input(size);
            if (rng.chance(30) && b->isConstant()) std::swap(a, b);
            return emit(opc, size, a, b)->getOut();
        }
        case SHIFT:
        {
            static const OpCode ops[] = { CPUI_INT_LEFT, CPUI_INT_RIGHT, CPUI_INT_SRIGHT };
            const OpCode opc = ops[rng.below(3)];
            Varnode* a = gen(size, depth - 1);
            if (a->isConstant()) a = input(size);
            Varnode* b = (gSh2 || rng.chance(88)) ? shiftAmount() : gen(rng.chance(50) ? 1 : size, depth - 1);
            if (b->isConstant() && !gWide && b->getOffset() > 255) b = shiftAmount();
            return emit(opc, size, a, b)->getOut();
        }
        case UN:
        {
            static const OpCode ops[] = { CPUI_INT_NEGATE, CPUI_INT_2COMP };
            Varnode* a = gen(size, depth - 1);
            if (a->isConstant()) a = input(size);
            return emit(ops[rng.below(2)], size, a)->getOut();
        }
        case EXT:
        {
            int from = 1;
            do from = 1 << rng.below(3); while (from >= size);
            Varnode* a = gen(from, depth - 1);
            if (a->isConstant()) a = input(from);
            return emit(rng.chance(50) ? CPUI_INT_ZEXT : CPUI_INT_SEXT, size, a)->getOut();
        }
        case SUB:
        {
            int from = size * (1 << (1 + rng.below(2)));
            if (from > 8) from = 8;
            if (from <= size) return leaf(size);
            Varnode* a = gen(from, depth - 1);
            if (a->isConstant()) a = input(from);
            const int off = rng.below(from - size + 1);
            return emit(CPUI_SUBPIECE, size, a, fd->newConstant(4, off))->getOut();
        }
        case PIECEOP:
        {
            const int lo = 1 + rng.below(size - 1), hi = size - lo;
            Varnode* a = gen(hi, depth - 1);
            Varnode* b = gen(lo, depth - 1);
            if (a->isConstant() && b->isConstant()) b = input(lo);
            return emit(CPUI_PIECE, size, a, b)->getOut();
        }
        case CMP: return genCmp(depth);
        case BOOLOP: return genBool(depth);
        case CARRY: return genCarry(depth);
        case COUNT:
        {
            const int s = 1 << rng.below(4);
            Varnode* a = gen(s, depth - 1);
            if (a->isConstant()) a = input(s);
            return emit(rng.chance(50) ? CPUI_POPCOUNT : CPUI_LZCOUNT, size, a)->getOut();
        }
        case DIVOP:
        {
            static const OpCode ops[] = { CPUI_INT_DIV, CPUI_INT_SDIV, CPUI_INT_REM, CPUI_INT_SREM };
            Varnode* a = gen(size, depth - 1);
            if (a->isConstant()) a = input(size);
            Varnode* b = rng.chance(75) ? konst(size) : gen(size, depth - 1);
            if (b->isConstant() && b->getOffset() == 0) b = fd->newConstant(size, 1 + rng.below(7));
            return emit(ops[rng.below(4)], size, a, b)->getOut();
        }
        default: break;
        }
        return leaf(size);
    }
};

// ---- the pool --------------------------------------------------------------------------------------------------------------------------------
#define RF_RULES(X) \
    X(RuleTermOrder) X(RuleSelectCse) X(RuleCollectTerms) X(RulePullsubMulti) X(RulePullsubIndirect) X(RuleSborrow) X(RuleScarry) \
    X(RuleIntLessEqual) X(RuleTrivialArith) X(RuleTrivialBool) X(RuleTrivialShift) X(RuleSignShift) X(RuleTestSign) X(RuleIdentityEl) X(RuleOrMask) \
    X(RuleAndMask) X(RuleOrConsume) X(RuleOrCollapse) X(RuleAndOrLump) X(RuleShiftBitops) X(RuleRightShiftAnd) X(RuleNotDistribute) X(RuleHighOrderAnd) \
    X(RuleAndDistribute) X(RuleAndCommute) X(RuleAndPiece) X(RuleAndZext) X(RuleAndCompare) X(RuleDoubleSub) X(RuleDoubleShift) X(RuleDoubleArithShift) \
    X(RuleConcatShift) X(RuleLeftRight) X(RuleShiftCompare) X(RuleShift2Mult) X(RuleShiftPiece) X(RuleMultiCollapse) X(RuleAliasUpdate) X(Rule2Comp2Mult) \
    X(RuleSub2Add) X(RuleCarryElim) X(RuleBxor2NotEqual) X(RuleLess2Zero) X(RuleLessEqual2Zero) X(RuleSLess2Zero) X(RuleEqual2Zero) X(RuleEqual2Constant) \
    X(RuleThreeWayCompare) X(RuleXorCollapse) X(RuleAddMultCollapse) X(RuleCollapseConstants) X(RulePropagateCopy) X(RuleZextEliminate) X(RuleSlessToLess) \
    X(RuleZextSless) X(RuleBitUndistribute) X(RuleBooleanUndistribute) X(RuleBooleanDedup) X(RuleBoolZext) X(RuleBooleanNegate) X(RuleLogic2Bool) \
    X(RuleSubExtComm) X(RuleSubCommute) X(RuleConcatCommute) X(RuleConcatZext) X(RuleZextCommute) X(RuleZextShiftZext) X(RuleShiftAnd) X(RuleConcatZero) \
    X(RuleConcatLeftShift) X(RuleSubZext) X(RuleSubCancel) X(RuleShiftSub) X(RuleHumptyDumpty) X(RuleDumptyHump) X(RuleHumptyOr) X(RuleNegateIdentity) \
    X(RuleSubNormal) X(RulePositiveDiv) X(RuleDivTermAdd) X(RuleDivTermAdd2) X(RuleDivOpt) X(RuleSignForm) X(RuleSignForm2) X(RuleSignDiv2) X(RuleDivChain) \
    X(RuleSignNearMult) X(RuleModOpt) X(RuleSignMod2nOpt) X(RuleSignMod2nOpt2) X(RuleSignMod2Opt) X(RuleBoolNegate) X(RuleLessEqual) X(RuleLessNotEqual) \
    X(RuleLessOne) X(RuleRangeMeld) X(RulePiece2Zext) X(RulePiece2Sext) X(RulePopcountBoolXor) X(RuleXorSwap) X(RuleLzcountShiftBool) X(RuleOrCompare) \
    X(RuleSubvarAnd) X(RuleSubvarSubpiece) X(RuleSubvarCompZero) X(RuleSubvarShift) X(RuleSubvarZext) X(RuleSubvarSext) X(RuleNegateNegate)

#define RF_CLEANUP_RULES(X) X(RuleMultNegOne) X(RuleAddUnsigned) X(Rule2Comp2Sub) X(RuleDumptyHumpLate) X(RuleSubRight) X(RuleExtensionPush) X(RulePieceStructure) X(RuleAndStructure)

struct PoolEntry
{
    bool cleanup = false;
    Rule* rule;
    std::vector<uint4> ops;
    std::string name;
    long applied = 0, tested = 0, bad = 0, thrown = 0;
};

std::vector<PoolEntry> MakePool(const std::string& only)
{
    std::vector<PoolEntry> pool;
#define RF_ADD(cls) \
    if (only.empty() || only == #cls) { PoolEntry e; e.rule = new cls("analysis"); e.name = #cls; e.rule->getOpList(e.ops); pool.push_back(e); }
    RF_RULES(RF_ADD)
#undef RF_ADD
#define RF_ADD(cls) \
    if (only.empty() || only == #cls) { PoolEntry e; e.rule = new cls("cleanup"); e.name = #cls; e.cleanup = true; e.rule->getOpList(e.ops); pool.push_back(e); }
    RF_CLEANUP_RULES(RF_ADD)
#undef RF_ADD
    return pool;
}

// ---- dump ------------------------------------------------------------------------------------------------------------------------------------
std::string VnText(const Varnode* vn, std::map<const Varnode*, int>& ids)
{
    std::ostringstream o;
    if (vn->isConstant()) { o << "#0x" << std::hex << vn->getOffset() << ":" << std::dec << vn->getSize(); return o.str(); }
    if (vn->isInput()) { o << "in" << std::hex << vn->getOffset() << ":" << std::dec << vn->getSize(); return o.str(); }
    auto it = ids.find(vn);
    if (it == ids.end()) it = ids.insert({ vn, static_cast<int>(ids.size()) }).first;
    o << "v" << it->second << ":" << vn->getSize();
    return o.str();
}

std::string Dump(Funcdata* fd)
{
    std::map<const Varnode*, int> ids;
    std::ostringstream o;
    for (auto it = fd->beginOpAlive(); it != fd->endOpAlive(); ++it)
    {
        const PcodeOp* op = *it;
        if (op->getOut()) o << VnText(op->getOut(), ids) << " = ";
        o << op->getOpcode()->getName();
        for (int i = 0; i < op->numInput(); ++i) o << (i ? ", " : " ") << VnText(op->getIn(i), ids);
        o << " ; ";
    }
    return o.str();
}

uint64_t Hash(Funcdata* fd)
{
    uint64_t h = 1469598103934665603ull;
    auto mix = [&](uint64_t x) { h = (h ^ x) * 1099511628211ull; };
    for (auto it = fd->beginOpAlive(); it != fd->endOpAlive(); ++it)
    {
        const PcodeOp* op = *it;
        mix(static_cast<uint64_t>(op->code()));
        mix(reinterpret_cast<uintptr_t>(op->getOut()));
        for (int i = 0; i < op->numInput(); ++i)
        {
            const Varnode* v = op->getIn(i);
            mix(v->isConstant() ? v->getOffset() * 31 + v->getSize() : reinterpret_cast<uintptr_t>(v));
        }
    }
    return h;
}

}  // namespace

int main(int argc, char** argv)
{
    if (argc < 4) { std::printf("usage: %s <scratch dir> <first seed> <count> [depth]\n", argv[0]); return 2; }
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    const std::string scratch = argv[1];
    const uint64_t first = std::strtoull(argv[2], nullptr, 10), count = std::strtoull(argv[3], nullptr, 10);
    const int depth = argc > 4 ? std::atoi(argv[4]) : 3;
    const char* only = std::getenv("RF_ONLY");
    const bool verbose = std::getenv("RF_VERBOSE") != nullptr;
    const bool nomask = std::getenv("RF_NOMASK") != nullptr;
    gWide = std::getenv("RF_WIDE") != nullptr;
    gSh2 = std::getenv("RF_SH2") != nullptr;
    const int showMax = std::getenv("RF_SHOW") ? std::atoi(std::getenv("RF_SHOW")) : 3;

    // the engine, with its spec materialised as the app does
    const sfe::decomp::SpecBundleResult b = sfe::decomp::MaterialiseSh2SpecBundle(scratch);
    if (b.dir.empty()) { std::printf("no spec bundle\n"); return 2; }
    startDecompilerLibrary(std::vector<std::string>{ b.dir });
    RegionImage* image = new RegionImage();
    std::ostringstream errs;
    RfArch arch(image, &errs);
    DocumentStorage store;
    arch.init(store);
    const Address entry(arch.getDefaultCodeSpace(), kBase);
    std::string fname;
    arch.nameFunction(entry, fname);
    Funcdata* fd = arch.symboltab->getGlobalScope()->addFunction(entry, fname)->getFunction();
    AddrSpace* reg = arch.getSpaceByName("register");
    if (!reg) { std::printf("no register space\n"); return 2; }

    std::vector<PoolEntry> pool = MakePool(only ? only : "");
    std::map<std::string, long> unsupported;
    long graphs = 0, findings = 0, maskBad = 0, unsupportedCount = 0, loops = 0;
    std::map<std::string, int> shown;
    const auto t0 = std::chrono::steady_clock::now();

    for (uint64_t seed = first; seed < first + count; ++seed)
    {
        Rng rng(seed);
        fd->clear();
        BlockGraph& graph = const_cast<BlockGraph&>(fd->getBasicBlocks());
        BlockBasic* bb = graph.newBlockBasic(fd);
        graph.setStartBlock(bb);
        Builder bld(fd, reg, bb, entry, rng, depth);
        const int rootSize = 1 << rng.below(4);
        bld.budget = 6 + rng.below(8);
        Varnode* root = bld.gen(rootSize, depth);
        if (root->isConstant() || bld.inputKeys.empty()) continue;
        // the sink
        {
            PcodeOp* op = fd->newOp(1, entry);
            fd->opSetOpcode(op, CPUI_COPY);
            fd->newVarnodeOut(rootSize, Address(reg, 0x8000), op);
            fd->opSetInput(op, root, 0);
            fd->opInsertEnd(op, bb);
        }
        PcodeOp* sink = nullptr;
        for (auto it = fd->beginOpAlive(); it != fd->endOpAlive(); ++it) sink = *it;
        // all ops consume everything
        for (auto it = fd->beginOpAlive(); it != fd->endOpAlive(); ++it)
            if ((*it)->getOut()) (*it)->getOut()->setConsume(calc_mask((*it)->getOut()->getSize()));
        fd->calcNZMask();

        // environments
        std::vector<Env> envs;
        std::vector<uint64_t> interesting(bld.constSeen.begin(), bld.constSeen.end());
        int totalBits = 0;
        for (const auto& k : bld.inputKeys) totalBits += bld.boolKeys.count(k.first) ? 1 : 8 * k.second;
        const bool exhaustive = totalBits <= 10;
        const int nenv = exhaustive ? (1 << totalBits) : 160;
        for (int e = 0; e < nenv; ++e)
        {
            if (exhaustive)
            {
                Env env;
                int shift = 0;
                for (const auto& k : bld.inputKeys)
                {
                    const int bits = bld.boolKeys.count(k.first) ? 1 : 8 * k.second;
                    env.v[k] = (static_cast<uint64_t>(e) >> shift) & ((static_cast<uint64_t>(1) << bits) - 1);
                    shift += bits;
                }
                envs.push_back(env);
                continue;
            }
            Env env;
            for (const auto& k : bld.inputKeys)
            {
                const uint64_t m = Mask(k.second);
                uint64_t val;
                if (bld.boolKeys.count(k.first)) { env.v[k] = rng.below(2); continue; }
                const int pick = rng.below(10);
                if (pick < 4) { std::vector<uint64_t> ed = EdgeConsts(k.second, rng); val = ed[rng.below(static_cast<int>(ed.size()))]; }
                else if (pick < 6 && !interesting.empty()) { val = interesting[rng.below(static_cast<int>(interesting.size()))] + (rng.below(3) - 1); }
                else val = rng.next();
                env.v[k] = val & m;
            }
            envs.push_back(env);
        }
        auto evalAll = [&](std::vector<uint64_t>& out, std::vector<char>& defined) {
            out.assign(envs.size(), 0);
            defined.assign(envs.size(), 1);
            for (size_t i = 0; i < envs.size(); ++i)
            {
                gUndef = false;
                out[i] = Eval(sink->getIn(0), envs[i]);
                if (gUndef) defined[i] = 0;
            }
        };
        std::vector<uint64_t> before, cur;
        std::vector<char> def0, def1;
        gUnsupported = false;
        evalAll(before, def0);
        if (gUnsupported) continue;
        ++graphs;
        const std::string original = Dump(fd);

        // the non-zero masks hold every value
        auto checkMasks = [&](const std::string& where) {
            if (nomask) return;
            for (auto it = fd->beginOpAlive(); it != fd->endOpAlive(); ++it)
            {
                const Varnode* o = (*it)->getOut();
                if (!o || o == sink->getOut()) continue;
                for (size_t i = 0; i < envs.size(); ++i)
                {
                    gUndef = false;
                    const uint64_t val = Eval(o, envs[i]);
                    if (gUndef) continue;
                    if (val & ~o->getNZMask())
                    {
                        ++maskBad;
                        if (shown["mask"]++ < showMax)
                            std::printf("MASK seed %llu %s: %s has the value %llx outside its nzmask %llx; graph: %s\n", (unsigned long long)seed, where.c_str(), (*it)->getOpcode()->getName().c_str(),
                                        (unsigned long long)val, (unsigned long long)o->getNZMask(), Dump(fd).c_str());
                        return;
                    }
                }
            }
        };
        checkMasks("initial");

        std::string prev = original;
        uint64_t prevHash = Hash(fd);
        int applications = 0;
        bool stop = false;
        // one application of one rule to one op; returns false when the graph is finished (a finding, an unsupported op, an exception)
        auto step = [&](PoolEntry& pe, PcodeOp* op, int& rcOut) -> bool {
            int rc = 0;
            ++pe.tested;
            try { rc = pe.rule->applyOp(op, *fd); }
            catch (LowlevelError& e)
            {
                ++pe.thrown;
                if (e.explain.find("Divide by 0") == std::string::npos && e.explain.find("Remainder by 0") == std::string::npos && shown["throw:" + pe.name]++ < 1)
                    std::printf("THROW %s seed %llu: %s; graph: %s\n", pe.name.c_str(), (unsigned long long)seed, e.explain.c_str(), prev.c_str());
                return false;
            }
            rcOut = rc;
            const uint64_t h = Hash(fd);
            if (rc > 0) { ++pe.applied; fd->calcNZMask(); ++applications; }
            if (h == prevHash && rc == 0) return true;
            if (rc <= 0) fd->calcNZMask();
            gUnsupported = false;
            evalAll(cur, def1);
            if (gUnsupported)
            {
                ++unsupportedCount;
                unsupported[std::string(get_opname(gUnsupportedOp)) + " after " + pe.name]++;
                return false;
            }
            bool bad = false;
            size_t at = 0;
            for (size_t i = 0; i < envs.size(); ++i)
                if (def0[i] && cur[i] != before[i]) { bad = true; at = i; break; }
            if (bad)
            {
                ++pe.bad;
                ++findings;
                if (shown[pe.name]++ < showMax)
                {
                    std::printf("WRONG %s seed %llu (rc %d): value %llx before, %llx after, for", pe.name.c_str(), (unsigned long long)seed, rc, (unsigned long long)before[at], (unsigned long long)cur[at]);
                    for (const auto& kv : envs[at].v) std::printf(" in%llx:%d=%llx", (unsigned long long)kv.first.first, kv.first.second, (unsigned long long)kv.second);
                    std::printf("\n   original: %s\n   before the rule: %s\n   after: %s\n", original.c_str(), prev.c_str(), Dump(fd).c_str());
                }
                return false;
            }
            checkMasks(pe.name);
            prev = Dump(fd);
            prevHash = h;
            return true;
        };
        auto handles = [](const PoolEntry& pe, OpCode c) {
            for (uint4 opc : pe.ops) if (opc == static_cast<uint4>(c)) return true;
            return false;
        };
        const bool ruleMajor = std::getenv("RF_RULEMAJOR") != nullptr;
        for (int phase = 0; phase < 2 && !stop; ++phase)       // the analysis rules to a fixpoint, then the cleanup rules (twice)
        {
            for (int pass = 0; pass < (phase == 0 ? 12 : 2) && !stop; ++pass)
            {
                const int before_apps = applications;
                if (ruleMajor)
                {
                    for (PoolEntry& pe : pool)
                    {
                        if (stop) break;
                        if (pe.cleanup != (phase == 1)) continue;
                        std::vector<PcodeOp*> cands;
                        for (auto it = fd->beginOpAlive(); it != fd->endOpAlive(); ++it)
                            if (handles(pe, (*it)->code())) cands.push_back(*it);
                        for (PcodeOp* op : cands)
                        {
                            if (op->isDead()) continue;
                            int rc;
                            if (!step(pe, op, rc)) { stop = true; break; }
                        }
                    }
                }
                else
                {
                    std::vector<PcodeOp*> ops;
                    for (auto it = fd->beginOpAlive(); it != fd->endOpAlive(); ++it) ops.push_back(*it);
                    for (PcodeOp* op : ops)
                    {
                        if (stop) break;
                        size_t ri = 0;
                        while (ri < pool.size() && !stop)
                        {
                            if (op->isDead()) break;
                            PoolEntry& pe = pool[ri++];
                            if (pe.cleanup != (phase == 1) || !handles(pe, op->code())) continue;
                            const OpCode oc = op->code();
                            int rc = 0;
                            if (!step(pe, op, rc)) { stop = true; break; }
                            if (rc > 0 && !op->isDead() && op->code() != oc) ri = 0;
                        }
                    }
                }
                if (applications > 200) { ++loops; break; }
                if (applications == before_apps) break;
            }
        }
        if (verbose && graphs % 1000 == 0) std::printf("seed %llu: %ld graphs\n", (unsigned long long)seed, graphs);
    }
    std::printf("rulefuzz13: seeds %llu..%llu depth %d: %ld graphs, %ld wrong applications, %ld unsound masks, %ld unsupported, %ld loops, %.0f s\n", (unsigned long long)first,
                (unsigned long long)(first + count - 1), depth, graphs, findings, maskBad, unsupportedCount, loops,
                std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
    for (const auto& kv : unsupported) std::printf("  unsupported: %s x%ld\n", kv.first.c_str(), kv.second);
    for (const PoolEntry& pe : pool)
        std::printf("RULE %-26s tested %8ld applied %7ld bad %4ld thrown %3ld\n", pe.name.c_str(), pe.tested, pe.applied, pe.bad, pe.thrown);
    return findings + maskBad ? 1 : 0;
}
