// divexh14: the division recognisers of patch 0064 (RuleDivOpt, RuleDivTermAdd, RuleDivTermAdd2, RuleDivChain) judged by value on every constant a small
// width has, against an independent description of what the machine computes (plain arithmetic, no graph).
//
// Area 2 round 14. Round 13's pins and fuzzer sampled sixteen constants and exempted the environments in which the original multiply wraps. Here:
//   mode opt   <xs> <first y> <count>  RuleDivOpt alone on `ext(x) * y` (x of xs bytes, the multiply 2*xs bytes wide): every constant y of the range, both
//              extensions, the three forms the rule reads (shift of the product, shift of a SUBPIECE of it, SUBPIECE alone), both shifts, every amount, three readers;
//              every x of 1 byte (exhaustive) or 4096 values of 2 and 4 bytes. One character per configuration goes to <out>: '0' the rule did nothing,
//              'C' it rewrote the graph and the reader sees the machine's value for every x, 'W' it rewrote and some x differs.
//   mode term  <xs> <first y> <count>  the shapes of RuleDivTermAdd (unsigned and signed `hi + x`, a shift of the sum, the signed correction) and RuleDivTermAdd2
//              (`W + ((x - W) >> 1)`) with the whole analysis pool run to a fixpoint (a flagged exact product left over is a wrapped multiply to the evaluator).
//              'c' correct, no division made; 'C' correct and divided; 'w' / 'W' wrong (without / with a division).
// usage: divexh14 <scratch dir> <mode> <xs> <first> <count> <out file>   (the file holds one character per configuration, in a fixed order)
// compare two engines (the head before patch 0064 against the tree) with divexh_cmp14.py
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
AddrSpace* gRegSpace = nullptr;
bool gWrap = false;       // an INT_MULT overflowed its width under both the unsigned and the signed reading (a division recogniser reasons about the exact product)
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
        if (it != env.v.end()) return it->second & m;
        // a piece of a known input (the sub-variable flow makes a smaller input inside the register): the space is big-endian
        for (const auto& kv : env.v)
        {
            const uint64_t base = kv.first.first;
            const int rsz = kv.first.second;
            if (vn->getOffset() >= base && vn->getOffset() + size <= base + rsz && vn->getSpace() == gRegSpace)
            {
                const int shift = 8 * (rsz - static_cast<int>(vn->getOffset() - base) - size);
                return (shift >= 64 ? 0 : (kv.second >> shift)) & m;
            }
        }
        gUnsupported = true; gUnsupportedOp = CPUI_MULTIEQUAL; return 0;
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
    case CPUI_INT_MULT:
    {
        const unsigned __int128 pu = static_cast<unsigned __int128>(a) * b;
        const __int128 ps = static_cast<__int128>(Signed(a, size)) * Signed(b, size);
        const __int128 lim = static_cast<__int128>(1) << (8 * size - 1);
        {   // the reading of the operands that the graph itself gives: a multiplicand extended by ZEXT is unsigned, by SEXT signed; otherwise either reading may fit
            bool uns = false, sgn = false;
            for (int i = 0; i < 2; ++i)
            {
                const Varnode* in = d->getIn(i);
                if (in->isWritten() && in->getDef()->code() == CPUI_INT_ZEXT) uns = true;
                if (in->isWritten() && in->getDef()->code() == CPUI_INT_SEXT) sgn = true;
            }
            const bool fitU = pu <= m, fitS = ps >= -lim && ps < lim;
            if (uns && !sgn) { if (!fitU) gWrap = true; }
            else if (sgn && !uns) { if (!fitS) gWrap = true; }
            else if (!fitU && !fitS) gWrap = true;
        }
        return (a * b) & m;
    }
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
        o << get_opname(op->code());
        for (int i = 0; i < op->numInput(); ++i) o << (i ? ", " : " ") << VnText(op->getIn(i), ids);
        o << " ; ";
    }
    return o.str();
}

