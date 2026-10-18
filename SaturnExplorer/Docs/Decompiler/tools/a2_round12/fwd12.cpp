// Area 2 round 12 review: a second, independent audit of the range arithmetic that patch 0059 repaired.
// What it adds to round 11's forward_audit.cpp and the pin TestForwardRangesRound11:
//   * every size from 1 to 8 bytes for the push-forwards (the pin has 1, 2, 4, 8), every SUBPIECE (in, out, offset), every ZEXT / SEXT
//     (in, out) pair, the shift amount as a 1, 4 or 8 byte constant;
//   * ranges drawn from structured bounds (powers of two and their neighbours, both sign boundaries, the ends), all steps 1 to 32;
//   * the elements pushed through an operation are the ends, the elements round the points where an operation changes behaviour (0, the
//     top, the sign boundary) and, for INT_LEFT / INT_MULT, round the points where the product crosses a multiple of the domain;
//   * a union / container / intersect / invert / contains(range) / getSize / getNext check against bitsets at 2 bytes (65,536 bits), including
//     the intersect of two ranges of different masks;
//   * setNZMask (every mask at 1 and 2 bytes) and widen, which round 11 did not check.
// Build: g++ -O2 -std=c++14 -I<ghidra-decompiler>/cpp fwd12.cpp <the engine objects> -lz   (run.sh in this directory does it)
// Run:   ./fwd12 <section> <seed> <cases>      sections: fwd unary shift mult add sub sets nz widen all
#include "libdecomp.hh"
#include "rangeutil.hh"
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
static long long SX(u64 v, int sz) { if (sz >= 8) return (long long)v; u64 s = 1ull << (8 * sz - 1); return (long long)((v ^ s) - s); }
static std::mt19937_64 rg;
static u64 R() { return rg(); }

struct Rg { u64 l, r; int st; };
static u64 cnt(const Rg &g, u64 m) { return g.l == g.r ? m / g.st + 1 : ((g.r - g.l) & m) / g.st; }
static u64 elem(const Rg &g, u64 m, u64 j) { return (g.l + j * (u64)g.st) & m; }
static bool member(u64 v, u64 l, u64 r, int st, u64 m) {   // contains() written out
  v &= m;
  if (v % st != l % st) return false;
  if (l == r) return true;
  return l < r ? (v >= l && v < r) : (v < r || v >= l);
}
static bool member(u64 v, const Rg &g, u64 m) { return member(v, g.l, g.r, g.st, m); }
static std::string T(const Rg &g) { char b[96]; snprintf(b, sizeof b, "[%llx,%llx)/%d", g.l, g.r, g.st); return b; }
static std::string T(const CircleRange &q) {
  char b[128]; snprintf(b, sizeof b, "[%llx,%llx)/%d m%llx%s", (u64)q.getMin(), (u64)q.getEnd(), q.getStep(), (u64)q.getMask(), q.isEmpty() ? " empty" : "");
  return b;
}

