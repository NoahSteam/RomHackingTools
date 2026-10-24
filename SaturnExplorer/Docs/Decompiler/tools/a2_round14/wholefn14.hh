// Area 2 round 14 review: an in-situ, by-value check of the WHOLE FUNCTION after every rule application (and, optionally, every action).
//
// rulecheck13/14.hh compare the cone of the values around the op a rule rewrote. That cannot see a rule whose meaning lives in the control flow: a rule on a
// MULTIEQUAL or a CBRANCH (the phi and the branch behind it are leaves of the cone), a rule that destroys the op it was applied to, or one that changes a
// value elsewhere than at the op and its neighbours. RuleSignMod2nOpt2 was exempt from the in-situ check for exactly that reason, and it takes the condition of
// `Vadj = (W s< 0) ? V + 2^n-1 : V` for V's own sign without asking that W be V (ledger A2-92).
//
// Here the function itself is run. The graph is interpreted from its start block for a few environments (the values of the input varnodes are bytes of a
// per-trial hash, cut by the non-zero mask the engine holds for the input), following CBRANCH (taken = condition XOR boolean_flip, taken goes to the true
// out edge), assigning MULTIEQUAL by the edge the block was entered by, and the values the RETURN op reads are the observation. Before and after a rule
// the observation must be the same, bit for bit under the return value's consume mask. A function whose run reaches an op this file does not evaluate
// (LOAD, STORE, CALL*, INDIRECT, BRANCHIND, pointer ops, floating point), a varnode that is neither input, constant nor defined, a division by zero, or
// more steps than the limit is not judged (counted).
//
// Included by action.cc in a SCRATCH build only (-DSE_RULECHECK, see mkrc14.sh); active with SE_WHOLEFN=1.
//   SE_WHOLEFN=1         check after every rule application; totals at exit (`WHOLEFN <rule> applied n changed n compared n bad n ...`)
//   SE_WHOLEFN_ACTIONS=1 also around every leaf action (a rule pool and the groups are not leaves)
//   SE_WHOLEFN_DUMP=1    keep the text of the graph before each change, to print with a bad application (slow)
//   SE_WHOLEFN_MAX=n     print up to n bad applications of a rule (3)
//   SE_WHOLEFN_TRIALS=n  environments per check (6)
#ifndef SE_WHOLEFN_14_HH
#define SE_WHOLEFN_14_HH

