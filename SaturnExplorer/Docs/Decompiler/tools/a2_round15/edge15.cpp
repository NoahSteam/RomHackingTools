// edge15 (shares divexh14's scaffold): the division recognisers of patch 0064 (RuleDivOpt, RuleDivTermAdd, RuleDivTermAdd2, RuleDivChain) judged by value on every constant a small
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


// ---- RuleThreeWayCompare (A2-86) ---------------------------------------------------------------------------------------------------------------
// `zext(a < b) + zext(a <= b) - 1` in the three arrangements of the sum, the two compares of constants that are equal, one apart (in either order), or far apart,
// LESS and LESSEQUAL, unsigned and signed, then a compare of the sum with -1, 0, 1 or 2 on either side. The rule is applied to the outer compare; the value
// of that compare must not change, for every x (all of a byte, the edges and random values of the longer widths) and the second variable y.
std::vector<uint64_t> Edges(int size, Rng& r)
{
    const uint64_t m = Mask(size), hib = (m >> 1) + 1;
    std::vector<uint64_t> e = { 0, 1, 2, 3, m, m - 1, m - 2, hib, hib - 1, hib + 1, hib - 2, hib + 2, m >> 2, (m >> 2) + 1 };
    for (uint64_t& v : e) v &= m;
    for (int i = 0; i < 6; ++i) e.push_back(r.next() & m);
    return e;
}