// ---- a graph in a block ----------------------------------------------------------------------------------------------------------------------
struct Gr
{
    Funcdata* fd = nullptr;
    AddrSpace* reg = nullptr;
    BlockBasic* bb = nullptr;
    Address entry;
    uint64_t nextReg = 0x100;
    Varnode* xin = nullptr;
    void reset()
    {
        fd->clear();
        BlockGraph& g = const_cast<BlockGraph&>(fd->getBasicBlocks());
        bb = g.newBlockBasic(fd);
        g.setStartBlock(bb);
        nextReg = 0x100;
    }
    Varnode* input(int size)
    {
        Varnode* v = fd->newVarnode(size, reg, nextReg);
        nextReg += 16;
        return xin = fd->setInputVarnode(v);
    }
    Varnode* op(OpCode opc, int outSize, Varnode* a, Varnode* b = nullptr)
    {
        PcodeOp* o = fd->newOp(b ? 2 : 1, entry);
        fd->opSetOpcode(o, opc);
        Varnode* out = fd->newUniqueOut(outSize, o);
        fd->opSetInput(o, a, 0);
        if (b) fd->opSetInput(o, b, 1);
        fd->opInsertEnd(o, bb);
        return out;
    }
    Varnode* k(int size, uint64_t v) { return fd->newConstant(size, v & Mask(size)); }
    // the sink reads the root, everything is consumed
    Varnode* sink(Varnode* root)
    {
        PcodeOp* o = fd->newOp(1, entry);
        fd->opSetOpcode(o, CPUI_COPY);
        fd->newVarnodeOut(root->getSize(), Address(reg, 0x8000), o);
        fd->opSetInput(o, root, 0);
        fd->opInsertEnd(o, bb);
        return o->getOut();
    }
    void consumeAll()
    {
        for (auto it = fd->beginOpAlive(); it != fd->endOpAlive(); ++it)
            if ((*it)->getOut()) (*it)->getOut()->setConsume(calc_mask((*it)->getOut()->getSize()));
    }
};

// the machine's value of a varnode for x (the one input), as the p-code defines it; a flagged exact product that was left over wraps here like any multiply
bool gUndefined = false;
bool gUnsup = false;
uint64_t gX = 0;
int gXSize = 0;
uint64_t gXOff = 0;
AddrSpace* gReg = nullptr;
uint64_t Val(const Varnode* vn, int depth = 0)
{
    const int size = vn->getSize();
    const uint64_t m = Mask(size);
    if (vn->isConstant()) return vn->getOffset() & m;
    if (depth > 300) { gUnsup = true; return 0; }
    const PcodeOp* d = vn->getDef();
    if (d == nullptr)
    {
        if (vn->getSpace() == gReg && vn->getOffset() >= gXOff && vn->getOffset() + size <= gXOff + gXSize)
        {
            const int shift = 8 * (gXSize - static_cast<int>(vn->getOffset() - gXOff) - size);
            return (shift >= 64 ? 0 : (gX >> shift)) & m;
        }
        gUnsup = true;
        return 0;
    }
    const int n = d->numInput();
    const uint64_t a = n > 0 ? Val(d->getIn(0), depth + 1) : 0, b = n > 1 ? Val(d->getIn(1), depth + 1) : 0;
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
    case CPUI_INT_DIV: if (b == 0) { gUndefined = true; return 0; } return (a / b) & m;
    case CPUI_INT_REM: if (b == 0) { gUndefined = true; return 0; } return (a % b) & m;
    case CPUI_INT_SDIV:
    {
        const int64_t x = Signed(a, sa), y = Signed(b, sb);
        if (y == 0 || (y == -1 && static_cast<uint64_t>(x) == (static_cast<uint64_t>(1) << (wa - 1)))) { gUndefined = true; return 0; }
        return static_cast<uint64_t>(x / y) & m;
    }
    case CPUI_INT_SREM:
    {
        const int64_t x = Signed(a, sa), y = Signed(b, sb);
        if (y == 0 || (y == -1 && static_cast<uint64_t>(x) == (static_cast<uint64_t>(1) << (wa - 1)))) { gUndefined = true; return 0; }
        return static_cast<uint64_t>(x % y) & m;
    }
    case CPUI_INT_EQUAL: return a == b;
    case CPUI_INT_NOTEQUAL: return a != b;
    case CPUI_INT_LESS: return a < b;
    case CPUI_INT_SLESS: return Signed(a, sa) < Signed(b, sb);
    case CPUI_INT_LESSEQUAL: return a <= b;
    case CPUI_INT_SLESSEQUAL: return Signed(a, sa) <= Signed(b, sb);
    default: break;
    }
    gUnsup = true;
    return 0;
}
bool HasOp(Funcdata* fd, OpCode c)
{
    for (auto it = fd->beginOpAlive(); it != fd->endOpAlive(); ++it) if ((*it)->code() == c) return true;
    return false;
}
bool HasFlagged(Funcdata* fd)
{
    for (auto it = fd->beginOpAlive(); it != fd->endOpAlive(); ++it) if ((*it)->code() == CPUI_INT_MULT && (*it)->isDivProductWide()) return true;
    return false;
}