// ---- structured values and ranges
static u64 pickVal(int sz) {
  const u64 m = M(sz);
  switch (R() % 8) {
    case 0: return R() & m;
    case 1: return (R() % 17) & m;
    case 2: return (m - R() % 17) & m;
    case 3: return ((m >> 1) + (R() % 17) - 8) & m;
    case 4: { int k = R() % (8 * sz); return ((1ull << k) + (R() % 9) - 4) & m; }
    case 5: { int k = R() % (8 * sz); return ((0 - (1ull << k)) + (R() % 9) - 4) & m; }
    case 6: return (R() & R() & R()) & m;
    default: return (R() | R() | R()) & m;
  }
}
static Rg genRange(int sz) {
  const u64 m = M(sz);
  static const int steps[] = {1, 1, 1, 1, 2, 2, 4, 8, 16, 32};
  int st = steps[R() % 10];
  while ((u64)st > m) st >>= 1;
  Rg g; g.st = st;
  g.l = pickVal(sz);
  switch (R() % 12) {
    case 0: g.r = g.l; break;                                  // full
    case 1: g.r = (g.l + st) & m; break;                       // single
    case 2: g.r = (g.l + 2 * st) & m; break;
    case 3: g.r = (g.l - st) & m; break;                       // all but one
    default: g.r = pickVal(sz);
  }
  const u64 rem = g.l % st;
  g.r = (g.r - g.r % st + rem) & m;
  if (g.r % st != rem) g.r = g.l;
  if (g.l == g.r) { g.l = g.r = rem; }                         // the normal form of a full range
  return g;
}
static void sample(const Rg &g, int sz, std::vector<u64> &out, const std::vector<u64> &extra = std::vector<u64>()) {
  const u64 m = M(sz);
  out.clear();
  const u64 n = cnt(g, m);
  if (n <= 64) { for (u64 j = 0; j < n; ++j) out.push_back(elem(g, m, j)); }
  else {
    for (u64 j = 0; j < 8; ++j) { out.push_back(elem(g, m, j)); out.push_back(elem(g, m, n - 1 - j)); }
    for (int k = 0; k < 40; ++k) out.push_back(elem(g, m, R() % n));
  }
  std::vector<u64> targets = {0, m, m >> 1, (m >> 1) + 1};
  for (u64 t : extra) targets.push_back(t & m);
  for (u64 t : targets) for (int d = -5; d <= 5; ++d) { u64 v = (t + d) & m; if (member(v, g, m)) out.push_back(v); }
}

// ---- results
struct Tally { long n = 0, refused = 0, bad = 0; std::string ex[3]; };
static std::map<std::string, Tally> G;
static bool wellFormed(const CircleRange &q, int outSz, std::string &why) {
  if (q.isEmpty()) { why = "empty"; return false; }
  if (q.getMask() != M(outSz)) { why = "mask"; return false; }
  int st = q.getStep();
  if (st < 1 || (st & (st - 1))) { why = "step not a power of two"; return false; }
  if ((q.getMin() & ~q.getMask()) || (q.getEnd() & ~q.getMask())) { why = "bound outside mask"; return false; }
  if (q.getMin() % st != q.getEnd() % st) { why = "bounds differ mod step"; return false; }
  return true;
}
static void judge(const std::string &key, const std::string &desc, bool ok, const CircleRange &q, int outSz, const std::vector<u64> &images) {
  Tally &t = G[key];
  t.n++;
  if (!ok) { t.refused++; return; }
  std::string why;
  const u64 m = M(outSz);
  bool bad = false;
  std::string what;
  if (!wellFormed(q, outSz, why)) { bad = true; what = "malformed: " + why; }
  else {
    for (u64 y : images) {
      const bool mine = member(y, q.getMin(), q.getEnd(), q.getStep(), m), eng = q.contains(y);
      if (!mine || !eng) { bad = true; char b[96]; snprintf(b, sizeof b, "misses %llx (definition %d, contains() %d)", y, (int)mine, (int)eng); what = b; break; }
    }
  }
  if (bad) {
    t.bad++;
    if (t.bad <= 3) t.ex[t.bad - 1] = desc + " -> " + T(q) + " " + what;
  }
}