void RunThreeWay(Gr& t, uint64_t seedBase, uint64_t count, const char* outFile)
{
    long graphs = 0, rewritten = 0, wrong = 0, byForm[5] = { 0, 0, 0, 0, 0 }, graphsForm[5] = { 0, 0, 0, 0, 0 };
    int shown = 0;
    std::string out;
    for (uint64_t seed = seedBase; seed < seedBase + count; ++seed)
    {
        Rng r(seed);
        const int size = std::vector<int>{ 1, 1, 2, 4, 8 }[r.below(5)];
        const bool sg = r.chance(50);
        const OpCode lt = sg ? CPUI_INT_SLESS : CPUI_INT_LESS, le = sg ? CPUI_INT_SLESSEQUAL : CPUI_INT_LESSEQUAL;
        const std::vector<uint64_t> E = Edges(size, r);
        // the two compares: (A1 op1 B1) and (A2 op2 B2); either side a constant or the variable x / y
        t.reset();
        Varnode* x = t.input(size);
        const int mode = r.below(4);     // 0 same variables, 1 constant on the left of both, 2 constant on the right of both, 3 both constants unrelated
        uint64_t c1 = E[r.below(static_cast<int>(E.size()))], c2 = c1;
        const int rel = r.below(5);      // 0 equal, 1 c2 = c1 + 1, 2 c1 = c2 + 1, 3 random, 4 the signed extremes
        if (rel == 1) c2 = (c1 + 1) & Mask(size);
        else if (rel == 2) c2 = (c1 - 1) & Mask(size);
        else if (rel == 3) c2 = E[r.below(static_cast<int>(E.size()))];
        else { c1 = r.chance(50) ? (Mask(size) >> 1) : ((Mask(size) >> 1) + 1); c2 = r.chance(50) ? (Mask(size) >> 1) : ((Mask(size) >> 1) + 1); }
        Varnode *a1, *b1, *a2, *b2;
        if (mode == 0) { a1 = a2 = x; b1 = b2 = t.k(size, c1); }
        else if (mode == 1) { a1 = t.k(size, c1); a2 = t.k(size, c2); b1 = b2 = x; }
        else if (mode == 2) { a1 = a2 = x; b1 = t.k(size, c1); b2 = t.k(size, c2); }
        else { a1 = t.k(size, c1); b1 = x; a2 = x; b2 = t.k(size, c2); }
        const OpCode op1 = lt, op2 = r.chance(70) ? le : lt;
        const int S = r.chance(50) ? 1 : 4;
        Varnode* z1 = t.op(CPUI_INT_ZEXT, S, t.op(op1, 1, a1, b1));
        Varnode* z2 = t.op(CPUI_INT_ZEXT, S, t.op(op2, 1, a2, b2));
        if (r.chance(50)) std::swap(z1, z2);
        const int form = r.below(5);   // 3 and 4: the partial form `zext(a < b) + zext(a <= b)` with no -1 (the rule completes it by subtracting 1 from the constant), round 15
        Varnode* sum;
        const uint64_t minus1 = Mask(S);
        if (form == 0) sum = t.op(CPUI_INT_ADD, S, t.op(CPUI_INT_ADD, S, z1, z2), t.k(S, minus1));
        else if (form == 1) sum = t.op(CPUI_INT_ADD, S, t.op(CPUI_INT_ADD, S, z1, t.k(S, minus1)), z2);
        else if (form == 2) sum = t.op(CPUI_INT_ADD, S, z1, t.op(CPUI_INT_ADD, S, z2, t.k(S, minus1)));
        else sum = t.op(CPUI_INT_ADD, S, z1, z2);
        static const uint64_t consts[] = { 0xFFFFFFFFFFFFFFFFull, 0, 1, 2 };
        const uint64_t kc = consts[r.below(4)] & Mask(S);
        static const OpCode cops[] = { CPUI_INT_SLESS, CPUI_INT_SLESSEQUAL, CPUI_INT_EQUAL, CPUI_INT_NOTEQUAL };
        const OpCode cop = cops[r.below(4)];
        const bool constLeft = r.chance(50);
        Varnode* root = constLeft ? t.op(cop, 1, t.k(S, kc), sum) : t.op(cop, 1, sum, t.k(S, kc));
        t.consumeAll();
        t.fd->calcNZMask();
        // before: the value for each x
        std::vector<uint64_t> xs;
        if (size == 1) for (uint64_t i = 0; i < 256; ++i) xs.push_back(i);
        else { xs = Edges(size, r); for (uint64_t c : { c1, c2 }) for (int d = -2; d <= 2; ++d) xs.push_back((c + d) & Mask(size)); }
        gXSize = size; gXOff = x->getOffset();
        std::vector<uint64_t> before;
        for (uint64_t xv : xs) { gX = xv; gUndefined = false; gUnsup = false; before.push_back(Val(root)); if (gUnsup) { before.clear(); break; } }
        if (before.empty()) continue;
        RuleThreeWayCompare rule("scratch");
        int rc;
        try { rc = rule.applyOp(root->getDef(), *t.fd); } catch (LowlevelError&) { continue; }
        ++graphs; ++graphsForm[form];
        if (rc) { ++rewritten; ++byForm[form]; }
        bool bad = false;
        for (size_t i = 0; i < xs.size(); ++i)
        {
            gX = xs[i]; gUndefined = false; gUnsup = false;
            // the root op may have become a compare of x with the constants: its output is the same varnode
            const uint64_t got = Val(root);
            if (got != before[i]) { bad = true; break; }
        }
        out.push_back(bad ? 'W' : (rc ? 'C' : '0'));
        if (bad)
        {
            ++wrong;
            if (shown++ < (std::getenv("DX_SHOW") ? std::atoi(std::getenv("DX_SHOW")) : 20))
                std::printf("WRONG RuleThreeWayCompare seed %llu: size %d %s mode %d consts %llx %llx rel %d ops %s/%s S %d form %d outer %s%s %llx\n", (unsigned long long)seed, size, sg ? "signed" : "unsigned", mode,
                            (unsigned long long)c1, (unsigned long long)c2, rel, get_opname(op1), get_opname(op2), S, form, constLeft ? "const " : "", get_opname(cop), (unsigned long long)kc);
        }
    }
    FILE* fp = std::fopen(outFile, "wb");
    if (fp) { std::fwrite(out.data(), 1, out.size(), fp); std::fclose(fp); }
    std::printf("edge15 threeway: %ld graphs, %ld rewritten, %ld wrong\n", graphs, rewritten, wrong);
    for (int f = 0; f < 5; ++f) std::printf("  form %d: %ld graphs, %ld rewritten\n", f, graphsForm[f], byForm[f]);
}