// the values of x tried: all of a byte, else edges, the neighbourhood of the multiples of the likely divisor, and random ones
std::vector<uint64_t> XValues(int xs, uint64_t y, uint64_t seed)
{
    std::vector<uint64_t> v;
    const uint64_t m = Mask(xs);
    if (xs == 1) { for (uint64_t i = 0; i < 256; ++i) v.push_back(i); return v; }
    Rng r(seed ^ (y * 0x9E3779B97F4A7C15ull));
    const uint64_t hib = (m >> 1) + 1;
    for (uint64_t e : { (uint64_t)0, (uint64_t)1, (uint64_t)2, m, m - 1, hib, hib - 1, hib + 1, m >> 2, (m >> 2) + 1, hib | (hib >> 1), (uint64_t)3, (uint64_t)7, (uint64_t)255, (uint64_t)256, (uint64_t)0xFFFF, (uint64_t)0x10000 }) v.push_back(e & m);
    // around the multiples of 2^(8w)/y
    if (y > 1)
    {
        const unsigned __int128 two = static_cast<unsigned __int128>(1) << (16 * xs);
        const uint64_t q = static_cast<uint64_t>(two / y);
        for (uint64_t mult : { (uint64_t)1, (uint64_t)2, (uint64_t)3, (uint64_t)5, (uint64_t)100 })
            for (int dv = -1; dv <= 1; ++dv) v.push_back((mult * q + dv) & m);
    }
    while (v.size() < 4096) v.push_back(r.next() & m);
    return v;
}

// ---- mode opt ----------------------------------------------------------------------------------------------------------------------------------
struct OptCfg
{
    int xs, w;
    int sext;          // 0 zext, 1 sext, 2 the value masked to xs bytes and not extended (the engine takes its extension from the non-zero mask)
    int form;          // 0 shift of the product, 1 shift of SUBPIECE(product), 2 SUBPIECE alone
    bool arith;        // SRIGHT
    int cut;           // bytes cut by the SUBPIECE (forms 1, 2)
    int amount;        // the shift
    int reader;        // 0 whole, 1 low half, 2 & 0xFF
};

// the machine's value of the reader, from the definition alone
uint64_t OptOrig(const OptCfg& c, uint64_t y, uint64_t x)
{
    const uint64_t mw = Mask(c.w);
    const uint64_t X = c.sext == 1 ? (static_cast<uint64_t>(Signed(x, c.xs)) & mw) : (c.sext == 2 ? (x & Mask(c.xs)) : x);
    const uint64_t P = (X * y) & mw;
    uint64_t R;
    int outSize;
    auto shiftRight = [&](uint64_t v, int size, int a) -> uint64_t {
        if (a >= 8 * size) return c.arith ? (static_cast<uint64_t>(Signed(v, size) >> (8 * size - 1)) & Mask(size)) : 0;
        return c.arith ? (static_cast<uint64_t>(Signed(v, size) >> a) & Mask(size)) : (v >> a);
    };
    if (c.form == 0) { outSize = c.w; R = shiftRight(P, c.w, c.amount); }
    else
    {
        outSize = c.w - c.cut;
        const uint64_t S = (P >> (8 * c.cut)) & Mask(outSize);
        R = c.form == 1 ? shiftRight(S, outSize, c.amount) : S;
    }
    if (c.reader == 0) return R;
    if (c.reader == 1) return R & Mask(outSize / 2);
    return R & 0xFF;
}

