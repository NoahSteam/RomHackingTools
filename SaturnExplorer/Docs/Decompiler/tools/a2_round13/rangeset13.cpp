// Area 2 round 13 review: patch 0062's range operations at every size from 1 to 8 bytes.
// fwd12.cpp (round 12) checked the set operations (intersect, circleUnion, minimalContainer, invert, contains(range)) against 65,536-bit sets at two bytes, the
// mixed-mask intersect at one and two bytes, setNZMask at 1 and 2 bytes exhaustively and widen at one byte. Patch 0062 changed newDomain (the intersect of two masks),
// setNZMask and widen; this tool asks the same questions at every size from 1 to 8 bytes, and every pair of sizes for the intersect, by MEMBERSHIP: the sets are
// written out from the definition (left, right, step, mask) in this file, and a result is judged on a sample taken at the places an arithmetic error shows (the
// bounds and their neighbours, both sides of the narrower domain, the ends of the domain, the sign boundary, elements of both ranges, random values).
//
// Build: build13.sh-style (g++ -O2 -std=gnu++14 -I<cpp> rangeset13.cpp <engine objects> -lz).  Run: rangeset13 <section> <seed> <cases>
//   sections: inter (same size and mixed), union, nz, widen, all
#include "libdecomp.hh"
#include "rangeutil.hh"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <random>
#include <string>
#include <vector>
using namespace ghidra;
typedef ghidra::uintb u64;

static u64 M(int sz) { return sz >= 8 ? ~0ull : ((1ull << (8 * sz)) - 1); }
static std::mt19937_64 rg;
static u64 R() { return rg(); }

struct Rg { u64 l, r; int st; int sz; };
static bool member(u64 v, u64 l, u64 r, int st, u64 m)
{
    v &= m;
    if (v % st != l % st) return false;
    if (l == r) return true;
    return l < r ? (v >= l && v < r) : (v < r || v >= l);
}
static bool member(u64 v, const Rg &g) { return member(v, g.l, g.r, g.st, M(g.sz)); }
static std::string T(const Rg &g) { char b[112]; snprintf(b, sizeof b, "[%llx,%llx)/%d@%d", (unsigned long long)g.l, (unsigned long long)g.r, g.st, g.sz); return b; }
static std::string T(const CircleRange &q)
{
    char b[128];
    snprintf(b, sizeof b, "[%llx,%llx)/%d m%llx%s", (unsigned long long)q.getMin(), (unsigned long long)q.getEnd(), q.getStep(), (unsigned long long)q.getMask(), q.isEmpty() ? " empty" : "");
    return b;
}
static bool qmember(const CircleRange &q, u64 v) { return !q.isEmpty() && member(v, q.getMin(), q.getEnd(), q.getStep(), q.getMask()); }

static u64 pickVal(int sz)
{
    const u64 m = M(sz);
    switch (R() % 9)
    {
    case 0: return R() & m;
    case 1: return (R() % 17) & m;
    case 2: return (m - R() % 17) & m;
    case 3: return ((m >> 1) + (R() % 17) - 8) & m;
    case 4: { int k = R() % (8 * sz); return ((1ull << k) + (R() % 9) - 4) & m; }
    case 5: { int k = R() % (8 * sz); return ((0 - (1ull << k)) + (R() % 9) - 4) & m; }
    case 6: return (R() & R() & R()) & m;
    case 7:
    {   // the edge of a narrower domain, which the wider range's bounds sit next to when the intersect cuts it
        int k = 8 * (1 + R() % std::max(1, sz));
        const u64 e = k >= 64 ? ~0ull : ((1ull << k) - 1);
        return (e + (R() % 9) - 4) & m;
    }
    default: return (R() | R() | R()) & m;
    }
}
static Rg genRange(int sz)
{
    const u64 m = M(sz);
    static const int steps[] = { 1, 1, 1, 1, 2, 2, 4, 8, 16, 32 };
    int st = steps[R() % 10];
    while ((u64)st > m) st >>= 1;
    Rg g; g.st = st; g.sz = sz;
    g.l = pickVal(sz);
    switch (R() % 12)
    {
    case 0: g.r = g.l; break;
    case 1: g.r = (g.l + st) & m; break;
    case 2: g.r = (g.l + 2 * st) & m; break;
    case 3: g.r = (g.l - st) & m; break;
    case 4: g.r = 0; break;                              // ends at the top of the domain
    default: g.r = pickVal(sz);
    }
    const u64 rem = g.l % st;
    g.r = (g.r - g.r % st + rem) & m;
    if (g.r % st != rem) g.r = g.l;
    if (g.l == g.r) { g.l = g.r = rem; }
    return g;
}
static u64 count(const Rg &g)
{
    const u64 m = M(g.sz);
    return g.l == g.r ? m / g.st + 1 : ((g.r - g.l) & m) / g.st;
}
static u64 elem(const Rg &g, u64 j) { return (g.l + j * (u64)g.st) & M(g.sz); }