// ---- RuleDivChain (A2-84, A2-90) ---------------------------------------------------------------------------------------------------------------
// `(x / c1) / c2` and `(x s/ c1) s/ c2` and `(x >> n) / c` at 1 byte for every pair of constants (the whole 256 x 256 grid) and every x, at 2 and 4 bytes for the
// pairs of edge values and random ones. The value of the outer division must not change when the rule is applied.
void RunDivChain(Gr& t, int size, uint64_t first, uint64_t count, const char* outFile)
{
    long graphs = 0, rewritten = 0, wrong = 0;
    int shown = 0;
    std::string out;
    const uint64_t m = Mask(size);
    std::vector<uint64_t> cs;
    if (size == 1) for (uint64_t i = 0; i < 256; ++i) cs.push_back(i);
    else
    {
        Rng r(77 + size);
        const uint64_t hib = (m >> 1) + 1;
        for (uint64_t e : { (uint64_t)0, (uint64_t)1, (uint64_t)2, (uint64_t)3, (uint64_t)5, (uint64_t)7, (uint64_t)10, (uint64_t)255, (uint64_t)256, (uint64_t)257, (uint64_t)65535, (uint64_t)65536, (uint64_t)65537, m, m - 1, m - 2, hib, hib - 1, hib + 1, hib + 2, m >> 2, (m >> 2) + 1, (m >> 3) + 1 }) cs.push_back(e & m);
        for (int i = 0; i < 40; ++i) cs.push_back(r.next() & m);
        for (int k = 1; k < 8 * size; ++k) cs.push_back(((uint64_t)1 << k) & m), cs.push_back((((uint64_t)1 << k) + 1) & m), cs.push_back((((uint64_t)1 << k) - 1) & m);
    }
    const uint64_t n1 = cs.size();
    for (uint64_t idx = first; idx < first + count && idx < n1 * n1 * 3; ++idx)
    {
        const int kind = static_cast<int>(idx % 3);        // 0 unsigned/unsigned, 1 signed/signed, 2 (x >> n) / c
        const uint64_t pair = idx / 3;
        const uint64_t c1 = cs[pair / n1], c2 = cs[pair % n1];
        if (c2 == 0 || (kind != 2 && c1 == 0)) { out.push_back('-'); continue; }
        t.reset();
        Varnode* x = t.input(size);
        Varnode* d1;
        int sa = 0;
        if (kind == 0) d1 = t.op(CPUI_INT_DIV, size, x, t.k(size, c1));
        else if (kind == 1) d1 = t.op(CPUI_INT_SDIV, size, x, t.k(size, c1));
        else { sa = static_cast<int>(c1 % (8 * size + 3)); d1 = t.op(CPUI_INT_RIGHT, size, x, t.k(4, sa)); }
        Varnode* d2 = t.op(kind == 1 ? CPUI_INT_SDIV : CPUI_INT_DIV, size, d1, t.k(size, c2));
        Varnode* rd = t.op(CPUI_COPY, size, d2);       // d1 has this one reader only (the rule asks that)
        (void)rd;
        t.consumeAll();
        t.fd->calcNZMask();
        std::vector<uint64_t> xs;
        if (size == 1) for (uint64_t i = 0; i < 256; ++i) xs.push_back(i);
        else { Rng r(idx); xs = Edges(size, r); for (uint64_t c : { c1, c2, c1 * c2 }) for (int d = -2; d <= 2; ++d) xs.push_back((c + d) & m); for (int i = 0; i < 100; ++i) xs.push_back(r.next() & m); }
        gXSize = size; gXOff = x->getOffset();
        std::vector<uint64_t> before; std::vector<char> def;
        for (uint64_t xv : xs) { gX = xv; gUndefined = false; gUnsup = false; before.push_back(Val(d2)); def.push_back(gUndefined ? 0 : 1); }
        RuleDivChain rule("scratch");
        int rc;
        try { rc = rule.applyOp(d2->getDef(), *t.fd); } catch (LowlevelError&) { out.push_back('X'); continue; }
        ++graphs;
        if (rc) ++rewritten;
        bool bad = false;
        for (size_t i = 0; i < xs.size() && !bad; ++i)
        {
            gX = xs[i]; gUndefined = false; gUnsup = false;
            const uint64_t got = Val(d2);
            if (def[i] && !gUndefined && got != before[i]) bad = true;
        }
        out.push_back(bad ? 'W' : (rc ? 'C' : '0'));
        if (bad)
        {
            ++wrong;
            if (shown++ < (std::getenv("DX_SHOW") ? std::atoi(std::getenv("DX_SHOW")) : 20))
                std::printf("WRONG RuleDivChain: size %d kind %d c1 %llx (sa %d) c2 %llx\n", size, kind, (unsigned long long)c1, sa, (unsigned long long)c2);
        }
    }
    FILE* fp = std::fopen(outFile, "wb");
    if (fp) { std::fwrite(out.data(), 1, out.size(), fp); std::fclose(fp); }
    std::printf("edge15 divchain size %d: %ld graphs, %ld rewritten, %ld wrong (grid %llu x %llu x 3)\n", size, graphs, rewritten, wrong, (unsigned long long)n1, (unsigned long long)n1);
}