// ---- section: push-forward
static const u64 AMOUNTS[] = {0, 1, 2, 3, 4, 5, 7, 8, 9, 15, 16, 17, 30, 31, 32, 33, 47, 48, 55, 56, 57, 62, 63, 64, 65, 127, 128, 129, 255, 256, 0x7fffffffull, 0x80000000ull, 0xffffffffull, 0x100000000ull, 0x100000001ull, 0x8000000000000000ull, ~0ull};
static void shifts(int sz, long iters) {
  const u64 m = M(sz);
  std::vector<u64> el, im;
  for (long it = 0; it < iters; ++it) {
    Rg a = genRange(sz);
    CircleRange A(a.l, a.r, sz, a.st);
    u64 sa = (R() % 3) ? AMOUNTS[R() % (sizeof AMOUNTS / sizeof AMOUNTS[0])] : R() % (8 * sz + 4);
    int asz = sa <= 0xff ? ((R() % 2) ? 1 : 4) : (sa <= 0xffffffffull ? 4 : 8);
    if (R() % 5 == 0 && asz == 4) asz = 8;
    CircleRange C(sa, asz);
    int maxStep = 1 << (R() % 6);
    // the points where the product x << sa crosses a multiple of the domain
    std::vector<u64> extra;
    if (sa < 64 && sa < (u64)(8 * sz)) for (int j = 1; j <= 3; ++j) for (int d = -1; d <= 1; ++d) extra.push_back((((u64)j << (8 * sz - sa)) + d) & m);
    sample(a, sz, el, extra);
    for (OpCode opc : {CPUI_INT_LEFT, CPUI_INT_RIGHT, CPUI_INT_SRIGHT}) {
      im.clear();
      for (u64 x : el) {
        u64 y;
        if (opc == CPUI_INT_LEFT) y = sa >= (u64)(8 * sz) ? 0 : (x << sa) & m;
        else if (opc == CPUI_INT_RIGHT) y = sa >= (u64)(8 * sz) ? 0 : x >> sa;
        else y = (u64)(SX(x, sz) >> (sa >= (u64)(8 * sz) ? 8 * sz - 1 : sa)) & m;
        im.push_back(y);
      }
      CircleRange q;
      char k[48]; snprintf(k, sizeof k, "shift op%d size %d", (int)opc, sz);
      char d[160]; snprintf(d, sizeof d, "%s by %llx (%d-byte const) maxStep %d", T(a).c_str(), sa, asz, maxStep);
      judge(k, d, q.pushForwardBinary(opc, A, C, sz, sz, maxStep), q, sz, im);
    }
  }
}
static void mults(int sz, long iters) {
  const u64 m = M(sz);
  std::vector<u64> el, im;
  for (long it = 0; it < iters; ++it) {
    Rg a = genRange(sz);
    CircleRange A(a.l, a.r, sz, a.st);
    u64 c;
    switch (R() % 6) {
      case 0: c = R() % 20; break;
      case 1: c = (0 - R() % 20); break;
      case 2: c = 1ull << (R() % (8 * sz)); break;
      case 3: c = (1ull << (R() % (8 * sz))) + (R() % 5) - 2; break;
      case 4: c = pickVal(sz); break;
      default: c = R();
    }
    c &= m;
    std::vector<u64> extra;
    // x*c crosses a multiple of 2^(8 sz) near x = j * 2^(8 sz) / c, for the magnitude of c
    u64 mag = (c & (m ^ (m >> 1))) ? ((0 - c) & m) : c;
    if (mag > 1) for (int j = 1; j <= 4; ++j) {
      long double x = (long double)j * (long double)((m == ~0ull) ? 18446744073709551616.0L : (long double)(m + 1)) / (long double)mag;
      for (int d = -1; d <= 1; ++d) { u64 xv = (u64)x; extra.push_back((xv + d) & m); extra.push_back((0 - xv - d) & m); }
    }
    sample(a, sz, el, extra);
    im.clear();
    for (u64 x : el) im.push_back((x * c) & m);
    CircleRange C(c, sz);
    int maxStep = 1 << (R() % 6);
    CircleRange q, q2;
    char k[48]; snprintf(k, sizeof k, "INT_MULT size %d", sz);
    char d[160]; snprintf(d, sizeof d, "%s * %llx maxStep %d", T(a).c_str(), c, maxStep);
    judge(k, d, q.pushForwardBinary(CPUI_INT_MULT, A, C, sz, sz, maxStep), q, sz, im);
    snprintf(k, sizeof k, "INT_MULT first size %d", sz);
    judge(k, d, q2.pushForwardBinary(CPUI_INT_MULT, C, A, sz, sz, maxStep), q2, sz, im);
  }
}
static void adds(int sz, long iters) {
  const u64 m = M(sz);
  std::vector<u64> el, el2, im;
  for (long it = 0; it < iters; ++it) {
    Rg a = genRange(sz), b = genRange(sz);
    if (R() % 6 == 0) { b.l = pickVal(sz) ; b.st = 1; b.r = (b.l + 1) & m; }   // a constant
    CircleRange A(a.l, a.r, sz, a.st), B(b.l, b.r, sz, b.st);
    sample(a, sz, el); sample(b, sz, el2);
    im.clear();
    for (u64 x : el) for (size_t i = 0; i < el2.size(); i += 1 + (el2.size() > 40 ? 3 : 0)) im.push_back((x + el2[i]) & m);
    for (OpCode opc : {CPUI_INT_ADD, CPUI_PTRSUB}) {
      CircleRange q;
      char k[48]; snprintf(k, sizeof k, "INT_ADD size %d op%d", sz, (int)opc);
      judge(k, T(a) + " + " + T(b), q.pushForwardBinary(opc, A, B, sz, sz, 32), q, sz, im);
    }
  }
}
static void unaries(int sz, long iters) {
  const u64 m = M(sz);
  std::vector<u64> el, im;
  for (long it = 0; it < iters; ++it) {
    Rg a = genRange(sz);
    CircleRange A(a.l, a.r, sz, a.st);
    sample(a, sz, el);
    for (OpCode opc : {CPUI_COPY, CPUI_CAST, CPUI_INT_2COMP, CPUI_INT_NEGATE}) {
      im.clear();
      for (u64 x : el) im.push_back(opc == CPUI_INT_2COMP ? ((0 - x) & m) : opc == CPUI_INT_NEGATE ? ((~x) & m) : x);
      CircleRange q;
      char k[48]; snprintf(k, sizeof k, "unary op%d size %d", (int)opc, sz);
      judge(k, T(a), q.pushForwardUnary(opc, A, sz, sz), q, sz, im);
    }
    for (int out = sz + 1; out <= 8; ++out) for (OpCode opc : {CPUI_INT_ZEXT, CPUI_INT_SEXT}) {
      im.clear();
      for (u64 x : el) im.push_back(opc == CPUI_INT_ZEXT ? x : ((u64)SX(x, sz) & M(out)));
      CircleRange q;
      char k[48]; snprintf(k, sizeof k, "ext op%d %d to %d", (int)opc, sz, out);
      judge(k, T(a), q.pushForwardUnary(opc, A, sz, out), q, out, im);
    }
  }
}
static void subpieces(int in, long iters) {
  const u64 m = M(in);
  std::vector<u64> el, im;
  for (long it = 0; it < iters; ++it) {
    Rg a = genRange(in);
    CircleRange A(a.l, a.r, in, a.st);
    sample(a, in, el);
    for (int out = 1; out < in; ++out) for (int off = 0; off + out <= in; ++off) {
      im.clear();
      for (u64 x : el) im.push_back((x >> (8 * off)) & M(out));
      CircleRange q;
      char k[64]; snprintf(k, sizeof k, "SUBPIECE %d to %d at %d", in, out, off);
      judge(k, T(a), q.pushForwardBinary(CPUI_SUBPIECE, A, CircleRange((u64)off, 4), in, out, 32), q, out, im);
    }
  }
  (void)m;
}