// returns the outcome character
char OptOne(Gr& t, const OptCfg& c, uint64_t y, uint64_t seed, std::vector<uint64_t>& xvals)
{
    t.reset();
    Varnode* x = t.input(c.sext == 2 ? c.w : c.xs);
    Varnode* xe = c.sext == 2 ? t.op(CPUI_INT_AND, c.w, x, t.k(c.w, Mask(c.xs))) : t.op(c.sext ? CPUI_INT_SEXT : CPUI_INT_ZEXT, c.w, x);
    Varnode* prod = t.op(CPUI_INT_MULT, c.w, xe, t.k(c.w, y));
    Varnode* root;
    int outSize = c.w;
    if (c.form == 0) root = t.op(c.arith ? CPUI_INT_SRIGHT : CPUI_INT_RIGHT, c.w, prod, t.k(4, c.amount));
    else
    {
        outSize = c.w - c.cut;
        Varnode* sp = t.op(CPUI_SUBPIECE, outSize, prod, t.k(4, c.cut));
        root = c.form == 1 ? t.op(c.arith ? CPUI_INT_SRIGHT : CPUI_INT_RIGHT, outSize, sp, t.k(4, c.amount)) : sp;
    }
    Varnode* read;
    if (c.reader == 0) read = t.op(CPUI_COPY, outSize, root);
    else if (c.reader == 1) read = t.op(CPUI_SUBPIECE, outSize / 2, root, t.k(4, 0));
    else read = t.op(CPUI_INT_AND, outSize, root, t.k(outSize, 0xFF));
    t.consumeAll();
    t.fd->calcNZMask();
    RuleDivOpt rule("scratch");
    int rc;
    try { rc = rule.applyOp(root->getDef(), *t.fd); }
    catch (LowlevelError&) { return 'X'; }
    if (rc == 0) return '0';
    xvals = XValues(c.xs, y, seed);
    if (c.sext == 2)
    {
        // the masked value: the xs low bytes as above, the rest of the register random (the mask must drop it)
        Rng hr(seed ^ y);
        const size_t n0 = xvals.size();
        for (size_t i = 0; i < n0; ++i) xvals[i] |= (hr.next() << (8 * c.xs)) & Mask(c.w);
    }
    gXSize = x->getSize(); gXOff = x->getOffset();
    for (uint64_t xv : xvals)
    {
        gX = xv; gUndefined = false; gUnsup = false;
        const uint64_t got = Val(read);
        if (gUnsup) return 'U';
        if (gUndefined) continue;
        if (got != OptOrig(c, y, xv)) return 'W';
    }
    return 'C';
}

void RunOpt(Gr& t, int xs, uint64_t first, uint64_t count, const char* outFile)
{
    const int w = 2 * xs;
    std::string out;
    out.reserve(count * 100);
    long rewritten = 0, wrong = 0, total = 0;
    int shown = 0;
    std::vector<uint64_t> xv;
    for (uint64_t y = first; y < first + count; ++y)
    {
        // xs 1: every constant; xs 2 and 4: the constants are a list, y is the index (see YList)
        const uint64_t yy = y;
        for (int sext = 0; sext < 3; ++sext)
            for (int form = 0; form < 3; ++form)
                for (int arith = 0; arith < 2; ++arith)
                {
                    if (form == 2 && arith) continue;
                    const int amounts = form == 0 ? 8 * w : (form == 1 ? 8 * (w - 1) : 1);
                    for (int cut = (form == 0 ? 0 : 1); cut <= (form == 0 ? 0 : (w - 1 >= 2 ? 2 : 1)); ++cut)
                    {
                        if (form != 0 && cut >= w) continue;
                        for (int amount = 0; amount < amounts; ++amount)
                        {
                            // the amounts of a longer width are sampled: the edges and a stride
                            if (w > 2 && amount > 3 && amount < amounts - 3 && (amount % 3) != 0) continue;
                            for (int reader = 0; reader < 3; ++reader)
                            {
                                OptCfg c{ xs, w, sext, form, arith != 0, cut, amount, reader };
                                if (form != 0 && reader == 1 && (w - cut) < 2) { out.push_back('-'); continue; }
                                if (form == 0 && reader == 1 && w < 2) { out.push_back('-'); continue; }
                                if (form == 2) c.amount = 0;
                                const char ch = OptOne(t, c, yy, 0x5eed, xv);
                                ++total;
                                if (ch == 'C' || ch == 'W') ++rewritten;
                                if (ch == 'W')
                                {
                                    ++wrong;
                                    if (shown++ < 20)
                                        std::printf("WRONG RuleDivOpt: %d bytes %s of %d by %llx, form %d, %s, cut %d, amount %d, reader %d\n", w, sext == 1 ? "sext" : (sext == 2 ? "and" : "zext"), xs, (unsigned long long)yy, form, arith ? "s>>" : ">>", cut, amount, reader);
                                }
                                out.push_back(ch);
                            }
                        }
                    }
                }
    }
    FILE* fp = std::fopen(outFile, "wb");
    if (fp) { std::fwrite(out.data(), 1, out.size(), fp); std::fclose(fp); }
    std::printf("divexh14 opt xs=%d y=%llu..%llu: %ld configurations, %ld rewritten, %ld wrong\n", xs, (unsigned long long)first, (unsigned long long)(first + count - 1), total, rewritten, wrong);
}