#include "funcdata.hh"
#include "op.hh"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace ghidra {
namespace wf14 {

static const int kMaxTrials = 16;
static const long kStepLimit = 3000;

struct Val
{
    uint64_t v = 0;
    bool ex = false;              // a flagged exact product (PcodeOp::isDivProductWide) or something derived from one by SUBPIECE, right shifts, ZEXT, COPY
    unsigned __int128 e = 0;
    bool sg = false;
    bool k = false;               // known: a constant, or computed from constants and loads of the image only (no input, no phi of unknowns)
};
enum Status { ST_OK = 0, ST_UNSUP, ST_LIMIT, ST_UNDEF };
struct Obs { int size; uint64_t v; uint64_t consume; };
struct Run
{
    Status st = ST_OK;
    std::vector<Obs> obs;
    std::string why;
};

inline uint64_t mix(uint64_t x)
{
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}

inline uint64_t signExt(uint64_t v, int size)
{
    if (size >= 8) return v;
    const uint64_t sign = (uint64_t)1 << (8 * size - 1);
    return (v ^ sign) - sign;
}

inline Val readVn(Funcdata& data, const Varnode* vn, uint64_t seed, const std::unordered_map<const Varnode*, Val>& env, Status& st, std::string& why)
{
    Val r;
    const int size = vn->getSize();
    if (size > 8) { st = ST_UNSUP; why = "wide varnode"; return r; }
    const uint64_t m = calc_mask(size);
    if (vn->isConstant()) { r.v = vn->getOffset() & m; r.k = true; return r; }
    auto it = env.find(vn);
    if (it != env.end()) return it->second;
    if (vn->isInput() && !vn->isFree())
    {
        const AddrSpace* spc = vn->getSpace();
        if (spc == (const AddrSpace*)0 || spc->getType() == IPTR_CONSTANT) { st = ST_UNSUP; why = "input in odd space"; return r; }
        uint64_t acc = 0;
        for (int i = 0; i < size; ++i)
            acc = (acc << 8) | (mix(seed * 0x10001 + spc->getIndex() * 0x1000193ull + (vn->getOffset() + i) * 0x9E3779B1ull) & 0xFF);
        r.v = acc & m & vn->getNZMask();
        return r;
    }
    // a read of the loaded image itself (RuleLoadVarnode folds a LOAD at a constant address into one): the bytes the loader holds
    if (!vn->isWritten() && vn->getSpace() == data.getArch()->getDefaultCodeSpace() && data.getArch()->loader != (LoadImage*)0 && !vn->isInput())
    {
        uint1 buf[8];
        try { data.getArch()->loader->loadFill(buf, size, vn->getAddr()); }
        catch (...) { st = ST_UNSUP; why = "read outside the image"; return r; }
        uint64_t acc = 0;
        if (vn->getSpace()->isBigEndian()) for (int i = 0; i < size; ++i) acc = (acc << 8) | buf[i];
        else for (int i = size - 1; i >= 0; --i) acc = (acc << 8) | buf[i];
        r.v = acc & m;
        r.k = true;
        return r;
    }
    st = ST_UNSUP;
    why = "a varnode with no value";
    return r;
}

// the exact (128-bit) value of a flagged product and what is derived from it, as rulecheck14's model holds it
inline bool exactOf(const Val& a, unsigned __int128& e, bool& sg)
{
    if (!a.ex) return false;
    e = a.e; sg = a.sg;
    return true;
}

inline void execute(Funcdata& data, uint64_t seed, Run& run)
{
    std::unordered_map<const Varnode*, Val> env;
    const BlockGraph& bg = data.getBasicBlocks();
    if (bg.getSize() == 0) { run.st = ST_UNSUP; run.why = "no blocks"; return; }
    const FlowBlock* cur = bg.getStartBlock();
    const FlowBlock* prev = (const FlowBlock*)0;
    long steps = 0;
    Status st = ST_OK;
    std::string why;
    while (cur != (const FlowBlock*)0)
    {
        const BlockBasic* bb = (const BlockBasic*)cur;
        // the phis first, all read before any is written
        std::vector<std::pair<const Varnode*, Val>> phis;
        for (list<PcodeOp*>::const_iterator it = bb->beginOp(); it != bb->endOp(); ++it)
        {
            const PcodeOp* op = *it;
            if (op->code() != CPUI_MULTIEQUAL) break;
            if (prev == (const FlowBlock*)0) { run.st = ST_UNSUP; run.why = "phi at the entry"; return; }
            const int slot = bb->getInIndex(prev);
            if (slot < 0 || slot >= op->numInput()) { run.st = ST_UNSUP; run.why = "phi slot"; return; }
            Val x = readVn(data, op->getIn(slot), seed, env, st, why);
            if (st != ST_OK) { run.st = st; run.why = why; return; }
            x.v &= calc_mask(op->getOut()->getSize());
            phis.push_back({ op->getOut(), x });
        }
        for (auto& p : phis) env[p.first] = p.second;
        const FlowBlock* next = (const FlowBlock*)0;
        bool done = false;
        for (list<PcodeOp*>::const_iterator it = bb->beginOp(); it != bb->endOp(); ++it)
        {
            const PcodeOp* op = *it;
            const OpCode opc = op->code();
            if (opc == CPUI_MULTIEQUAL) continue;
            if (++steps > kStepLimit) { run.st = ST_LIMIT; return; }
            const int n = op->numInput();
            if (opc == CPUI_BRANCH) continue;
            if (opc == CPUI_RETURN)
            {
                for (int i = 1; i < n; ++i)
                {
                    Val x = readVn(data, op->getIn(i), seed, env, st, why);
                    if (st != ST_OK) { run.st = st; run.why = why; return; }
                    uint64_t cons = op->getIn(i)->getConsume() & calc_mask(op->getIn(i)->getSize());
                    if (cons == 0) cons = calc_mask(op->getIn(i)->getSize());
                    run.obs.push_back({ op->getIn(i)->getSize(), x.v, cons });
                }
                done = true;
                break;
            }
            if (opc == CPUI_CBRANCH)
            {
                Val c = readVn(data, op->getIn(1), seed, env, st, why);
                if (st != ST_OK) { run.st = st; run.why = why; return; }
                const bool taken = ((c.v & 1) != 0) != op->isBooleanFlip();
                if (bb->sizeOut() != 2) { run.st = ST_UNSUP; run.why = "cbranch edges"; return; }
                next = taken ? bb->getTrueOut() : bb->getFalseOut();
                break;
            }
            const Varnode* outvn = op->getOut();
            if (outvn == (const Varnode*)0) { run.st = ST_UNSUP; run.why = std::string("op without output: ") + get_opname(opc); return; }
            const int size = outvn->getSize();
            if (size > 8) { run.st = ST_UNSUP; run.why = "wide output"; return; }
            const uint64_t m = calc_mask(size);
            Val x, y;
            if (n > 0 && opc != CPUI_LOAD) { x = readVn(data, op->getIn(0), seed, env, st, why); if (st != ST_OK) { run.st = st; run.why = why; return; } }
            if (n > 1 && opc != CPUI_LOAD) { y = readVn(data, op->getIn(1), seed, env, st, why); if (st != ST_OK) { run.st = st; run.why = why; return; } }
            const uint64_t a = x.v, b = y.v;
            const int sa = n > 0 ? op->getIn(0)->getSize() : 0, sb = n > 1 ? op->getIn(1)->getSize() : 0;
            const uint64_t wa = 8 * (uint64_t)sa;
            Val r;
            switch (opc)
            {
            case CPUI_LOAD:
            {
                // only a load at a known address of the loaded image (the literal pool of the function): the bytes the loader holds, in the space's byte order
                Val pv = readVn(data, op->getIn(1), seed, env, st, why);
                if (st != ST_OK) { run.st = st; run.why = why; return; }
                const AddrSpace* spc = op->getIn(0)->getSpaceFromConst();
                if (!pv.k || spc == (const AddrSpace*)0 || data.getArch()->loader == (LoadImage*)0 || spc != data.getArch()->getDefaultCodeSpace()) { run.st = ST_UNSUP; run.why = "load through a pointer"; return; }
                uint1 buf[8];
                try { data.getArch()->loader->loadFill(buf, size, Address(const_cast<AddrSpace*>(spc), pv.v)); }
                catch (...) { run.st = ST_UNSUP; run.why = "load outside the image"; return; }
                uint64_t acc = 0;
                if (spc->isBigEndian()) for (int i = 0; i < size; ++i) acc = (acc << 8) | buf[i];
                else for (int i = size - 1; i >= 0; --i) acc = (acc << 8) | buf[i];
                r.v = acc & m;
                r.k = true;
                break;
            }
            case CPUI_COPY: r = x; r.v = a & m; break;
            case CPUI_CAST: r = x; r.v = a & m; break;
            case CPUI_INT_ADD: r.v = (a + b) & m; break;
            case CPUI_INT_SUB: r.v = (a - b) & m; break;
            case CPUI_INT_2COMP: r.v = (0 - a) & m; break;
            case CPUI_INT_NEGATE: r.v = (~a) & m; break;
            case CPUI_INT_XOR: r.v = (a ^ b) & m; break;
            case CPUI_INT_AND: r.v = (a & b) & m; break;
            case CPUI_INT_OR: r.v = (a | b) & m; break;
            case CPUI_INT_ZEXT: r = x; r.v = a & m; break;
            case CPUI_INT_SEXT: r.v = signExt(a, sa) & m; break;
            case CPUI_SUBPIECE:
            {
                unsigned __int128 ev; bool sg;
                if (exactOf(x, ev, sg) && b < 16)
                {
                    const unsigned __int128 sh = sg ? (unsigned __int128)(((__int128)ev) >> (8 * (int)b)) : ev >> (8 * (int)b);
                    r.ex = true; r.e = sh; r.sg = sg; r.v = (uint64_t)sh & m;
                    break;
                }
                r.v = b >= 8 ? 0 : (a >> (8 * b)) & m;
                break;
            }
            case CPUI_PIECE: r.v = (((sb >= 8) ? 0 : (a << (8 * sb))) | b) & m; break;
            case CPUI_INT_LEFT: r.v = (b >= (uint64_t)(8 * size)) ? 0 : (a << b) & m; break;
            case CPUI_INT_RIGHT:
            case CPUI_INT_SRIGHT:
            {
                unsigned __int128 ev; bool sg;
                if (exactOf(x, ev, sg))
                {
                    unsigned __int128 sh;
                    if (b >= 128) sh = (opc == CPUI_INT_SRIGHT && sg && (ev >> 127)) ? ~(unsigned __int128)0 : 0;
                    else if (opc == CPUI_INT_SRIGHT || sg) sh = (unsigned __int128)(((__int128)ev) >> (int)b);
                    else sh = ev >> (int)b;
                    r.ex = true; r.e = sh; r.sg = sg; r.v = (uint64_t)sh & m;
                    break;
                }
                if (opc == CPUI_INT_RIGHT) r.v = (b >= wa) ? 0 : (a >> b);
                else r.v = (uint64_t)((int64_t)signExt(a, sa) >> (b >= wa ? wa - 1 : b)) & m;
                break;
            }
            case CPUI_INT_MULT:
            {
                r.v = (a * b) & m;
                if (op->isDivProductWide())
                {
                    bool uns = false, sgn = false;
                    for (int i = 0; i < 2; ++i)
                    {
                        const Varnode* in = op->getIn(i);
                        if (in->isWritten() && in->getDef()->code() == CPUI_INT_ZEXT) uns = true;
                        if (in->isWritten() && in->getDef()->code() == CPUI_INT_SEXT) sgn = true;
                    }
                    const unsigned __int128 pu = (unsigned __int128)a * b;
                    const int64_t sx = (int64_t)signExt(a, sa);
                    r.ex = true;
                    r.sg = sgn && !uns;
                    r.e = r.sg ? (unsigned __int128)((__int128)sx * (__int128)b) : pu;
                }
                break;
            }
            case CPUI_INT_DIV: if (b == 0) { run.st = ST_UNDEF; return; } r.v = (a / b) & m; break;
            case CPUI_INT_REM: if (b == 0) { run.st = ST_UNDEF; return; } r.v = (a % b) & m; break;
            case CPUI_INT_SDIV:
            {
                const int64_t p = (int64_t)signExt(a, sa), q = (int64_t)signExt(b, sb);
                if (q == 0 || (q == -1 && (uint64_t)p == ((uint64_t)1 << (wa - 1)))) { run.st = ST_UNDEF; return; }
                r.v = (uint64_t)(p / q) & m;
                break;
            }
            case CPUI_INT_SREM:
            {
                const int64_t p = (int64_t)signExt(a, sa), q = (int64_t)signExt(b, sb);
                if (q == 0 || (q == -1 && (uint64_t)p == ((uint64_t)1 << (wa - 1)))) { run.st = ST_UNDEF; return; }
                r.v = (uint64_t)(p % q) & m;
                break;
            }
            case CPUI_INT_EQUAL: r.v = a == b; break;
            case CPUI_INT_NOTEQUAL: r.v = a != b; break;
            case CPUI_INT_LESS: r.v = a < b; break;
            case CPUI_INT_LESSEQUAL: r.v = a <= b; break;
            case CPUI_INT_SLESS: r.v = (int64_t)signExt(a, sa) < (int64_t)signExt(b, sb); break;
            case CPUI_INT_SLESSEQUAL: r.v = (int64_t)signExt(a, sa) <= (int64_t)signExt(b, sb); break;
            case CPUI_INT_CARRY: r.v = ((a + b) & calc_mask(sa)) < a; break;
            case CPUI_INT_SCARRY:
            {
                const __int128 s = (__int128)(int64_t)signExt(a, sa) + (int64_t)signExt(b, sa), lim = (__int128)1 << (wa - 1);
                r.v = (s < -lim || s >= lim) ? 1 : 0;
                break;
            }
            case CPUI_INT_SBORROW:
            {
                const __int128 s = (__int128)(int64_t)signExt(a, sa) - (int64_t)signExt(b, sa), lim = (__int128)1 << (wa - 1);
                r.v = (s < -lim || s >= lim) ? 1 : 0;
                break;
            }
            case CPUI_BOOL_NEGATE: r.v = (a ^ 1) & 1; break;
            case CPUI_BOOL_AND: r.v = a & b & 1; break;
            case CPUI_BOOL_OR: r.v = (a | b) & 1; break;
            case CPUI_BOOL_XOR: r.v = (a ^ b) & 1; break;
            case CPUI_POPCOUNT: r.v = (uint64_t)__builtin_popcountll(a) & m; break;
            case CPUI_LZCOUNT:
            {
                int c = 0;
                for (int i = (int)wa - 1; i >= 0 && !((a >> i) & 1); --i) ++c;
                r.v = (uint64_t)c & m;
                break;
            }
            default:
                run.st = ST_UNSUP;
                run.why = std::string("unsupported op ") + get_opname(opc);
                return;
            }
            if (opc != CPUI_LOAD) r.k = (n < 1 || x.k) && (n < 2 || y.k) && (n < 3 || true);
            env[outvn] = r;
        }
        if (done) return;
        if (next == (const FlowBlock*)0)
        {
            // a block that ends without a CBRANCH or RETURN: its one out edge
            if (bb->sizeOut() == 1) next = bb->getOut(0);
            else { run.st = ST_UNSUP; run.why = "block with no way on"; return; }
        }
        prev = cur;
        cur = next;
    }
}

inline uint64_t graphHash(Funcdata& data)
{
    uint64_t h = 1469598103934665603ull;
    auto add = [&](uint64_t x) { h = (h ^ x) * 1099511628211ull; };
    const BlockGraph& bg = data.getBasicBlocks();
    for (int i = 0; i < bg.getSize(); ++i)
    {
        const FlowBlock* fb = bg.getBlock(i);
        add((uint64_t)(uintptr_t)fb);
        for (int j = 0; j < fb->sizeOut(); ++j) add((uint64_t)(uintptr_t)fb->getOut(j) * 31 + j);
        if (fb->getType() != FlowBlock::t_basic) continue;
        const BlockBasic* bb = (const BlockBasic*)fb;
        for (list<PcodeOp*>::const_iterator it = bb->beginOp(); it != bb->endOp(); ++it)
        {
            const PcodeOp* op = *it;
            add((uint64_t)op->code() * 131 + (op->isBooleanFlip() ? 1 : 0) + (op->isDivProductWide() ? 2 : 0));
            add((uint64_t)(uintptr_t)op->getOut());
            for (int k = 0; k < op->numInput(); ++k)
            {
                const Varnode* v = op->getIn(k);
                add(v->isConstant() ? v->getOffset() * 17 + v->getSize() : (uint64_t)(uintptr_t)v);
            }
        }
    }
    return h;
}

inline std::string dumpGraph(Funcdata& data)
{
    std::ostringstream o;
    const BlockGraph& bg = data.getBasicBlocks();
    for (int i = 0; i < bg.getSize(); ++i)
    {
        const FlowBlock* fb = bg.getBlock(i);
        if (fb->getType() != FlowBlock::t_basic) continue;
        const BlockBasic* bb = (const BlockBasic*)fb;
        o << "  block " << i << " (in";
        for (int j = 0; j < bb->sizeIn(); ++j) o << " " << bb->getIn(j)->getIndex();
        o << ", out";
        for (int j = 0; j < bb->sizeOut(); ++j) o << " " << bb->getOut(j)->getIndex();
        o << "):\n";
        for (list<PcodeOp*>::const_iterator it = bb->beginOp(); it != bb->endOp(); ++it)
        {
            o << "    ";
            (*it)->printRaw(o);
            if ((*it)->isBooleanFlip()) o << "  [flip]";
            if ((*it)->isDivProductWide()) o << "  [wide]";
            o << "\n";
        }
    }
    return o.str();
}

struct Tally
{
    long applied = 0, changed = 0, compared = 0, bad = 0, unsupBefore = 0, unsupAfter = 0, limit = 0, undef = 0, vacuous = 0;
};
inline std::map<std::string, long>& reasons() { static std::map<std::string, long>* m = new std::map<std::string, long>(); return *m; }

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
                if (!std::getenv("SE_WHOLEFN")) return;
                for (auto& kv : reasons()) std::fprintf(stderr, "WHOLEFN-WHY %ld %s\n", kv.second, kv.first.c_str());
                for (auto& kv : *m)
                    std::fprintf(stderr, "WHOLEFN %-26s applied %8ld changed %8ld compared %9ld bad %5ld unsupBefore %7ld unsupAfter %7ld limit %6ld undef %6ld vacuous %8ld\n", kv.first.c_str(), kv.second.applied, kv.second.changed, kv.second.compared, kv.second.bad, kv.second.unsupBefore, kv.second.unsupAfter, kv.second.limit, kv.second.undef, kv.second.vacuous);
            }
        };
        std::atexit(D::dump);
    }
    return *m;
}