// ---- section: set operations against bitsets (2 bytes: 65,536 bits)
typedef std::vector<u64> Bits;
static const size_t W16 = 1024;
static void setbit(Bits &b, u64 v) { b[v >> 6] |= 1ull << (v & 63); }
static bool getbit(const Bits &b, u64 v) { return (b[v >> 6] >> (v & 63)) & 1; }
static Bits bitsOfDef(u64 l, u64 r, int st, u64 m, bool empty = false) {   // by the definition of the set; the domain is [0,m]
  Bits b(W16, 0);
  if (empty) return b;
  for (u64 v = l % st; v <= m; v += st) if (member(v, l, r, st, m)) setbit(b, v);
  return b;
}
static Bits bitsOfRange(const CircleRange &q) { return bitsOfDef(q.getMin(), q.getEnd(), q.getStep(), q.getMask(), q.isEmpty()); }
// the elements by walking getNext, as the engine's consumers do (a check of getNext against the definition)
static bool walkMatches(const CircleRange &q, const Bits &def) {
  if (q.isEmpty()) return true;
  Bits w(W16, 0);
  u64 v = q.getMin();
  for (long g = 0; g < 70000; ++g) { setbit(w, v); if (!q.getNext(v)) break; }
  return w == def;
}
static u64 popcnt(const Bits &b) { u64 n = 0; for (u64 w : b) n += __builtin_popcountll(w); return n; }