// ---- RuleSignForm2 (A2-83) -----------------------------------------------------------------------------------------------------------------------
// `sub(sext(V) * C, c) s>> (8*size - 1)`: V of 1 byte with every multiplier of the 2-byte multiply, V of 2 bytes with the edge multipliers of 4 and 8 bytes.
void RunSignForm2(Gr& t, int vs, uint64_t first, uint64_t count, const char* outFile)
{
    long graphs = 0, rewritten = 0, wrong = 0;
    int shown = 0;
    std::string out;
    for (uint64_t idx = first; idx < first + count; ++idx)
    {
        const int ms = vs == 1 ? 2 : (idx & 1 ? 4 : 8);
        uint64_t C;
        Rng r(idx);
        if (vs == 1) C = idx & 0xFFFF;
        else { std::vector<uint64_t> e = Edges(ms, r); C = e[(idx >> 1) % e.size()]; }
        t.reset();
        Varnode* x = t.input(vs);
        Varnode* ext = t.op(CPUI_INT_SEXT, ms, x);
        Varnode* prod = t.op(CPUI_INT_MULT, ms, ext, t.k(ms, C));
        Varnode* hi = t.op(CPUI_SUBPIECE, vs, prod, t.k(4, ms - vs));
        Varnode* sgn = t.op(CPUI_INT_SRIGHT, vs, hi, t.k(4, 8 * vs - 1));
        t.consumeAll();
        t.fd->calcNZMask();
        std::vector<uint64_t> xs;
        if (vs == 1) for (uint64_t i = 0; i < 256; ++i) xs.push_back(i); else xs = Edges(vs, r);
        gXSize = vs; gXOff = x->getOffset();
        std::vector<uint64_t> before;
        for (uint64_t xv : xs) { gX = xv; gUndefined = false; gUnsup = false; before.push_back(Val(sgn)); }
        RuleSignForm2 rule("scratch");
        const int rc = rule.applyOp(sgn->getDef(), *t.fd);
        ++graphs;
        if (rc) ++rewritten;
        bool bad = false;
        for (size_t i = 0; i < xs.size() && !bad; ++i) { gX = xs[i]; gUndefined = false; gUnsup = false; if (Val(sgn) != before[i]) bad = true; }
        out.push_back(bad ? 'W' : (rc ? 'C' : '0'));
        if (bad) { ++wrong; if (shown++ < 20) std::printf("WRONG RuleSignForm2: V %d bytes, multiply %d bytes, C %llx\n", vs, ms, (unsigned long long)C); }
    }
    FILE* fp = std::fopen(outFile, "wb");
    if (fp) { std::fwrite(out.data(), 1, out.size(), fp); std::fclose(fp); }
    std::printf("edge15 signform2 V %d: %ld graphs, %ld rewritten, %ld wrong\n", vs, graphs, rewritten, wrong);
}

}  // namespace

int main(int argc, char** argv)
{
    if (argc < 6) { std::printf("usage: %s <scratch dir> <threeway|divchain1|divchain2|divchain4|divchain8|signform1|signform2> <first seed> <count> <out file>\n", argv[0]); return 2; }
    std::setvbuf(stdout, nullptr, _IOLBF, 0);
    const std::string scratch = argv[1], mode = argv[2];
    const uint64_t first = std::strtoull(argv[3], nullptr, 10), count = std::strtoull(argv[4], nullptr, 10);
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
    if (mode == "threeway") RunThreeWay(t, first, count, argv[5]);
    else if (mode == "divchain1") RunDivChain(t, 1, first, count, argv[5]);
    else if (mode == "divchain2") RunDivChain(t, 2, first, count, argv[5]);
    else if (mode == "divchain4") RunDivChain(t, 4, first, count, argv[5]);
    else if (mode == "divchain8") RunDivChain(t, 8, first, count, argv[5]);
    else if (mode == "signform1") RunSignForm2(t, 1, first, count, argv[5]);
    else if (mode == "signform2") RunSignForm2(t, 2, first, count, argv[5]);
    return 0;
}