// ---- mode term ---------------------------------------------------------------------------------------------------------------------------------
struct TermCfg
{
    int xs, w;
    int kind;       // 0 unsigned hi + x, 1 signed hi + x, 2 W + ((x - W) >> 1)
    int d, s;       // a shift of hi, a shift of the sum
    int order;      // operand order of the ADD
    int fix;        // signed: 0 none, 1 + (R >>u top), 2 - (x s>> top)
};
uint64_t TermOrig(const TermCfg& c, uint64_t y, uint64_t x)
{
    const uint64_t mx = Mask(c.xs), mw = Mask(c.w);
    auto rs = [&](uint64_t v, int size, int a, bool arith) -> uint64_t {
        if (a >= 8 * size) return arith ? (static_cast<uint64_t>(Signed(v, size) >> (8 * size - 1)) & Mask(size)) : 0;
        return arith ? (static_cast<uint64_t>(Signed(v, size) >> a) & Mask(size)) : (v >> a);
    };
    if (c.kind == 0 || c.kind == 1)
    {
        const bool sg = c.kind == 1;
        const uint64_t X = sg ? (static_cast<uint64_t>(Signed(x, c.xs)) & mw) : x;
        const uint64_t P = (X * y) & mw;
        uint64_t H = (P >> (8 * (c.w - c.xs))) & mx;
        if (c.d) H = rs(H, c.xs, c.d, sg);
        uint64_t S = (H + x) & mx;
        uint64_t R = c.s ? rs(S, c.xs, c.s, sg) : S;
        if (sg && c.fix == 1) R = (R + rs(R, c.xs, 8 * c.xs - 1, false)) & mx;
        if (sg && c.fix == 2) R = (R - rs(x, c.xs, 8 * c.xs - 1, true)) & mx;
        return R;
    }
    const uint64_t P = (x * y) & mw;
    const uint64_t W = (P >> (8 * (c.w - c.xs))) & mx;
    const uint64_t T = (x - W) & mx;
    const uint64_t Q = T >> 1;
    const uint64_t Z = (Q + W) & mx;
    return c.s ? rs(Z, c.xs, c.s, false) : Z;
}

std::vector<PoolEntry> gPool;
long gStepLimit = 400;

