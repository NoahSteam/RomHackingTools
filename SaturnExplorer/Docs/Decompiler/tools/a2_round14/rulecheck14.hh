// Area 2 round 13 review: an in-situ, by-value check of every rule application the real engine makes.
//
// Included by action.cc in a SCRATCH build only (-DSE_RULECHECK, see rulecheck14.patch and relink13.sh); the vendored tree does not change.
// ActionPool::processOp calls rc14::snapshot(op) before a rule's applyOp and rc14::verify(rule, op, snapshot, result) after it.
// The check: the value of the expression cones of the op's output and of the outputs of its readers (two levels) is a function of the "leaves" under them
// (a varnode that is an input, a constant or the result of an op this file does not evaluate: LOAD, CALL, INDIRECT, MULTIEQUAL, floating point, pointer ops).
// The functions before and after the rule are evaluated for a few dozen assignments of the leaves (edge values and random ones, each inside the leaf's
// non-zero mask) and compared on the bits the root's consumers read (its consume mask). A leaf that exists only after the rule (the rule built a new
// SUBPIECE of an input, say) cannot be compared and is counted as `unmatched`.
//
// SE_RULECHECK=1       print a line for each application whose value changed, and the totals at exit
// SE_RULECHECK_ONLY=n  check only the rule whose name contains n
// SE_RULECHECK_MAX=n   print up to n bad applications of a rule (3)
//
// Exemptions (what the check does not compare), as of the area 2 round 13 fix. Gone: the samples whose multiply wraps (RuleDivOpt), and RuleDivTermAdd and
// RuleDivTermAdd2 as a whole; the engine now refuses a product that wraps (A2-82) unless RuleDivTermAdd or RuleDivTermAdd2 built it as the exact product
// of a division term and flagged it (PcodeOp::isDivProductWide), and this check values a flagged INT_MULT as that exact product (128 bits) for the shifts
// that read it. Kept, each for a reason:
//  - RuleSignMod2nOpt2: the adjusted value is read from a MULTIEQUAL and the branch behind it; the leaf is related to V by control flow, not by data
//  - a root with consume 0 (dead: rules fold it to anything), a root the rule left without a reader, an interior ADD of a RuleCollectTerms tree (the rule moves
//    a term between the ADDs of the tree; the root is compared), the zext RuleBoolZext negates in place (the final ADD that reads it is compared)
//  - a sample in which an op takes a value outside its own non-zero mask (counted as maskskip, the first three printed; 0 in every run)
//  - cones the check cannot value: more than 24 leaves or 400 nodes, an output of more than 8 bytes, ops it does not evaluate (LOAD, CALL, INDIRECT,
//    MULTIEQUAL, floating point, pointer ops), and a leaf that exists only after the rule (counted as unmatched)
#ifndef SE_RULECHECK_14_HH
#define SE_RULECHECK_14_HH