static std::string setsKey(const char *s, const char *what) { return std::string(s) + " " + what; }
static void note(const std::string &key, const std::string &desc, bool bad) {
  Tally &t = G[key];
  t.n++;
  if (bad) { t.bad++; if (t.bad <= 3) t.ex[t.bad - 1] = desc; }
}
static void sets16(long iters) {
  const int sz = 2; const u64 m = M(sz);
  for (long it = 0; it < iters; ++it) {
    Rg a = genRange(sz), b = genRange(sz);
    if (R() % 5 == 0) { b.st = a.st; b.l = pickVal(sz); b.r = b.l; b.l %= b.st; b.r = b.l; if (R() % 2) { b.r = (b.l + a.st * (1 + R() % 4)) & m; } if (b.r % b.st != b.l % b.st) b.r = b.l; }
    CircleRange A(a.l, a.r, sz, a.st), B(b.l, b.r, sz, b.st);
    const Bits ba = bitsOfDef(a.l, a.r, a.st, m), bb = bitsOfDef(b.l, b.r, b.st, m);
    const std::string both = T(a) + " , " + T(b);
    // the engine's reading of A and B agrees with the definition (getNext, getSize, contains)
    note("getNext walk", T(a), !walkMatches(A, ba));
    note("getSize", T(a) + " is " + std::to_string(A.getSize()) + " of " + std::to_string(popcnt(ba)),
         A.getSize() != popcnt(ba) && !(popcnt(ba) == m / a.st + 1 && a.l == a.r));   // a full range: "lies by one", documented
    {
      bool bad = false; u64 at = 0;
      for (int k = 0; k < 40 && !bad; ++k) { u64 v = R() & m; if (k < 20) v = (a.l + (k - 10)) & m; if (A.contains(v) != getbit(ba, v)) { bad = true; at = v; } }
      note("contains(value)", T(a) + " at " + std::to_string(at), bad);
    }
    // intersect
    {
      CircleRange q = A; int rc = q.intersect(B);
      if (rc == 0) {
        Bits want(W16); for (size_t i = 0; i < W16; ++i) want[i] = ba[i] & bb[i];
        Bits got = bitsOfRange(q);
        std::string why;
        bool bad = got != want;
        if (!bad && !q.isEmpty() && !wellFormed(q, sz, why)) bad = true;
        note("intersect==0 exact", both + " -> " + T(q) + " " + why, bad);
      } else {
        note("intersect!=0 leaves this", both, !(q == A));
        note("intersect=2 only for a true two-piece or large step", both, false);
      }
    }
    // circleUnion
    {
      CircleRange q = A; int rc = q.circleUnion(B);
      if (rc == 0) {
        Bits want(W16); for (size_t i = 0; i < W16; ++i) want[i] = ba[i] | bb[i];
        Bits got = bitsOfRange(q);
        std::string why;
        bool bad = got != want;
        if (!bad && !wellFormed(q, sz, why)) bad = true;
        note("circleUnion==0 exact", both + " -> " + T(q) + " " + why, bad);
      } else note("circleUnion!=0 leaves this", both, !(q == A));
    }
    // minimalContainer
    {
      CircleRange q = A; bool every = q.minimalContainer(B, 1 << (R() % 6));
      Bits got = bitsOfRange(q);
      bool bad = false;
      for (size_t i = 0; i < W16; ++i) if (((ba[i] | bb[i]) & ~got[i])) bad = true;
      std::string why;
      if (!wellFormed(q, sz, why)) bad = true;
      note("minimalContainer holds both", both + " -> " + T(q) + " " + why, bad);
      if (every) { bool full = popcnt(got) == m + 1; note("minimalContainer 'everything' is everything", both + " -> " + T(q), !full); }
    }
    // contains(range)
    if (A.contains(B)) {
      bool bad = false; for (size_t i = 0; i < W16; ++i) if (bb[i] & ~ba[i]) bad = true;
      note("contains(range) claimed true", both, bad);
    } else {
      // not claimed: may be a refusal; count how often a true containment is refused
      bool truly = true; for (size_t i = 0; i < W16; ++i) if (bb[i] & ~ba[i]) truly = false;
      note("contains(range) refused although true (info)", both, false);
      if (truly) G["contains(range) refused although true (info)"].refused++;
    }
    // invert: the engine's reading is the complement of the interval at step 1 (documented in widening); check it is that
    {
      CircleRange q = A; int rc = q.invert();
      note("invert returns the old step", T(a), rc != a.st);
      if (a.st == 1) {
        Bits want(W16); for (size_t i = 0; i < W16; ++i) want[i] = ~ba[i];
        Bits got = bitsOfRange(q);
        bool bad = got != want;
        if (a.l == a.r) bad = !q.isEmpty();   // the complement of everything
        note("invert (step 1) is the complement", T(a) + " -> " + T(q), bad);
      } else {
        // the complement of a strided set keeps no more than the interval [r,l): a subset of the complement
        Bits got = bitsOfRange(q);
        bool bad = false; for (size_t i = 0; i < W16; ++i) if (got[i] & ba[i]) bad = true;
        if (a.l == a.r) bad = !q.isEmpty() ? !bad : bad;
        note("invert (strided) is inside the complement", T(a) + " -> " + T(q), bad);
      }
    }
  }
}
// intersect of a range of one mask with a range of another (JumpBasic::calcRange meets guards of another size): the set is the
// intersection of the two element sets restricted to the smaller domain
static void intersectMixed(long iters) {
  for (long it = 0; it < iters; ++it) {
    const int sa = 1 + R() % 2, sb = 1 + R() % 2;
    if (sa == sb) continue;
    Rg a = genRange(sa), b = genRange(sb);
    CircleRange A(a.l, a.r, sa, a.st), B(b.l, b.r, sb, b.st);
    const u64 ma = M(sa), mb = M(sb), mn = ma & mb;
    CircleRange q = A; int rc = q.intersect(B);
    if (rc != 0) { note("intersect mixed masks != 0 leaves this", T(a) + " , " + T(b), !(q == A)); continue; }
    bool over = false, under = false; u64 atOver = 0, atUnder = 0;
    for (u64 v = 0; v <= mn; ++v) {
      const bool want = member(v, a, ma) && member(v, b, mb);
      const bool got = !q.isEmpty() && member(v, q.getMin(), q.getEnd(), q.getStep(), q.getMask());
      if (want && !got && !under) { under = true; atUnder = v; }
      if (!want && got && !over) { over = true; atOver = v; }
    }
    char buf[96]; snprintf(buf, sizeof buf, " at %llx, mask of result %llx", over ? atOver : atUnder, (u64)q.getMask());
    const std::string d = T(a) + "(" + std::to_string(sa) + ") , " + T(b) + "(" + std::to_string(sb) + ") -> " + T(q) + buf;
    note("intersect mixed masks: values MISSING", d, under);
    note("intersect mixed masks: values extra (info)", d, over);
  }
}