char TermOne(Gr& t, const TermCfg& c, uint64_t y, uint64_t seed, std::vector<uint64_t>& xvals, std::string* dump)
{
    t.reset();
    Varnode* x = t.input(c.xs);
    Varnode* root;
    if (c.kind == 0 || c.kind == 1)
    {
        const bool sg = c.kind == 1;
        Varnode* xe = t.op(sg ? CPUI_INT_SEXT : CPUI_INT_ZEXT, c.w, x);
        Varnode* prod = t.op(CPUI_INT_MULT, c.w, xe, t.k(c.w, y));
        Varnode* hi = t.op(CPUI_SUBPIECE, c.xs, prod, t.k(4, c.w - c.xs));
        if (c.d) hi = t.op(sg ? CPUI_INT_SRIGHT : CPUI_INT_RIGHT, c.xs, hi, t.k(4, c.d));
        Varnode* sum = c.order ? t.op(CPUI_INT_ADD, c.xs, x, hi) : t.op(CPUI_INT_ADD, c.xs, hi, x);
        root = c.s ? t.op(sg ? CPUI_INT_SRIGHT : CPUI_INT_RIGHT, c.xs, sum, t.k(4, c.s)) : sum;
        if (sg && c.fix == 1) root = t.op(CPUI_INT_ADD, c.xs, root, t.op(CPUI_INT_RIGHT, c.xs, root, t.k(4, 8 * c.xs - 1)));
        if (sg && c.fix == 2) root = t.op(CPUI_INT_SUB, c.xs, root, t.op(CPUI_INT_SRIGHT, c.xs, x, t.k(4, 8 * c.xs - 1)));
    }
    else
    {
        Varnode* xe = t.op(CPUI_INT_ZEXT, c.w, x);
        Varnode* prod = t.op(CPUI_INT_MULT, c.w, xe, t.k(c.w, y));
        Varnode* W = t.op(CPUI_SUBPIECE, c.xs, prod, t.k(4, c.w - c.xs));
        Varnode* negW = t.op(CPUI_INT_MULT, c.xs, W, t.k(c.xs, Mask(c.xs)));
        Varnode* T = c.order ? t.op(CPUI_INT_ADD, c.xs, negW, x) : t.op(CPUI_INT_ADD, c.xs, x, negW);
        Varnode* Q = t.op(CPUI_INT_RIGHT, c.xs, T, t.k(4, 1));
        Varnode* Z = c.order ? t.op(CPUI_INT_ADD, c.xs, W, Q) : t.op(CPUI_INT_ADD, c.xs, Q, W);
        root = c.s ? t.op(CPUI_INT_RIGHT, c.xs, Z, t.k(4, c.s)) : Z;
    }
    Varnode* snk = t.sink(root);
    (void)snk;
    t.consumeAll();
    t.fd->calcNZMask();
    for (PoolEntry& pe : gPool) pe.applied = 0;
    int applications = 0;
    auto handles = [](const PoolEntry& pe, OpCode oc) { for (uint4 o : pe.ops) if (o == static_cast<uint4>(oc)) return true; return false; };
    try
    {
        for (int phase = 0; phase < 2; ++phase)
            for (int pass = 0; pass < (phase == 0 ? 12 : 2); ++pass)
            {
                const int before = applications;
                std::vector<PcodeOp*> ops;
                for (auto it = t.fd->beginOpAlive(); it != t.fd->endOpAlive(); ++it) ops.push_back(*it);
                for (PcodeOp* op : ops)
                {
                    size_t ri = 0;
                    while (ri < gPool.size())
                    {
                        if (op->isDead()) break;
                        PoolEntry& pe = gPool[ri++];
                        if (pe.cleanup != (phase == 1) || !handles(pe, op->code())) continue;
                        const OpCode oc = op->code();
                        const int rc = pe.rule->applyOp(op, *t.fd);
                        if (rc > 0) { t.fd->calcNZMask(); ++applications; }
                        if (rc > 0 && !op->isDead() && op->code() != oc) ri = 0;
                    }
                }
                if (applications > gStepLimit || applications == before) break;
            }
    }
    catch (LowlevelError&) { return 'X'; }
    xvals = XValues(c.xs, y, seed);
    gXSize = c.xs; gXOff = x->getOffset();
    const bool divided = HasOp(t.fd, CPUI_INT_DIV) || HasOp(t.fd, CPUI_INT_SDIV);
    bool wrongv = false;
    Varnode* outv = snk;
    for (uint64_t xv : xvals)
    {
        gX = xv; gUndefined = false; gUnsup = false;
        const uint64_t got = Val(outv);
        if (gUnsup) return 'U';
        if (gUndefined) continue;
        if (got != TermOrig(c, y, xv)) { wrongv = true; break; }
    }
    if (wrongv && dump) *dump = Dump(t.fd) + (HasFlagged(t.fd) ? " [flagged product left over]" : "");
    return wrongv ? (divided ? 'W' : 'w') : (divided ? 'C' : 'c');
}

uint64_t Hash64(uint64_t a, uint64_t b) { Rng r(a * 0x9E3779B97F4A7C15ull + b); return r.next(); }