struct Tally { long n = 0, bad = 0, refused = 0; std::string ex[3]; };
static std::map<std::string, Tally> G;
static void note(const std::string &key, const std::string &desc, bool bad)
{
    Tally &t = G[key];
    t.n++;
    if (bad) { t.bad++; if (t.bad <= 3) t.ex[t.bad - 1] = desc; }
}
static void refused(const std::string &key) { G[key].refused++; }

static void points(const Rg &a, std::vector<u64> &out)
{
    const u64 m = M(a.sz);
    const u64 n = count(a);
    for (u64 j = 0; j < std::min<u64>(n, 6); ++j) { out.push_back(elem(a, j)); out.push_back(elem(a, n - 1 - j)); }
    for (int k = 0; k < 14 && n > 0; ++k) out.push_back(elem(a, R() % n));
    for (u64 b : { a.l, a.r })
        for (int d = -3; d <= 3; ++d) out.push_back((b + (u64)((long long)d * a.st)) & m);
}
static void edges(int sz, std::vector<u64> &out)
{
    const u64 m = M(sz);
    for (u64 t : { (u64)0, m, m >> 1, (m >> 1) + 1 })
        for (int d = -4; d <= 4; ++d) out.push_back((t + d) & m);
}
static bool wellFormed(const CircleRange &q, u64 wantMask, std::string &why)
{
    if (q.isEmpty()) return true;
    if (q.getMask() != wantMask) { why = "mask"; return false; }
    int st = q.getStep();
    if (st < 1 || (st & (st - 1))) { why = "step"; return false; }
    if ((q.getMin() & ~q.getMask()) || (q.getEnd() & ~q.getMask())) { why = "bound outside mask"; return false; }
    if (q.getMin() % st != q.getEnd() % st) { why = "bounds differ mod step"; return false; }
    return true;
}

static void inter(long iters)
{
    for (long it = 0; it < iters; ++it)
    {
        int sa = 1 + R() % 8, sb = 1 + R() % 8;
        if (R() % 4 == 0) sb = sa;
        Rg a = genRange(sa), b = genRange(sb);
        if (R() % 5 == 0 && sa == sb) { b.st = a.st; b.l = pickVal(sb); b.l %= b.st; b.r = b.l; if (R() % 2) b.r = (b.l + a.st * (1 + R() % 4)) & M(sb); if (b.r % b.st != b.l % b.st) b.r = b.l; }
        CircleRange A(a.l, a.r, sa, a.st), B(b.l, b.r, sb, b.st);
        const u64 mn = M(std::min(sa, sb)), mw = M(std::max(sa, sb));
        const std::string kind = sa == sb ? "same size" : "mixed sizes";
        const std::string desc = T(a) + " , " + T(b);
        std::vector<u64> P;
        points(a, P); points(b, P); edges(std::min(sa, sb), P); edges(std::max(sa, sb), P);
        for (int k = 0; k < 10; ++k) P.push_back(R() & mn);
        CircleRange q = A;
        const int rc = q.intersect(B);
        if (rc != 0)
        {
            note(kind + ": a refused intersect leaves this as it was", desc, !(q == A));
            refused(kind + " refused");
            continue;
        }
        std::string why;
        if (!wellFormed(q, mn, why)) { note(kind + ": result well formed", desc + " -> " + T(q) + " " + why, true); continue; }
        note(kind + ": result well formed", desc, false);
        bool missing = false, extra = false; u64 at = 0;
        for (u64 v : P)
        {
            if (v > mn) { if (qmember(q, v) && (v & mn) == v) { /* cannot happen: handled by the next line */ } }
            if (v > mn) continue;
            const bool want = member(v, a) && member(v, b);
            const bool got = qmember(q, v);
            if (want && !got && !missing) { missing = true; at = v; }
            if (!want && got && !extra) { extra = true; at = v; }
        }
        char buf[96]; snprintf(buf, sizeof buf, " at %llx", (unsigned long long)at);
        note(kind + ": values MISSING from an answered intersect", desc + " -> " + T(q) + buf, missing);
        note(kind + ": values EXTRA in an answered intersect", desc + " -> " + T(q) + buf, extra);
        // the engine's contains agrees with the definition on the result
        bool disagree = false;
        for (u64 v : P) if (v <= mn && !q.isEmpty() && q.contains(v) != qmember(q, v)) { disagree = true; at = v; }
        note(kind + ": contains(value) on the result", desc + " -> " + T(q), disagree);
        (void)mw;
    }
}