#include "funcdata.hh"
#include "op.hh"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace ghidra {
namespace rc14 {

static const int kMaxNodes = 600;
static const int kMaxRoots = 40;
static const int kMaxLeaves = 24;
static const int kTrials = 40;

struct Node
{
    int kind;            // 0 leaf, 1 constant, 2 op
    OpCode opc;
    int size;
    int nin;
    int in[3];
    int insize[3];
    uintb value;         // constant value, or the leaf's non-zero mask
    const Varnode* vn;   // leaf identity (pointer compared only; a freed varnode's address can be reused: the size and address are compared too)
    uintb addr;
    int spc;
    uintb nz;            // the varnode's non-zero mask as the engine holds it when the snapshot is taken (a sample whose value has a bit outside it is no input the engine can see)
    bool wide;           // an INT_MULT flagged PcodeOp::isDivProductWide: the exact product of a division term, wider than the multiply
};
struct Cone
{
    Node nodes[kMaxNodes];
    int n = 0;
    bool overflow = false;
    int add(const Node& x) { if (n >= kMaxNodes) { overflow = true; return 0; } nodes[n] = x; return n++; }
};
struct Root
{
    const Varnode* vn;
    const PcodeOp* def;
    uintb consume;
    int node;
    bool dead;               // round 14: consume 0 when the snapshot was taken (a varnode a rule made in this pass, or one dead code will remove): compared on its non-zero mask, a change is counted apart (deadchg), not a bad application
    const PcodeOp* reader;   // round 14: a slot root: the value an op without an output reads (STORE, RETURN, CALL, CBRANCH ...), compared through the reader's input slot
    int slot;
    uint64_t fp;             // rootFp when the snapshot was taken
    bool flip;               // a slot root of a CBRANCH: its boolean_flip flag when the snapshot was taken (a rule may flip the condition and negate the input in one step)
    int kind;                // 0 the op's output, 1 a reader's output, 2 a reader's reader, 3 (unused), 4 another reader of an input (a sibling: a rule that changes the definition of an input in place changes what it reads), 5 slot root
};
struct Snap
{
    Cone cone;
    Root roots[kMaxRoots];
    int nroots = 0;
    int dropped = 0;         // roots whose cone did not fit (the others are still compared)
    bool ok = false;
};

inline bool evaluable(OpCode c)
{
    switch (c)
    {
    case CPUI_COPY: case CPUI_INT_ZEXT: case CPUI_INT_SEXT: case CPUI_SUBPIECE: case CPUI_PIECE:
    case CPUI_INT_ADD: case CPUI_INT_SUB: case CPUI_INT_CARRY: case CPUI_INT_SCARRY: case CPUI_INT_SBORROW: case CPUI_INT_2COMP: case CPUI_INT_NEGATE:
    case CPUI_INT_XOR: case CPUI_INT_AND: case CPUI_INT_OR: case CPUI_INT_LEFT: case CPUI_INT_RIGHT: case CPUI_INT_SRIGHT: case CPUI_INT_MULT:
    case CPUI_INT_DIV: case CPUI_INT_SDIV: case CPUI_INT_REM: case CPUI_INT_SREM:
    case CPUI_INT_EQUAL: case CPUI_INT_NOTEQUAL: case CPUI_INT_SLESS: case CPUI_INT_SLESSEQUAL: case CPUI_INT_LESS: case CPUI_INT_LESSEQUAL:
    case CPUI_BOOL_NEGATE: case CPUI_BOOL_XOR: case CPUI_BOOL_AND: case CPUI_BOOL_OR: case CPUI_POPCOUNT: case CPUI_LZCOUNT:
        return true;
    default: return false;
    }
}

struct Memo
{
    // (round 14: a hash index over the entries; the linear scan this replaces made every snapshot quadratic in the cone, and the check cost 40 seconds on one
    // 32-step division)
    static const int kSlots = 2048;
    const Varnode* key[kMaxNodes];
    int val[kMaxNodes];
    short slot[kSlots];
    int n = 0;
    Memo() { std::memset(slot, 0xFF, sizeof(slot)); }
    static unsigned hashOf(const Varnode* v) { return (unsigned)(((uintptr_t)v >> 4) * 2654435761u) & (kSlots - 1); }
    int find(const Varnode* v) const
    {
        for (unsigned h = hashOf(v); slot[h] >= 0; h = (h + 1) & (kSlots - 1))
            if (key[slot[h]] == v) return val[slot[h]];
        return -1;
    }
    void put(const Varnode* v, int id)
    {
        if (n >= kMaxNodes) return;
        unsigned h = hashOf(v);
        while (slot[h] >= 0) h = (h + 1) & (kSlots - 1);
        key[n] = v; val[n] = id; slot[h] = (short)n; ++n;
    }
    // drop the entries after the first m (the last ones put)
    void truncate(int m)
    {
        for (int i = n - 1; i >= m; --i)
        {
            unsigned h = hashOf(key[i]);
            while (slot[h] != i) h = (h + 1) & (kSlots - 1);
            slot[h] = -1;
        }
        n = m;
    }
};

inline int build(const Varnode* vn, Cone& c, Memo& memo, int depth, bool boolUse = false)
{
    { const int hit = memo.find(vn); if (hit >= 0) return hit; }
    Node nd;
    nd.wide = false; nd.nin = 0; nd.opc = CPUI_COPY; nd.size = vn->getSize(); nd.vn = vn; nd.value = 0; nd.nz = vn->getNZMask() & calc_mask(vn->getSize()); nd.addr = vn->getOffset(); nd.spc = vn->getSpace() ? vn->getSpace()->getIndex() : -1;
    int id;
    if (vn->isConstant())
    {
        nd.kind = 1; nd.value = vn->getOffset() & calc_mask(vn->getSize());
        id = c.add(nd);
    }
    else if (vn->isWritten() && depth > 0 && evaluable(vn->getDef()->code()))
    {
        const PcodeOp* d = vn->getDef();
        nd.kind = 2; nd.opc = d->code(); nd.nin = d->numInput();
        nd.wide = d->code() == CPUI_INT_MULT && d->isDivProductWide();
        if (nd.nin > 3) { nd.kind = 0; nd.value = vn->getNZMask() & calc_mask(vn->getSize()); id = c.add(nd); }
        else
        {
            const bool boolOp = nd.opc == CPUI_BOOL_NEGATE || nd.opc == CPUI_BOOL_AND || nd.opc == CPUI_BOOL_OR || nd.opc == CPUI_BOOL_XOR;
            for (int i = 0; i < nd.nin; ++i)
            {
                nd.in[i] = build(d->getIn(i), c, memo, depth - 1, boolOp);
                nd.insize[i] = d->getIn(i)->getSize();
            }
            id = c.add(nd);
        }
    }
    else
    {
        nd.kind = 0; nd.value = vn->getNZMask() & calc_mask(vn->getSize());
        if (boolUse || (vn->isWritten() && vn->getDef()->isBoolOutput())) nd.value &= 1;     // a boolean is 0 or 1
        id = c.add(nd);
    }
    memo.put(vn, id);
    return id;
}

inline uint64_t rootFp(const Root& r);

inline void snapshot(const PcodeOp* op, Snap& s)
{
    s.ok = false;
    s.nroots = 0;
    s.dropped = 0;
    s.cone.n = 0;
    s.cone.overflow = false;
    if (!evaluable(op->code())) return;      // a rule on a phi, a call, a load ... is outside what this check can value
    const Varnode* out = op->getOut();
    if (out == (const Varnode*)0 || out->getSize() > 8) return;
    Memo memo;
    // round 14. The roots are the values the rule may not change: the op's output, the outputs of its readers and of theirs (as before), and now also the
    // inputs of the op (a rule may rewrite the definition of an input in place, which changes the value every other reader of it sees), the outputs of the
    // other readers of those inputs, and the input slot of every reader that has no output (a STORE, RETURN, CALL or CBRANCH reads a value through a slot,
    // and an op the rule destroys takes its output's root with it). A root whose cone does not fit is dropped alone (counted), the others stay.
    auto addNode = [&](const Varnode* v, int kind, const PcodeOp* reader, int slot) {
        if (s.nroots >= kMaxRoots || v->getSize() > 8) return;
        // a long chain (the 32 steps of an inline division) makes every cone large: past a few roots of a big cone the rest are left out, or the check is quadratic in the chain
        if (s.nroots >= 8 && s.cone.n > 120) return;
        if (!v->isWritten() && kind != 5) return;
        if (kind == 5 && v->isConstant()) return;
        if (kind != 5) { for (int i = 0; i < s.nroots; ++i) if (s.roots[i].vn == v && s.roots[i].kind != 5) return; }
        else { for (int i = 0; i < s.nroots; ++i) if (s.roots[i].reader == reader && s.roots[i].slot == slot) return; }
        const int saveN = s.cone.n, saveM = memo.n;
        Root& r = s.roots[s.nroots];
        r.vn = v; r.def = v->isWritten() ? v->getDef() : (const PcodeOp*)0; r.kind = kind; r.reader = reader; r.slot = slot;
        r.flip = (kind == 5 && reader->code() == CPUI_CBRANCH) ? reader->isBooleanFlip() : false;
        const uintb full = calc_mask(v->getSize());
        r.consume = v->getConsume() & full;
        // a varnode no consumer reads a bit of (consume 0) is dead as the engine stands: rules fold it to anything (RuleAndMask to 0). A varnode made by an earlier
        // rule of this pass has consume 0 as well (dead code analysis has not run since): such a root is compared on its non-zero mask and a change counted as deadchg
        r.dead = (r.consume == 0);
        static const bool noDead = std::getenv("SE_RC14_NODEAD") != nullptr;
        if (r.dead && noDead) return;
        if (r.dead) r.consume = v->getNZMask() & full;
        if (r.consume == 0) r.consume = full;
        r.node = build(v, s.cone, memo, 100000);
        if (s.cone.overflow)
        {
            s.cone.n = saveN; memo.truncate(saveM); s.cone.overflow = false; ++s.dropped;
            return;
        }
        r.fp = rootFp(r);
        ++s.nroots;
    };
    addNode(out, 0, (const PcodeOp*)0, 0);
    // the readers, and their readers: a rule may rewrite the reader (RuleDivTermAdd changes the ADD that reads the op's result)
    const PcodeOp* rd1[kMaxRoots];
    int n1 = 0;
    for (auto it = out->beginDescend(); it != out->endDescend() && n1 < 16; ++it) rd1[n1++] = *it;
    for (int i = 0; i < n1; ++i)
    {
        if (rd1[i]->getOut() != (const Varnode*)0) addNode(rd1[i]->getOut(), 1, (const PcodeOp*)0, 0);
        else for (int sl = 0; sl < rd1[i]->numInput(); ++sl) if (rd1[i]->getIn(sl) == out) addNode(out, 5, rd1[i], sl);
    }
    for (int i = 0; i < n1; ++i)
    {
        const Varnode* o1 = rd1[i]->getOut();
        if (o1 == (const Varnode*)0) continue;
        int n2 = 0;
        for (auto it = o1->beginDescend(); it != o1->endDescend() && n2 < 8; ++it, ++n2)
        {
            const PcodeOp* r2 = *it;
            if (r2->getOut() != (const Varnode*)0) addNode(r2->getOut(), 2, (const PcodeOp*)0, 0);
            else for (int sl = 0; sl < r2->numInput(); ++sl) if (r2->getIn(sl) == o1) addNode(o1, 5, r2, sl);
        }
    }
    // the inputs, and the other readers of the inputs
    static const bool noSib = std::getenv("SE_RC14_NOSIB") != nullptr;
    for (int k = 0; k < op->numInput() && !noSib; ++k)
    {
        const Varnode* in = op->getIn(k);
        if (in->isConstant()) continue;
        // (not the input itself: a rule may fold a negation into the compare that defines its input, which changes that value and keeps the op's; if the input has
        // another reader it sees the change, and that reader is a root below)
        int n3 = 0;
        for (auto it = in->beginDescend(); it != in->endDescend() && n3 < 8; ++it, ++n3)
        {
            const PcodeOp* sib = *it;
            if (sib == op) continue;
            if (sib->getOut() != (const Varnode*)0) addNode(sib->getOut(), 4, (const PcodeOp*)0, 0);
            else for (int sl = 0; sl < sib->numInput(); ++sl) if (sib->getIn(sl) == in) addNode(in, 5, sib, sl);
        }
    }
    s.ok = s.nroots > 0;
}

// round 14: a cheap fingerprint of what a root stands on (its defining op, or the varnode a reader reads through its slot): a rule that returns 0 and leaves every
// fingerprint alone has changed nothing any root can see, and the cones are not valued again (the check ran on every failed attempt of every rule, which is
// quadratic on a long chain)
inline uint64_t rootFp(const Root& r)
{
    uint64_t h = 1469598103934665603ull;
    auto add = [&](uint64_t x) { h = (h ^ x) * 1099511628211ull; };
    if (r.kind == 5)
    {
        if (r.reader->isDead() || r.slot >= r.reader->numInput()) { add(1); return h; }
        const Varnode* v = r.reader->getIn(r.slot);
        add(v->isConstant() ? v->getOffset() * 31 + v->getSize() : (uint64_t)(uintptr_t)v);
        return h;
    }
    if (r.def == (const PcodeOp*)0 || r.def->isDead() || r.def->getOut() != r.vn) { add(2); return h; }
    add((uint64_t)r.def->code() + (r.def->isDivProductWide() ? 1000 : 0));
    add((uint64_t)(uintptr_t)r.vn);
    for (int k = 0; k < r.def->numInput(); ++k)
    {
        const Varnode* v = r.def->getIn(k);
        add(v->isConstant() ? v->getOffset() * 31 + v->getSize() : (uint64_t)(uintptr_t)v);
    }
    return h;
}

struct Rng
{
    uint64_t s;
    explicit Rng(uint64_t x) : s(x) {}
    uint64_t next() { uint64_t z = (s += 0x9E3779B97F4A7C15ull); z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull; z = (z ^ (z >> 27)) * 0x94D049BB133111EBull; return z ^ (z >> 31); }
};

struct Eval
{
    const Cone* c;
    std::vector<uintb> val;
    std::vector<char> have;
    bool undef = false;
    bool hasWide = false;     // round 14: the cone holds a flagged exact product (its non-zero mask is the wrapped multiply's, the value is the exact one: a mask violation there is the model's, counted apart as maskwide)
    bool maskViol = false;    // an op node took a value with a bit outside its varnode's non-zero mask
    const Node* viol = nullptr;
    bool wrapped = false;     // an INT_MULT overflowed its width (the unsigned reading when a multiplicand is a ZEXT, the signed one for a SEXT, either otherwise); informational
    std::vector<unsigned __int128> exact;   // the exact product of a flagged (wide) INT_MULT, as a 128-bit two's complement number
    std::vector<char> haveExact;
    std::vector<char> exactSigned;           // the exact product was made from a sign extension: its right shifts are arithmetic
    // the exact value (128 bits) of node `id` if it derives from a flagged product through SUBPIECE, right shifts, ZEXT and COPY only
    bool exactOf(int id, unsigned __int128& v, bool& sgn)
    {
        if (haveExact.size() <= (size_t)id || !haveExact[id]) return false;
        v = exact[id]; sgn = exactSigned[id] != 0;
        return true;
    }
    void setExact(int id, unsigned __int128 v, bool sgn)
    {
        exact.resize(c->n, 0); haveExact.resize(c->n, 0); exactSigned.resize(c->n, 0);
        exact[id] = v; haveExact[id] = 1; exactSigned[id] = sgn ? 1 : 0;
    }
    const std::map<const Varnode*, uintb>* leafVals;
    uintb get(int id)
    {
        if (have[id]) return val[id];
        const Node& n = c->nodes[id];
        uintb r = 0;
        if (n.kind == 1) r = n.value;
        else if (n.kind == 0)
        {
            auto it = leafVals->find(n.vn);
            r = it == leafVals->end() ? 0 : it->second;
        }
        else
        {
            uintb a = n.nin > 0 ? get(n.in[0]) : 0, b = n.nin > 1 ? get(n.in[1]) : 0;
            const uintb m = calc_mask(n.size);
            try
            {
                switch (n.opc)
                {
                case CPUI_INT_SEXT: r = sign_extend(a, n.insize[0], n.size); break;
                case CPUI_SUBPIECE:
                {
                    unsigned __int128 ev; bool sg;
                    if (n.nin > 1 && b < 16 && exactOf(n.in[0], ev, sg))
                    {
                        // the cut RuleDivTermAdd leaves on the way: the exact number shifted down, not yet cut to the varnode
                        const unsigned __int128 sh = sg ? (unsigned __int128)(((__int128)ev) >> (8 * (int)b)) : ev >> (8 * (int)b);
                        setExact(id, sh, sg);
                        r = (uintb)sh & m;
                        break;
                    }
                    r = b >= 8 ? 0 : (a >> (8 * b)) & m;
                    break;
                }
                case CPUI_PIECE: r = ((n.insize[1] >= 8 ? 0 : (a << (8 * n.insize[1]))) | b) & m; break;
                case CPUI_INT_SUB: r = (a - b) & m; break;
                case CPUI_INT_2COMP: r = (0 - a) & m; break;
                case CPUI_INT_NEGATE: r = (~a) & m; break;
                case CPUI_BOOL_NEGATE: r = (a ^ 1) & 1; break;
                case CPUI_POPCOUNT: r = (uintb)popcount(a) & m; break;
                case CPUI_LZCOUNT: r = (uintb)count_leading_zeros(a) - (uintb)(8 * (8 - n.insize[0])); r &= m; break;
                case CPUI_INT_DIV: if (b == 0) { undef = true; r = 0; } else r = (a / b) & m; break;
                case CPUI_INT_REM: if (b == 0) { undef = true; r = 0; } else r = (a % b) & m; break;
                case CPUI_INT_SDIV:
                {
                    const intb x = sign_extend(a, n.insize[0] * 8 - 1), y = sign_extend(b, n.insize[1] * 8 - 1);
                    if (y == 0 || (y == -1 && (uintb)x == ((uintb)1 << (8 * n.insize[0] - 1)) && n.insize[0] == 8)) { undef = true; r = 0; }
                    else r = (uintb)(x / y) & m;
                    break;
                }
                case CPUI_INT_SREM:
                {
                    const intb x = sign_extend(a, n.insize[0] * 8 - 1), y = sign_extend(b, n.insize[1] * 8 - 1);
                    if (y == 0 || (y == -1 && n.insize[0] == 8)) { undef = true; r = 0; }
                    else r = (uintb)(x % y) & m;
                    break;
                }
                case CPUI_INT_MULT:
                {
                    const unsigned __int128 pu = (unsigned __int128)a * b;
                    const intb sa = sign_extend(a, 8 * n.insize[0] - 1), sb = sign_extend(b, 8 * n.insize[1] - 1);
                    const __int128 ps = (__int128)sa * sb, lim = (__int128)1 << (8 * n.size - 1);
                    bool uns = false, sgn = false;
                    for (int i = 0; i < 2; ++i)
                    {
                        const Node& ch = c->nodes[n.in[i]];
                        if (ch.kind == 2 && ch.opc == CPUI_INT_ZEXT) uns = true;
                        if (ch.kind == 2 && ch.opc == CPUI_INT_SEXT) sgn = true;
                    }
                    const bool fitU = pu <= (unsigned __int128)m, fitS = ps >= -lim && ps < lim;
                    if (uns && !sgn) { if (!fitU) wrapped = true; }
                    else if (sgn && !uns) { if (!fitS) wrapped = true; }
                    else if (!fitU && !fitS) wrapped = true;
                    r = (a * b) & m;
                    if (n.wide)
                    {
                        hasWide = true;
                        // the product RuleDivTermAdd and RuleDivTermAdd2 built as the exact one: a sign extension times the multiplier read as the number it is
                        setExact(id, (sgn && !uns) ? (unsigned __int128)((__int128)sa * (__int128)b) : pu, sgn && !uns);
                    }
                    break;
                }
                case CPUI_INT_RIGHT:
                case CPUI_INT_SRIGHT:
                {
                    unsigned __int128 ev; bool sg;
                    if (n.nin > 1 && exactOf(n.in[0], ev, sg))
                    {
                        unsigned __int128 sh;
                        if (b >= 128) sh = (n.opc == CPUI_INT_SRIGHT && sg && (ev >> 127)) ? ~(unsigned __int128)0 : 0;
                        else if (n.opc == CPUI_INT_SRIGHT || sg) sh = (unsigned __int128)(((__int128)ev) >> (int)b);     // an exact signed number shifts as a signed number
                        else sh = ev >> (int)b;
                        setExact(id, sh, sg);
                        r = (uintb)sh & m;
                        break;
                    }
                    r = evalBinary(n.opc, n.size, n.insize[0], a, b);
                    break;
                }
                case CPUI_INT_ZEXT:
                case CPUI_COPY:
                {
                    unsigned __int128 ev; bool sg;
                    if (n.opc == CPUI_COPY) r = a; else r = a;
                    if (exactOf(n.in[0], ev, sg)) setExact(id, ev, sg);
                    break;
                }
                default:
                    if (n.nin == 1) r = n.opc == CPUI_COPY ? a : 0;
                    else
                    {
                        const TypeOp* t = nullptr;
                        (void)t;
                        r = evalBinary(n.opc, n.size, n.insize[0], a, b);
                    }
                }
            }
            catch (...) { undef = true; }
        }
        if (n.kind == 2 && (r & ~n.nz & calc_mask(n.size)) != 0 && !maskViol) { maskViol = true; viol = &n; }
        have[id] = 1;
        val[id] = r;
        return r;
    }
    static uintb evalBinary(OpCode opc, int sizeout, int sizein, uintb a, uintb b)
    {
        const uintb m = calc_mask(sizeout);
        const int w = 8 * sizein;
        switch (opc)
        {
        case CPUI_INT_ADD: return (a + b) & m;
        case CPUI_INT_AND: return a & b;
        case CPUI_INT_OR: return a | b;
        case CPUI_INT_XOR: return a ^ b;
        case CPUI_INT_MULT: return (a * b) & m;
        case CPUI_INT_LEFT: return b >= (uintb)(8 * sizeout) ? 0 : (a << b) & m;
        case CPUI_INT_RIGHT: return b >= (uintb)w ? 0 : (a >> b);
        case CPUI_INT_SRIGHT:
        {
            const intb x = sign_extend(a, w - 1);
            return (uintb)(x >> (b >= (uintb)w ? w - 1 : (int)b)) & m;
        }
        case CPUI_INT_EQUAL: return a == b;
        case CPUI_INT_NOTEQUAL: return a != b;
        case CPUI_INT_LESS: return a < b;
        case CPUI_INT_LESSEQUAL: return a <= b;
        case CPUI_INT_SLESS: return sign_extend(a, w - 1) < sign_extend(b, w - 1);
        case CPUI_INT_SLESSEQUAL: return sign_extend(a, w - 1) <= sign_extend(b, w - 1);
        case CPUI_BOOL_XOR: return (a ^ b) & 1;
        case CPUI_BOOL_AND: return a & b & 1;
        case CPUI_BOOL_OR: return (a | b) & 1;
        case CPUI_INT_CARRY: return ((a + b) & calc_mask(sizein)) < a;
        case CPUI_INT_SCARRY:
        {
            const __int128 s = (__int128)sign_extend(a, w - 1) + sign_extend(b, w - 1), lim = (__int128)1 << (w - 1);
            return (s < -lim || s >= lim) ? 1 : 0;
        }
        case CPUI_INT_SBORROW:
        {
            const __int128 s = (__int128)sign_extend(a, w - 1) - sign_extend(b, w - 1), lim = (__int128)1 << (w - 1);
            return (s < -lim || s >= lim) ? 1 : 0;
        }
        default: return 0;
        }
    }
};

inline long& maskWide() { static long n = 0; return n; }
struct Tally { long applied = 0, checked = 0, bad = 0, unmatched = 0, skipped = 0, maskskip = 0, uncompared = 0, deadchg = 0, dropped = 0; std::string first; };
inline std::map<std::string, Tally>& tallies()
{
    static std::map<std::string, Tally>* m = nullptr;
    if (!m)
    {
        m = new std::map<std::string, Tally>();
        struct D
        {
            static void dump()
            {
                if (!std::getenv("SE_RULECHECK")) return;
                std::fprintf(stderr, "RULECHECK-MASKWIDE %ld\n", maskWide());
                for (auto& kv : tallies())
                    std::fprintf(stderr, "RULECHECK %-26s applied %8ld checked %8ld bad %5ld unmatched %7ld skipped %7ld maskskip %7ld uncompared %7ld deadchg %7ld dropped %7ld\n", kv.first.c_str(), kv.second.applied, kv.second.checked, kv.second.bad, kv.second.unmatched, kv.second.skipped, kv.second.maskskip, kv.second.uncompared, kv.second.deadchg, kv.second.dropped);
            }
        };
        std::atexit(D::dump);
    }
    return *m;
}

inline std::string text(const Cone& c, int id, int depth = 0)
{
    const Node& n = c.nodes[id];
    std::ostringstream o;
    if (n.kind == 1) o << "#0x" << std::hex << n.value << ":" << std::dec << n.size;
    else if (n.kind == 0) o << "L" << (const void*)n.vn << ":" << n.size << "{nz " << std::hex << n.value << std::dec << "}";
    else
    {
        o << get_opname(n.opc) << ":" << n.size << "{nz " << std::hex << n.nz << std::dec << "}(";
        for (int i = 0; i < n.nin; ++i) { if (i) o << ","; o << (depth > 5 ? std::string("..") : text(c, n.in[i], depth + 1)); }
        o << ")";
    }
    return o.str();
}

inline bool checkOne(const std::string& rule, const Cone& before, int rb, const Cone& after, int ra, uintb consume, std::string& detail, bool& unmatched, long& maskskip, uintb xorAfter = 0)
{
    // the leaves of both cones
    std::map<const Varnode*, uintb> nz;
    std::vector<const Varnode*> leaves;
    std::map<const Varnode*, int> sizes;
    for (int i = 0; i < before.n; ++i)
        if (before.nodes[i].kind == 0 && !nz.count(before.nodes[i].vn)) { nz[before.nodes[i].vn] = before.nodes[i].value; sizes[before.nodes[i].vn] = before.nodes[i].size; leaves.push_back(before.nodes[i].vn); }
    for (int i = 0; i < after.n; ++i)
        if (after.nodes[i].kind == 0)
        {
            auto it = nz.find(after.nodes[i].vn);
            if (it == nz.end()) { unmatched = true; return true; }
            // the same address of memory is not the same varnode if it is another size or place
            for (int j = 0; j < before.n; ++j)
                if (before.nodes[j].kind == 0 && before.nodes[j].vn == after.nodes[i].vn &&
                    (before.nodes[j].size != after.nodes[i].size || before.nodes[j].addr != after.nodes[i].addr || before.nodes[j].spc != after.nodes[i].spc)) { unmatched = true; return true; }
        }
    if (leaves.size() > (size_t)kMaxLeaves) { unmatched = true; return true; }
    Rng rng(0x1234567ull + leaves.size() * 77 + (uintb)rb);
    Eval eb, ea;
    eb.c = &before; ea.c = &after;
    for (int t = 0; t < kTrials; ++t)
    {
        std::map<const Varnode*, uintb> lv;
        for (const Varnode* l : leaves)
        {
            const int sz = sizes[l];
            const uintb m = calc_mask(sz), z = nz[l];
            uintb v;
            const int pick = (int)(rng.next() % 12);
            const uintb w = 8 * sz;
            switch (pick)
            {
            case 0: v = 0; break;
            case 1: v = 1; break;
            case 2: v = m; break;
            case 3: v = (m >> 1) + 1; break;
            case 4: v = m >> 1; break;
            case 5: v = ((m >> 1) + 1) | 1; break;
            case 6: v = (uintb)1 << (rng.next() % w); break;
            case 7: v = ((uintb)1 << (rng.next() % w)) - 1; break;
            default: v = rng.next(); break;
            }
            lv[l] = v & m & z;
        }
        eb.leafVals = ea.leafVals = &lv;
        eb.val.assign(before.n, 0); eb.have.assign(before.n, 0); eb.undef = false; eb.wrapped = false; eb.hasWide = false; eb.maskViol = false; eb.viol = nullptr;
        eb.exact.assign(before.n, 0); eb.haveExact.assign(before.n, 0); ea.exact.assign(after.n, 0); ea.haveExact.assign(after.n, 0);
        ea.val.assign(after.n, 0); ea.have.assign(after.n, 0); ea.undef = false;
        const uintb vb = eb.get(rb), va = ea.get(ra);
        if (eb.undef || ea.undef) continue;
        // a leaf sampled inside its own mask can still give an op a value outside the mask the engine holds for the op's output (the engine's masks are
        // refreshed pass by pass, a rule that rewrites a def can leave an output's mask narrower than its input's has become): not an input the engine can see.
        // Counted, and the first few are printed, so that an unsound mask is not hidden (rulefuzz14 checks the masks themselves).
        if (eb.maskViol && eb.hasWide) { ++maskWide(); continue; }
        if (eb.maskViol)
        {
            if (maskskip++ < 3 && std::getenv("SE_RULECHECK"))
                std::fprintf(stderr, "RULECHECK-MASK %s: a %s of size %d took a value outside its non-zero mask %llx\n", rule.c_str(), get_opname(eb.viol->opc), eb.viol->size, (unsigned long long)eb.viol->nz);
            continue;
        }
        if ((vb & consume) != ((va ^ xorAfter) & consume))
        {
            std::ostringstream o;
            o << "value " << std::hex << vb << " before, " << va << " after (consume " << consume << ") for";
            for (const Varnode* l : leaves) o << " L" << (const void*)l << "=" << lv[l];
            o << "\n    before: " << text(before, rb) << "\n    after:  " << text(after, ra);
            detail = o.str();
            return false;
        }
    }
    return true;
}

inline void verify(const std::string& rule, const PcodeOp* op, const Snap& s, int result)
{
    Tally& t = tallies()[rule];
    if (result > 0) ++t.applied;
    // (RuleDivTermAdd's `x*(c+2^n)` is wider than the multiply that holds it, on the way to RuleDivOpt. The engine flags that multiply and this check values it as
    // the exact product, so the steps of RuleDivTermAdd, RuleDivTermAdd2 and RuleDivOpt are compared like any other rule's.)
    // RuleSignMod2nOpt2's `V - (Vadj & ~(2^n-1))` reads Vadj from a MULTIEQUAL and the branch behind it: the leaf is related to V by the control flow, not by the data
    if (rule == "signmod2nopt2") { if (result > 0) ++t.skipped; return; }
    if (!s.ok) { if (result > 0) { ++t.skipped; ++t.uncompared; } return; }
    if (const char* only = std::getenv("SE_RULECHECK_ONLY")) if (rule.find(only) == std::string::npos) return;
    if (result <= 0)
    {
        bool changed = false;
        for (int i = 0; i < s.nroots && !changed; ++i) changed = rootFp(s.roots[i]) != s.roots[i].fp;
        if (!changed) return;
    }
    t.dropped += s.dropped;
    long compared = 0;
    for (int i = 0; i < s.nroots; ++i)
    {
        const Root& r = s.roots[i];
        const Varnode* v;
        if (r.kind == 5)
        {
            // a slot root: the value the (output-less) reader reads through its slot, whatever varnode is there now
            if (r.reader->isDead() || r.slot >= r.reader->numInput()) continue;
            v = r.reader->getIn(r.slot);
            if (v->isConstant()) { /* a folded constant is a value like any other */ }
        }
        else
        {
            // the root still exists if its defining op still has it as output (pointer comparison only: the varnode may have been freed)
            if (r.def == (const PcodeOp*)0 || r.def->isDead()) continue;
            if (r.def->getOut() != r.vn) continue;
            // RuleCollectTerms moves a term from one INT_ADD of the tree to another; an interior ADD of the tree (its only reader is an ADD, which TermOrder::collect
            // descends through) keeps no value of its own, the root of the tree does. The loop's phi can make such an interior ADD a reader of the rule's op.
            if (rule == "collect_terms" && r.def->code() == CPUI_INT_ADD && r.vn != op->getOut())
            {
                const PcodeOp* nx = r.vn->loneDescend();
                if (nx != (const PcodeOp*)0 && nx->code() == CPUI_INT_ADD)
                {
                    ++t.skipped;
                    // (round 14: the root of the tree is compared when it is one of the roots: the readers, theirs, the siblings)
                    continue;
                }
            }
            // RuleBoolZext turns `zext(V) * -1 + 1` into `zext(!V)` by negating the zext's own input and letting the final ADD read the zext: the zext's value changes
            // by design, the ADD's (a root of its own) does not
            if (rule == "boolzext" && r.vn == op->getOut()) { ++t.skipped; continue; }
            // a root the rule has left without a reader (and that nothing else keeps alive) has no value to keep
            if (r.vn->hasNoDescend() && !r.vn->isAddrForce() && !r.vn->isPersist()) { ++t.skipped; continue; }
            v = r.def->getOut();
        }
        Cone ac;
        Memo m2;
        const int ra = build(v, ac, m2, 100000);
        if (ac.overflow) { ++t.skipped; continue; }
        std::string detail;
        bool unmatched = false;
        ++compared;
        if (!r.dead) ++t.checked;
        long dummyMask = 0;
        // a CBRANCH condition is read with its boolean_flip flag: a rule that flips the flag and negates the condition leaves the branch alone
        const uintb xorAfter = (r.kind == 5 && r.reader->code() == CPUI_CBRANCH && r.reader->isBooleanFlip() != r.flip) ? 1 : 0;
        const bool same = checkOne(rule, s.cone, r.node, ac, ra, r.consume, detail, unmatched, r.dead ? dummyMask : t.maskskip, xorAfter);
        if (!same)
        {
            if (r.dead)
            {
                if (t.deadchg++ < 3 && std::getenv("SE_RULECHECK_DEAD") && std::getenv("SE_RULECHECK"))
                    std::fprintf(stderr, "RULECHECK-DEAD %s (result %d): a dead root (consume 0) changed value: %s\n", rule.c_str(), result, detail.c_str());
                continue;
            }
            ++t.bad;
            if (t.bad <= (std::getenv("SE_RULECHECK_MAX") ? std::atol(std::getenv("SE_RULECHECK_MAX")) : 3) && std::getenv("SE_RULECHECK"))
            {
                std::ostringstream o;
                op->printRaw(o);
                std::ostringstream rt;
                if (r.def) r.def->printRaw(rt);
                else rt << "(slot " << r.slot << " of a reader)";
                rt << " [root kind " << r.kind << ", consume " << std::hex << r.consume << ", " << std::dec << (r.vn ? (int)std::distance(r.vn->beginDescend(), r.vn->endDescend()) : 0) << " readers]";
                std::fprintf(stderr, "RULECHECK-BAD %s (result %d) at %llx on `%s`: %s\n    root: %s\n", rule.c_str(), result, (unsigned long long)op->getAddr().getOffset(), o.str().c_str(), detail.c_str(), rt.str().c_str());
            }
        }
        else if (unmatched) ++t.unmatched;
    }
    if (result > 0 && compared == 0) ++t.uncompared;
}

}  // namespace rc14
}  // namespace ghidra

#endif