void RunTerm(Gr& t, int xs, const std::vector<uint64_t>& ys, uint64_t first, uint64_t count, const char* outFile)
{
    const int w = 2 * xs;
    std::string out;
    long wrong = 0, divided = 0, total = 0;
    int shown = 0;
    std::vector<uint64_t> xv;
    const uint64_t xm = Mask(xs);
    for (uint64_t yi = first; yi < first + count && yi < ys.size(); ++yi)
    {
        const uint64_t y = ys[yi] & Mask(w);
        for (int kind = 0; kind < 3; ++kind)
            for (int rep = 0; rep < 4; ++rep)
            {
                const uint64_t h = Hash64(y, kind * 16 + rep);
                TermCfg c{ xs, w, kind, 0, 0, static_cast<int>(h & 1), 0 };
                const int bits = 8 * xs;
                // the shift of hi and of the sum: the edges and a stride
                static const int dEdge[] = { 0, 0, 1, 2, 3, 4, 7, 8, 15, 16, 24, 31 };
                static const int sEdge[] = { 0, 1, 2, 3, 4, 5, 7, 8, 9, 15, 16, 17, 20, 24, 31, 32 };
                c.d = (kind == 2) ? 0 : dEdge[(h >> 4) % 12];
                c.s = sEdge[(h >> 9) % 16];
                if (c.d >= bits) c.d = bits - 1;
                if (c.s > bits) c.s = bits;
                c.fix = (kind == 1) ? static_cast<int>((h >> 14) % 3) : 0;
                const char ch = TermOne(t, c, y, 0x7e57, xv, nullptr);
                ++total;
                if (ch == 'W' || ch == 'w')
                {
                    ++wrong;
                    std::string dump;
                    TermOne(t, c, y, 0x7e57, xv, &dump);
                    if (shown++ < 20)
                        std::printf("WRONG term kind %d xs %d y=%llx d=%d s=%d order=%d fix=%d (%c): %s\n", kind, xs, (unsigned long long)y, c.d, c.s, c.order, c.fix, ch, dump.c_str());
                }
                if (ch == 'C' || ch == 'W') ++divided;
                out.push_back(ch);
            }
    }
    (void)xm;
    FILE* fp = std::fopen(outFile, "wb");
    if (fp) { std::fwrite(out.data(), 1, out.size(), fp); std::fclose(fp); }
    std::printf("divexh14 term xs=%d: %ld configurations, %ld divided, %ld wrong\n", xs, total, divided, wrong);
}

// the constants of a width: every one of 2 bytes; for 4 and 8 bytes the reciprocals of the divisors 1..70000 (rounded up and down, and the neighbours) at every
// power of two, the powers of two and their neighbours, and random ones
std::vector<uint64_t> YList(int w, uint64_t seed, size_t randoms)
{
    std::vector<uint64_t> ys;
    const uint64_t m = Mask(w);
    if (w == 2) { for (uint64_t i = 0; i <= m; ++i) ys.push_back(i); return ys; }
    std::set<uint64_t> s;
    if (w == 4) for (uint64_t i = 0; i <= 0x20002; ++i) s.insert(i);      // every multiplier of 17 bits or fewer
    for (uint64_t d = 1; d <= 3000; ++d)
        for (int k = 8 * w / 2; k <= 8 * w; ++k)
        {
            const unsigned __int128 p = static_cast<unsigned __int128>(1) << k;
            const uint64_t lo = static_cast<uint64_t>(p / d), hi = static_cast<uint64_t>((p + d - 1) / d);
            for (int dv = -1; dv <= 2; ++dv) { s.insert((lo + dv) & m); s.insert((hi + dv) & m); }
        }
    for (uint64_t d = 3000; d <= 70000; d += 7)
        for (int k : { 8 * w / 2, 8 * w / 2 + 1, 8 * w - 1, 8 * w })
        {
            const unsigned __int128 p = static_cast<unsigned __int128>(1) << k;
            s.insert((static_cast<uint64_t>(p / d) + 1) & m);
            s.insert((static_cast<uint64_t>((p + d - 1) / d)) & m);
        }
    for (int k = 0; k < 8 * w; ++k) for (int dv = -3; dv <= 3; ++dv) s.insert(((static_cast<uint64_t>(1) << k) + dv) & m);
    Rng r(seed);
    for (size_t i = 0; i < randoms; ++i) { const int bits = 1 + static_cast<int>(r.next() % (8 * w)); s.insert(r.next() & Mask(bits > 8 * w ? w : bits) & m); }
    ys.assign(s.begin(), s.end());
    return ys;
}

}  // namespace