static void unions(long iters)
{
    for (long it = 0; it < iters; ++it)
    {
        const int sz = 1 + R() % 8;
        Rg a = genRange(sz), b = genRange(sz);
        if (R() % 4 == 0) { b.st = a.st; b.l = pickVal(sz) % b.st; b.r = (b.l + a.st * (R() % 6)) & M(sz); if (b.r % b.st != b.l % b.st) b.r = b.l; if (b.l == b.r) b.l = b.r = b.l % b.st; }
        CircleRange A(a.l, a.r, sz, a.st), B(b.l, b.r, sz, b.st);
        const std::string desc = T(a) + " , " + T(b);
        std::vector<u64> P;
        points(a, P); points(b, P); edges(sz, P);
        for (int k = 0; k < 10; ++k) P.push_back(R() & M(sz));
        {
            CircleRange q = A;
            const int rc = q.circleUnion(B);
            if (rc == 0)
            {
                bool bad = false; u64 at = 0;
                for (u64 v : P) if ((member(v, a) || member(v, b)) != qmember(q, v)) { bad = true; at = v; break; }
                char buf[64]; snprintf(buf, sizeof buf, " at %llx", (unsigned long long)at);
                note("circleUnion answered 0 is exact", desc + " -> " + T(q) + buf, bad);
            }
            else { note("circleUnion refused leaves this", desc, !(q == A)); refused("circleUnion refused"); }
        }
        {
            CircleRange q = A;
            const bool ok = q.minimalContainer(B, 1 << (R() % 6));
            if (ok)
            {
                bool bad = false; u64 at = 0;
                for (u64 v : P) if ((member(v, a) || member(v, b)) && !qmember(q, v)) { bad = true; at = v; break; }
                char buf[64]; snprintf(buf, sizeof buf, " at %llx", (unsigned long long)at);
                std::string why;
                note("minimalContainer holds both", desc + " -> " + T(q) + buf, bad);
                note("minimalContainer well formed", desc + " -> " + T(q) + " " + why, !wellFormed(q, M(sz), why));
            }
        }
        {
            // contains(range) claimed true is true
            const bool c = A.contains(B);
            if (c)
            {
                bool bad = false;
                for (u64 v : P) if (member(v, b) && !member(v, a)) { bad = true; break; }
                note("contains(range) true is true", desc, bad);
            }
        }
    }
}