struct State
{
    uint64_t hash = 0;
    bool valid = false;
    std::vector<Run> runs;
    std::string text;
};
inline State& cache() { static State s; return s; }

inline int trials()
{
    static int n = std::getenv("SE_WHOLEFN_TRIALS") ? std::atoi(std::getenv("SE_WHOLEFN_TRIALS")) : 6;
    return n < 1 ? 1 : (n > kMaxTrials ? kMaxTrials : n);
}

inline void runAll(Funcdata& data, State& s)
{
    s.runs.assign(trials(), Run());
    for (int t = 0; t < trials(); ++t)
    {
        s.runs[t].obs.clear();
        execute(data, 0xC0FFEEull + (uint64_t)t * 7919, s.runs[t]);
    }
}

inline void before(Funcdata& data, State& pre)
{
    pre.hash = graphHash(data);
    State& c = cache();
    if (c.valid && c.hash == pre.hash) { pre.runs = c.runs; pre.text = c.text; pre.valid = true; return; }
    runAll(data, pre);
    pre.valid = true;
    static const bool wantText = std::getenv("SE_WHOLEFN_DUMP") != nullptr;
    if (wantText) pre.text = dumpGraph(data);
    c = pre;
}

inline void after(Funcdata& data, const State& pre, const std::string& what, int result, const PcodeOp* op)
{
    Tally& t = tallies()[what];
    if (result > 0) ++t.applied;
    const uint64_t h = graphHash(data);
    if (h == pre.hash) return;
    ++t.changed;
    State post;
    post.hash = h;
    runAll(data, post);
    post.valid = true;
    static const bool wantText = std::getenv("SE_WHOLEFN_DUMP") != nullptr;
    if (wantText) post.text = dumpGraph(data);
    for (int i = 0; i < trials(); ++i)
    {
        const Run& b = pre.runs[i];
        const Run& a = post.runs[i];
        if (b.st == ST_LIMIT || a.st == ST_LIMIT) { ++t.limit; continue; }
        if (b.st == ST_UNDEF || a.st == ST_UNDEF) { ++t.undef; continue; }
        if (b.st == ST_UNSUP) { ++t.unsupBefore; ++reasons()["before: " + b.why + " (" + what + ")"]; continue; }
        if (a.st == ST_UNSUP) { ++t.unsupAfter; ++reasons()["after: " + a.why + " (" + what + ")"]; continue; }
        ++t.compared;
        if (b.obs.empty() && a.obs.empty()) ++t.vacuous;      // a RETURN that reads nothing (before the return value is recovered): nothing is observed
        bool bad = b.obs.size() != a.obs.size();
        size_t at = 0;
        if (!bad)
            for (size_t k = 0; k < b.obs.size(); ++k)
            {
                // the return value may be narrowed (the sub-variable flow cuts a register to the bytes the return needs, the prototype's output gets smaller): what
                // was removed must be an extension of what is kept (zeros, or the sign), and the kept bytes equal under the consume mask
                const int bs = b.obs[k].size, as = a.obs[k].size;
                const uint64_t cons = b.obs[k].consume & calc_mask(bs);
                if (as < bs)
                {
                    const uint64_t keep = calc_mask(as);
                    const uint64_t zx = a.obs[k].v & keep;
                    const uint64_t sx = signExt(a.obs[k].v & keep, as) & calc_mask(bs);
                    if (b.obs[k].v != zx && b.obs[k].v != sx && (b.obs[k].v & cons) != (zx & cons) && (b.obs[k].v & cons) != (sx & cons)) { bad = true; at = k; break; }
                }
                else if (as > bs)
                {
                    if ((cons & ~calc_mask(bs)) != 0 || ((b.obs[k].v ^ a.obs[k].v) & cons) != 0) { bad = true; at = k; break; }
                }
                else if (((b.obs[k].v ^ a.obs[k].v) & cons) != 0) { bad = true; at = k; break; }
            }
        if (bad)
        {
            ++t.bad;
            static const long maxShow = std::getenv("SE_WHOLEFN_MAX") ? std::atol(std::getenv("SE_WHOLEFN_MAX")) : 3;
            if (t.bad <= maxShow && std::getenv("SE_WHOLEFN"))
            {
                std::ostringstream o;
                if (op) op->printRaw(o);
                std::fprintf(stderr, "WHOLEFN-BAD %s (result %d) at %llx on `%s`: trial %d observation %zu: %llx before, %llx after (consume %llx)\n", what.c_str(), result,
                             op ? (unsigned long long)op->getAddr().getOffset() : 0ull, o.str().c_str(), i, at,
                             b.obs.size() > at ? (unsigned long long)b.obs[at].v : 0ull, a.obs.size() > at ? (unsigned long long)a.obs[at].v : 0ull,
                             b.obs.size() > at ? (unsigned long long)b.obs[at].consume : 0ull);
                if (!pre.text.empty()) std::fprintf(stderr, " graph before:\n%s", pre.text.c_str());
                if (!post.text.empty()) std::fprintf(stderr, " graph after:\n%s", post.text.c_str());
            }
            break;
        }
    }
    cache() = post;
}

}  // namespace wf14
}  // namespace ghidra

#endif