int main(int argc, char** argv)
{
    if (argc < 7) { std::printf("usage: %s <scratch dir> <opt|term> <xs> <first> <count> <out file>\n", argv[0]); return 2; }
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    const std::string scratch = argv[1], mode = argv[2];
    const int xs = std::atoi(argv[3]);
    const uint64_t first = std::strtoull(argv[4], nullptr, 10), count = std::strtoull(argv[5], nullptr, 10);
    const sfe::decomp::SpecBundleResult b = sfe::decomp::MaterialiseSh2SpecBundle(scratch);
    if (b.dir.empty()) { std::printf("no spec bundle\n"); return 2; }
    startDecompilerLibrary(std::vector<std::string>{ b.dir });
    RegionImage* image = new RegionImage();
    std::ostringstream errs;
    RfArch arch(image, &errs);
    DocumentStorage store;
    arch.init(store);
    Gr t;
    t.entry = Address(arch.getDefaultCodeSpace(), kBase);
    std::string fname;
    arch.nameFunction(t.entry, fname);
    t.fd = arch.symboltab->getGlobalScope()->addFunction(t.entry, fname)->getFunction();
    t.reg = arch.getSpaceByName("register");
    gReg = t.reg;
    const auto t0 = std::chrono::steady_clock::now();
    if (mode == "opt")
    {
        if (xs == 1) RunOpt(t, xs, first, count, argv[6]);
        else
        {
            // a list of constants for the wider widths: RunOpt takes y as the value, so map the index through the list here
            const std::vector<uint64_t> ys = YList(2 * xs, 0xC0FFEE, 200000);
            std::string out;
            long rewritten = 0, wrong = 0, total = 0;
            int shown = 0;
            std::vector<uint64_t> xv;
            const int w = 2 * xs;
            for (uint64_t yi = first; yi < first + count && yi < ys.size(); ++yi)
            {
                const uint64_t y = ys[yi];
                for (int sext = 0; sext < 3; ++sext)
                    for (int form = 0; form < 3; ++form)
                        for (int arith = 0; arith < 2; ++arith)
                        {
                            if (form == 2 && arith) continue;
                            const int amounts = form == 0 ? 8 * w : (form == 1 ? 8 * (w - 1) : 1);
                            for (int cut = (form == 0 ? 0 : 1); cut <= (form == 0 ? 0 : (w >= 4 ? 2 : 1)); ++cut)
                            {
                                if (form != 0 && cut >= w) continue;
                                for (int amount = 0; amount < amounts; ++amount)
                                {
                                    if (amount > 3 && amount < amounts - 3 && (amount % 3) != 0 && amount != 8 * xs && amount != 8 * xs + 1 && amount != 8 * xs - 1) continue;
                                    for (int reader = 0; reader < 3; ++reader)
                                    {
                                        OptCfg c{ xs, w, sext, form, arith != 0, cut, amount, reader };
                                        if (form == 2) c.amount = 0;
                                        if (c.reader == 1 && (form == 0 ? w : w - cut) < 2) { out.push_back('-'); continue; }
                                        const char ch = OptOne(t, c, y, 0x5eed, xv);
                                        ++total;
                                        if (ch == 'C' || ch == 'W') ++rewritten;
                                        if (ch == 'W')
                                        {
                                            ++wrong;
                                            if (shown++ < 20)
                                                std::printf("WRONG RuleDivOpt: %d bytes %s of %d by %llx, form %d, %s, cut %d, amount %d, reader %d\n", w, sext == 1 ? "sext" : (sext == 2 ? "and" : "zext"), xs, (unsigned long long)y, form, arith ? "s>>" : ">>", cut, amount, reader);
                                        }
                                        out.push_back(ch);
                                    }
                                }
                            }
                        }
            }
            FILE* fp = std::fopen(argv[6], "wb");
            if (fp) { std::fwrite(out.data(), 1, out.size(), fp); std::fclose(fp); }
            std::printf("divexh14 opt xs=%d constants %llu..%llu of %zu: %ld configurations, %ld rewritten, %ld wrong\n", xs, (unsigned long long)first, (unsigned long long)(first + count - 1), ys.size(), total, rewritten, wrong);
        }
    }
    else if (mode == "term")
    {
        gPool = MakePool("");
        const std::vector<uint64_t> ys = YList(2 * xs, 0xBEEF, 100000);
        RunTerm(t, xs, ys, first, count, argv[6]);
    }
    std::printf("%.0f s\n", std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count());
    return 0;
}