static void nz(long iters)
{
    for (int sz = 1; sz <= 8; ++sz)
    {
        const u64 m = M(sz);
        std::vector<u64> masks;
        // every run of bits (the masks the range can express) and its neighbours, the ends, the sign boundary
        for (int lo = 0; lo < 8 * sz; ++lo)
            for (int hi = lo; hi < 8 * sz; ++hi)
            {
                u64 run = (hi - lo + 1 >= 64 ? ~0ull : ((1ull << (hi - lo + 1)) - 1)) << lo;
                masks.push_back(run & m);
                masks.push_back((run | 1) & m);
                masks.push_back((run | (1ull << (8 * sz - 1))) & m);
            }
        for (long k = 0; k < iters; ++k) masks.push_back(pickVal(sz) & m);
        for (u64 nzm : masks)
        {
            if (nzm == 0) continue;
            CircleRange q;
            const bool ok = q.setNZMask(nzm, sz);
            const std::string key = "setNZMask size " + std::to_string(sz);
            if (!ok)
            {
                // a refusal is for too many transitions or a lowest set bit of 31 or more; nothing else
                refused(key);
                continue;
            }
            std::string why;
            bool bad = !wellFormed(q, m, why);
            std::vector<u64> vs = { 0, nzm, nzm & (0 - nzm), nzm >> 1 << 1, nzm >> 1 };
            for (int k = 0; k < 24; ++k) vs.push_back(R() & nzm);
            for (u64 v : vs)
            {
                v &= nzm;
                if (!qmember(q, v) || !q.contains(v)) { bad = true; char b[64]; snprintf(b, sizeof b, " misses %llx", (unsigned long long)v); why += b; break; }
            }
            // the step the range claims divides every value the mask allows
            if (!bad && q.getStep() > 1 && (nzm & (u64)(q.getStep() - 1))) { bad = true; why += " step beyond the mask's lowest bit"; }
            note(key, "nz " + std::to_string(nzm) + " -> " + T(q) + " " + why, bad);
        }
    }
}

static void widen(long iters)
{
    for (long it = 0; it < iters; ++it)
    {
        const int sz = 1 + R() % 8;
        const u64 m = M(sz);
        Rg b = genRange(sz);
        Rg a = genRange(sz);
        if (R() % 2)
        {   // a sub-range of b: its elements are some of b's
            const u64 n = count(b);
            if (n < 2) continue;
            const u64 i0 = R() % n, len = 1 + R() % (n - i0);
            a.st = b.st; a.sz = sz; a.l = elem(b, i0); a.r = elem(b, i0 + len);
            if (i0 + len >= n && !(b.l == b.r)) a.r = b.r;
            if (a.l == a.r) a.l = a.r = a.l % a.st;
        }
        CircleRange A(a.l, a.r, sz, a.st), B(b.l, b.r, sz, b.st);
        if (!B.contains(A)) continue;
        for (int stable = 0; stable < 2; ++stable)
        {
            CircleRange q = A;
            q.widen(B, stable != 0);
            std::string why;
            std::vector<u64> P;
            points(a, P); points(b, P); edges(sz, P);
            bool bad = !wellFormed(q, m, why);
            u64 at = 0;
            for (u64 v : P) if (member(v, a) && !qmember(q, v)) { bad = true; at = v; why += " misses " + std::to_string(at); break; }
            if (q.getStep() != a.st) { bad = true; why += " step changed"; }
            note(stable ? "widen (left stable)" : "widen (right stable)", T(a) + " in " + T(b) + " -> " + T(q) + " " + why, bad);
        }
    }
}

int main(int argc, char **argv)
{
    std::string sec = argc > 1 ? argv[1] : "all";
    u64 seed = argc > 2 ? strtoull(argv[2], 0, 0) : 1;
    long n = argc > 3 ? atol(argv[3]) : 20000;
    rg.seed(seed);
    auto want = [&](const char *s) { return sec == "all" || sec == s; };
    if (want("inter")) inter(n * 10);
    if (want("union")) unions(n * 10);
    if (want("nz")) nz(std::max<long>(n / 20, 100));
    if (want("widen")) widen(n * 10);
    long bad = 0;
    for (auto &kv : G)
    {
        printf("%-72s n=%-9ld bad=%-6ld refused=%ld\n", kv.first.c_str(), kv.second.n, kv.second.bad, kv.second.refused);
        for (int i = 0; i < 3; ++i) if (!kv.second.ex[i].empty()) printf("      %s\n", kv.second.ex[i].c_str());
        bad += kv.second.bad;
    }
    printf("rangeset13 %s seed %llu: %ld bad\n", sec.c_str(), (unsigned long long)seed, bad);
    return bad ? 1 : 0;
}