// ---- setNZMask: every non-zero mask of a size yields a range that holds every value under the mask
static void nzmask(int sz, long iters, bool exhaustive) {
  const u64 m = M(sz);
  const u64 total = exhaustive ? m + 1 : (u64)iters;
  for (u64 i = 0; i < total; ++i) {
    u64 nz = exhaustive ? i : (pickVal(sz) & m);
    CircleRange q;
    if (!q.setNZMask(nz, sz)) continue;
    std::string key = "setNZMask size " + std::to_string(sz);
    bool bad = false; std::string why;
    if (!wellFormed(q, sz, why)) { bad = true; }
    else {
      // every v with (v & ~nz) == 0 must be in the range; test submasks: nz itself, 0, the lowest bit, random submasks
      std::vector<u64> vs = {0, nz, nz & (0 - nz)};
      for (int k = 0; k < 24; ++k) vs.push_back(R() & nz);
      if (sz <= 2 && exhaustive) { vs.clear(); for (u64 s = nz;; s = (s - 1) & nz) { vs.push_back(s); if (s == 0) break; if (vs.size() > 4096) break; } }
      for (u64 v : vs) if (!q.contains(v) || !member(v, q.getMin(), q.getEnd(), q.getStep(), m)) { bad = true; char b[64]; snprintf(b, sizeof b, " misses %llx", v); why += b; break; }
    }
    note(key, "nz " + std::to_string(nz) + " -> " + T(q) + " " + why, bad);
  }
}

// ---- widen: the range grows to the containing range at the unstable end and still holds what it held
static void widens(long iters) {
  for (long it = 0; it < iters; ++it) {
    const int sz = 1;
    const u64 m = M(sz);
    Rg b = genRange(sz);
    if (b.l == b.r && b.st == 1) continue;
    // A: a sub-range of B with the same or a bigger step: take A's elements as a subset of B's
    Rg a = genRange(sz);
    CircleRange A(a.l, a.r, sz, a.st), B(b.l, b.r, sz, b.st);
    if (!B.contains(A)) continue;
    for (int stable = 0; stable < 2; ++stable) {
      CircleRange q = A;
      q.widen(B, stable);
      Bits ba = bitsOfDef(a.l, a.r, a.st, m), bq = bitsOfRange(q);
      bool bad = false; for (size_t i = 0; i < W16; ++i) if (ba[i] & ~bq[i]) bad = true;
      std::string why;
      if (!wellFormed(q, sz, why)) bad = true;
      note(stable ? "widen (left stable) holds the old set" : "widen (right stable) holds the old set", T(a) + " in " + T(b) + " -> " + T(q) + " " + why, bad);
    }
  }
}

int main(int argc, char **argv) {
  std::string sec = argc > 1 ? argv[1] : "all";
  u64 seed = argc > 2 ? strtoull(argv[2], 0, 0) : 1;
  long n = argc > 3 ? atol(argv[3]) : 20000;
  rg.seed(seed);
  auto want = [&](const char *s) { return sec == "all" || sec == s; };
  if (want("unary") || want("fwd")) for (int sz = 1; sz <= 8; ++sz) unaries(sz, n);
  if (want("shift") || want("fwd")) for (int sz = 1; sz <= 8; ++sz) shifts(sz, n);
  if (want("mult") || want("fwd")) for (int sz = 1; sz <= 8; ++sz) mults(sz, n);
  if (want("add") || want("fwd")) for (int sz = 1; sz <= 8; ++sz) adds(sz, n);
  if (want("sub") || want("fwd")) for (int in = 2; in <= 8; ++in) subpieces(in, n);
  if (want("sets")) { sets16(n); intersectMixed(n); }
  if (want("nz")) { nzmask(1, 0, true); nzmask(2, 0, true); nzmask(4, n, false); nzmask(8, n, false); }
  if (want("widen")) widens(n * 10);
  long bad = 0;
  for (auto &kv : G) {
    const Tally &t = kv.second;
    printf("%-58s n %-9ld refused %-8ld BAD %ld\n", kv.first.c_str(), t.n, t.refused, t.bad);
    for (int i = 0; i < 3; ++i) if (!t.ex[i].empty()) printf("    %s\n", t.ex[i].c_str());
    bad += t.bad;
  }
  printf("TOTAL bad %ld (section %s seed %llu cases %ld)\n", bad, sec.c_str(), seed, n);
  return bad ? 1 : 0;
}
