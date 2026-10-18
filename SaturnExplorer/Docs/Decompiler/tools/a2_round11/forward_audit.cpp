// Brute-force audit of the forward half of CircleRange at 1 and 2 byte sizes (area 2 round 11 review).
// pushForwardUnary / pushForwardBinary: a result that is claimed (returns true) must contain every value the operation can produce
// from the elements of the input ranges (soundness: an under-approximation prints a wrong switch size or hides a stack alias), and be a
// well-formed range (left, right and step consistent, mask the output's). intersect / circleUnion / invert / minimalContainer /
// setNZMask / translate2Op / contains / getSize / widen against the set they claim to compute.
// Build: g++ -O1 -std=c++14 -I<ghidra-decompiler>/cpp forward_audit.cpp <the engine objects> -lz ; run: ./a.out [seed] [random cases per row]
// FWDUMP=<substring of the row key> lists the failing cases.
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

static uintb M(int sz) { return sz >= 8 ? ~(uintb)0 : (((uintb)1 << (8 * sz)) - 1); }
static int64_t SX(uintb v, int sz) { int b = 8 * sz; uintb s = (uintb)1 << (b - 1); return (int64_t)((v ^ s) - s); }

typedef std::vector<char> Set;
// the elements of the range as a bit per value (the semantics of contains()/getNext, written out independently)
static Set elements(uintb l, uintb r, int sz, int step, bool empty = false) {
  uintb n = M(sz) + 1;
  Set s(n, 0);
  if (empty) return s;
  uintb m = M(sz);
  uintb rem = l % step;
  if (l == r) { for (uintb v = 0; v < n; v++) if (v % step == rem) s[v] = 1; return s; }
  uintb v = l;
  for (;;) { s[v] = 1; v = (v + step) & m; if (v == r) break; }
  return s;
}
static Set elementsOf(const CircleRange &c, int sz) {
  uintb n = M(sz) + 1;
  Set s(n, 0);
  if (c.isEmpty()) return s;
  uintb m = c.getMask();
  // walk with getNext from the left boundary, as the engine's consumers do
  uintb v = c.getMin();
  uintb guard = 0;
  for (;;) { if (v <= M(sz)) s[v] = 1; if (!c.getNext(v)) break; if (++guard > (uintb)1 << 20) break; }
  (void)m;
  return s;
}
// consistent: left and right agree modulo the step, both inside the mask
static bool wellFormed(const CircleRange &c, std::string &why) {
  if (c.isEmpty()) return true;
  if (c.getStep() < 1 || (c.getStep() & (c.getStep() - 1))) { why = "step not a power of two"; return false; }
  if ((c.getMin() & ~c.getMask()) || (c.getEnd() & ~c.getMask())) { why = "boundary outside the mask"; return false; }
  if (c.getMin() % c.getStep() != c.getEnd() % c.getStep()) { why = "left and right differ modulo step"; return false; }
  return true;
}

struct Cls { long n = 0, none = 0, exact = 0, over = 0, under = 0, malformed = 0, wrongmask = 0; std::string ex[4]; };
static std::map<std::string, Cls> G;
static const char *dump = nullptr;

static void judge(const std::string &key, const std::string &desc, bool ok, const CircleRange &q, int outSz, const Set &truth) {
  Cls &c = G[key];
  c.n++;
  if (!ok) { c.none++; return; }
  std::string why;
  bool wf = wellFormed(q, why);
  bool maskBad = !q.isEmpty() && q.getMask() != M(outSz);
  Set got = elementsOf(q, outSz);
  long over = 0, under = 0;
  for (size_t v = 0; v < truth.size(); v++) { if (got[v] && !truth[v]) over++; if (!got[v] && truth[v]) under++; }
  char buf[320];
  auto rec = [&](int k, const char *what) {
    if (c.ex[k].empty()) {
      std::snprintf(buf, sizeof buf, "%s: %s -> [%llx,%llx) mask %llx step %d empty %d (over %ld under %ld)%s%s", what, desc.c_str(), (unsigned long long)q.getMin(), (unsigned long long)q.getEnd(), (unsigned long long)q.getMask(), q.getStep(), (int)q.isEmpty(), over, under, wf ? "" : " ", wf ? "" : why.c_str());
      c.ex[k] = buf;
    }
    if (dump && key.find(dump) != std::string::npos) { static int cnt = 0; if (cnt++ < 80) std::printf("DUMP %s %s: %s -> [%llx,%llx) m %llx st %d e %d over %ld under %ld\n", key.c_str(), what, desc.c_str(), (unsigned long long)q.getMin(), (unsigned long long)q.getEnd(), (unsigned long long)q.getMask(), q.getStep(), (int)q.isEmpty(), over, under); }
  };
  bool bad = false;
  if (!wf) { c.malformed++; rec(2, "malformed"); bad = true; }
  if (maskBad) { c.wrongmask++; rec(3, "mask"); bad = true; }
  if (under) { c.under++; rec(0, "UNDER"); bad = true; }
  if (!bad) { if (over) { c.over++; rec(1, "over"); } else c.exact++; }
}

static std::string D(uintb l, uintb r, int st) { char b[64]; std::snprintf(b, sizeof b, "[%llx,%llx)/%d", (unsigned long long)l, (unsigned long long)r, st); return b; }

static std::mt19937_64 rng;
static std::vector<uintb> edge(int sz) {
  uintb ms = M(sz);
  std::vector<uintb> v = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 15, 16, 17, 31, 32, 33, 63, 64, 65, 0x7e, 0x7f, 0x80, 0x81, 0xbf, 0xc0, 0xfe, 0xff, ms - 1, ms, (ms >> 1), (ms >> 1) + 1, (ms >> 1) + 2, (ms >> 1) - 1};
  if (sz == 2) for (uintb x : {0x100, 0x101, 0xff00, 0x7f00, 0x8000, 0x7fff, 0x3fff, 0x4000, 0xc000, 0xff, 0x1ff, 0xfeff, 0xff01}) v.push_back(x);
  for (auto &x : v) x &= ms;
  return v;
}
// a valid random range of size sz: step 1 (60%), 2, 4 or 8; empty and the "full" forms included
struct R { uintb l, r; int st; bool empty; };
static R randRange(int sz, const std::vector<uintb> &ed) {
  uintb ms = M(sz);
  R x; x.empty = false; x.st = 1;
  int k = rng() % 100;
  if (k >= 60) x.st = 1 << (1 + rng() % 3);
  auto pick = [&]() { return (rng() % 3) ? ed[rng() % ed.size()] : (rng() & ms); };
  x.l = pick(); x.r = (rng() % 8 == 0) ? x.l : pick();
  if (rng() % 10 == 0) x.r = (x.l + x.st) & ms;       // a single value
  if (x.st > 1) { uintb rem = rng() % x.st; x.l = (x.l - x.l % x.st + rem) & ms; x.r = (x.r - x.r % x.st + rem) & ms; if (x.l >= (uintb)x.st && (x.l % x.st) != rem) x.l = rem; }
  if (x.l % x.st != x.r % x.st) x.r = x.l;
  if (x.l == x.r && x.st > 1) { x.l %= x.st; x.r = x.l; }   // the normalized full set
  if (x.l == x.r && x.st == 1) { x.l = 0; x.r = 0; }
  return x;
}

int main(int argc, char **argv) {
  uint64_t seed = argc > 1 ? strtoull(argv[1], 0, 0) : 1;
  long rnd = argc > 2 ? atol(argv[2]) : 20000;
  dump = getenv("FWDUMP");
  rng.seed(seed);
  // ---------------- push-forward, unary ----------------
  struct U { OpCode opc; const char *name; int in, out; };
  std::vector<U> uns = {{CPUI_COPY,"COPY",1,1},{CPUI_COPY,"COPY",2,2},{CPUI_INT_2COMP,"2COMP",1,1},{CPUI_INT_2COMP,"2COMP",2,2},{CPUI_INT_NEGATE,"NEGATE",1,1},{CPUI_INT_NEGATE,"NEGATE",2,2},
    {CPUI_INT_ZEXT,"ZEXT",1,2},{CPUI_INT_SEXT,"SEXT",1,2}};   // 2 -> 4 would need 2^32 bits per set
  for (const U &u : uns) {
    std::vector<uintb> ed = edge(u.in);
    for (long it = 0; it < rnd; it++) {
      R a = randRange(u.in, ed);
      CircleRange A(a.l, a.r, u.in, a.st);
      Set sa = elements(a.l, a.r, u.in, a.st);
      uintb no = M(u.out) + 1;
      if (no > (1u << 24)) continue;
      Set truth(no, 0);
      for (uintb x = 0; x < sa.size(); x++) if (sa[x]) {
        uintb o = 0;
        switch (u.opc) {
          case CPUI_COPY: o = x; break;
          case CPUI_INT_2COMP: o = (-x) & M(u.out); break;
          case CPUI_INT_NEGATE: o = (~x) & M(u.out); break;
          case CPUI_INT_ZEXT: o = x; break;
          case CPUI_INT_SEXT: o = (uintb)SX(x, u.in) & M(u.out); break;
          default: break;
        }
        truth[o] = 1;
      }
      CircleRange Q;
      bool ok = Q.pushForwardUnary(u.opc, A, u.in, u.out);
      char key[64]; std::snprintf(key, sizeof key, "fwd %s in%d out%d", u.name, u.in, u.out);
      judge(key, D(a.l, a.r, a.st), ok, Q, u.out, truth);
    }
  }
  // ---------------- push-forward, binary ----------------
  struct B { OpCode opc; const char *name; };
  std::vector<B> bins = {{CPUI_INT_ADD,"ADD"},{CPUI_INT_MULT,"MULT"},{CPUI_INT_LEFT,"LEFT"},{CPUI_INT_RIGHT,"RIGHT"},{CPUI_INT_SRIGHT,"SRIGHT"},{CPUI_SUBPIECE,"SUBPIECE"}};
  for (int in = 1; in <= 2; in++) {
    std::vector<uintb> ed = edge(in);
    uintb ms = M(in);
    for (const B &b : bins) {
      int outs[3] = {in, 0, 0}; int nouts = 1;
      if (b.opc == CPUI_SUBPIECE) { nouts = 0; for (int o = 1; o < in; o++) outs[nouts++] = o; if (!nouts) continue; }
      for (int oi = 0; oi < nouts; oi++) {
        int out = outs[oi];
        for (long it = 0; it < rnd * (b.opc == CPUI_INT_ADD ? 1 : 1); it++) {
          R a = randRange(in, ed), c;
          int maxStep = (rng() % 2) ? 32 : 1 << (rng() % 5);
          // second operand
          if (b.opc == CPUI_INT_ADD || b.opc == CPUI_INT_MULT) {
            c = randRange(in, ed);
            if (b.opc == CPUI_INT_MULT || rng() % 4 == 0) { c.l = (rng() % 2) ? ed[rng() % ed.size()] : (rng() & ms); c.st = 1; c.r = (c.l + 1) & ms; c.empty = false; }
            if (b.opc == CPUI_INT_MULT && rng() % 2) std::swap(a, c);
          } else {
            uintb sh = (rng() % 3) ? rng() % (8 * in + 2) : rng() % 70;
            if (b.opc == CPUI_SUBPIECE) sh = rng() % (in + 1);
            c.l = sh; c.st = 1; c.r = sh + 1; c.empty = false;
          }
          CircleRange A(a.l, a.r, in, a.st), C;
          // the second operand's size: shifts and subpiece offsets are 4-byte constants in p-code (size does not matter for the value), the others the same size
          int csz = (b.opc == CPUI_INT_ADD || b.opc == CPUI_INT_MULT) ? in : 4;
          C = CircleRange(c.l, c.r, csz, c.st);
          Set sa = elements(a.l, a.r, in, a.st);
          Set sc = csz == in ? elements(c.l, c.r, csz, c.st) : Set();
          uintb no = M(out) + 1;
          Set truth(no, 0);
          if (b.opc == CPUI_INT_ADD || b.opc == CPUI_INT_MULT) {
            std::vector<uintb> ea, ec;
            for (uintb x = 0; x < sa.size(); x++) if (sa[x]) ea.push_back(x);
            for (uintb x = 0; x < sc.size(); x++) if (sc[x]) ec.push_back(x);
            if (ea.size() * ec.size() > 4000000) continue;
            for (uintb x : ea) for (uintb y : ec) truth[((b.opc == CPUI_INT_ADD) ? x + y : x * y) & ms] = 1;
          } else {
            uintb sh = c.l;
            for (uintb x = 0; x < sa.size(); x++) if (sa[x]) {
              uintb o = 0;
              switch (b.opc) {
                case CPUI_INT_LEFT: o = sh >= 8 * in ? 0 : (x << sh) & ms; break;
                case CPUI_INT_RIGHT: o = sh >= 8 * in ? 0 : x >> sh; break;
                case CPUI_INT_SRIGHT: { uintb s2 = sh >= 8 * in ? 8 * in - 1 : sh; o = (uintb)(SX(x, in) >> s2) & ms; break; }
                case CPUI_SUBPIECE: o = (x >> (8 * sh)) & M(out); break;
                default: break;
              }
              truth[o] = 1;
            }
          }
          CircleRange Q;
          bool ok = Q.pushForwardBinary(b.opc, A, C, in, out, maxStep);
          char key[64]; std::snprintf(key, sizeof key, "fwd %s in%d out%d", b.name, in, out);
          char d[160]; std::snprintf(d, sizeof d, "%s op %s maxStep %d", D(a.l, a.r, a.st).c_str(), D(c.l, c.r, c.st).c_str(), maxStep);
          judge(key, d, ok, Q, out, truth);
        }
      }
    }
  }
  // ---------------- set operations ----------------
  for (int sz = 1; sz <= 2; sz++) {
    std::vector<uintb> ed = edge(sz);
    for (long it = 0; it < rnd * 2; it++) {
      R a = randRange(sz, ed), c = randRange(sz, ed);
      CircleRange A(a.l, a.r, sz, a.st), C(c.l, c.r, sz, c.st);
      Set sa = elements(a.l, a.r, sz, a.st), sc = elements(c.l, c.r, sz, c.st);
      size_t n = sa.size();
      std::string d = D(a.l, a.r, a.st) + " , " + D(c.l, c.r, c.st);
      char key[64];
      // intersect: code 0 => exactly the intersection (empty included); 2 => this unchanged and the intersection is not one range (claim checked below)
      {
        Set truth(n, 0); for (size_t v = 0; v < n; v++) truth[v] = sa[v] && sc[v];
        CircleRange Q = A; int rc = Q.intersect(C);
        std::snprintf(key, sizeof key, "set intersect sz%d %s", sz, (a.st == 1 && c.st == 1) ? "st1" : "stN");
        if (rc == 0) {
          Cls &cl = G[key]; cl.n++;
          Set got = elementsOf(Q, sz); long over = 0, under = 0;
          for (size_t v = 0; v < n; v++) { if (got[v] && !truth[v]) over++; if (!got[v] && truth[v]) under++; }
          std::string why; bool wf = wellFormed(Q, why);
          if (over || under || !wf) {
            if (over && under) cl.under++; else if (under) cl.under++; else cl.over++;
            int k = under ? 0 : 1;
            if (cl.ex[k].empty()) { char b2[300]; std::snprintf(b2, sizeof b2, "%s: %s -> [%llx,%llx) m %llx st %d e %d over %ld under %ld %s", under ? "UNDER" : "OVER", d.c_str(), (unsigned long long)Q.getMin(), (unsigned long long)Q.getEnd(), (unsigned long long)Q.getMask(), Q.getStep(), (int)Q.isEmpty(), over, under, wf ? "" : why.c_str()); cl.ex[k] = b2; }
            if (dump && std::string(key).find(dump) != std::string::npos) { static int cnt = 0; if (cnt++ < 80) std::printf("DUMP %s %s -> [%llx,%llx) st %d e %d over %ld under %ld\n", key, d.c_str(), (unsigned long long)Q.getMin(), (unsigned long long)Q.getEnd(), Q.getStep(), (int)Q.isEmpty(), over, under); }
          } else cl.exact++;
        } else {
          Cls &cl = G[std::string(key) + " code2"]; cl.n++;
          // code 2 claims two pieces: the unchanged range must equal A
          if (!(Q == A)) { cl.under++; if (cl.ex[0].empty()) cl.ex[0] = "code 2 but this modified: " + d; }
          // count how many "two pieces" are in fact one range (imprecision only)
          int runs = 0; for (size_t v = 0; v < n; v++) if (sa[v] && sc[v] && !(sa[(v + n - 1) % n] && sc[(v + n - 1) % n])) runs++;
          if (a.st == 1 && c.st == 1 && runs <= 1) { cl.over++; if (cl.ex[1].empty()) cl.ex[1] = "code 2 but one piece (precision): " + d; }
        }
      }
      // circleUnion
      {
        Set truth(n, 0); for (size_t v = 0; v < n; v++) truth[v] = sa[v] || sc[v];
        CircleRange Q = A; int rc = Q.circleUnion(C);
        std::snprintf(key, sizeof key, "set union sz%d %s", sz, (a.st == 1 && c.st == 1) ? "st1" : "stN");
        if (rc == 0) judge(key, d, true, Q, sz, truth);
        else {
          Cls &cl = G[std::string(key) + " code2"]; cl.n++;
          if (!(Q == A)) { cl.under++; if (cl.ex[0].empty()) cl.ex[0] = "code 2 but this modified: " + d; }
        }
      }
      // minimalContainer: a superset of both
      {
        CircleRange Q = A; Q.minimalContainer(C, 1 << (rng() % 6));
        Set got = elementsOf(Q, sz); long under = 0;
        for (size_t v = 0; v < n; v++) if ((sa[v] || sc[v]) && !got[v]) under++;
        Cls &cl = G[(a.st == 1 && c.st == 1) ? "set minimalContainer st1" : "set minimalContainer stN"]; cl.n++;
        if (under) { cl.under++; if (cl.ex[0].empty()) { char b2[300]; std::snprintf(b2, sizeof b2, "UNDER %s -> [%llx,%llx) m %llx st %d (%ld values missing)", d.c_str(), (unsigned long long)Q.getMin(), (unsigned long long)Q.getEnd(), (unsigned long long)Q.getMask(), Q.getStep(), under); cl.ex[0] = b2; } }
        else cl.exact++;
      }
      // invert: the complement of the interval at step 1
      {
        CircleRange Q = A; Q.invert();
        Set sa1 = elements(a.l, a.r, sz, 1); if (a.st > 1 && a.l == a.r) sa1 = Set(n, 1);
        // the stride set A' = A with step 1: [l,r) as an interval; complement of that
        Set truth(n, 0);
        if (a.l == a.r) { for (size_t v = 0; v < n; v++) truth[v] = 0; }   // full interval: empty complement
        else for (size_t v = 0; v < n; v++) truth[v] = !sa1[v];
        Set got = elementsOf(Q, sz);
        Cls &cl = G["set invert"]; cl.n++;
        long bad = 0; for (size_t v = 0; v < n; v++) if (got[v] != truth[v]) bad++;
        if (bad) { cl.under++; if (cl.ex[0].empty()) cl.ex[0] = "invert of " + D(a.l, a.r, a.st); }
        else cl.exact++;
        // the real complement of the stride set is NOT what invert gives for step > 1 (documented): count it
        if (a.st > 1) { Cls &c3 = G["set invert vs stride-set complement"]; c3.n++; bool same = true; for (size_t v = 0; v < n; v++) if (got[v] == sa[v] || 0) same = false; if (!same) c3.over++; }
      }
      // contains(range) claims
      {
        bool claim = A.contains(C);
        Cls &cl = G[(a.st == 1 && c.st == 1) ? "set contains(range) st1" : "set contains(range) stN"]; cl.n++;
        if (claim) { bool truly = true; for (size_t v = 0; v < n; v++) if (sc[v] && !sa[v]) truly = false; if (!truly) { cl.under++; if (cl.ex[0].empty()) cl.ex[0] = "claims " + D(a.l, a.r, a.st) + " contains " + D(c.l, c.r, c.st); } else cl.exact++; }
      }
      // contains(value), getSize, getMaxInfo
      {
        Cls &cl = G["set contains(value)"]; cl.n++;
        bool bad = false; for (uintb v = 0; v < n && !bad; v += (n > 256 ? 7 : 1)) if (A.contains(v) != (bool)sa[v]) { bad = true; if (cl.ex[0].empty()) cl.ex[0] = "contains(" + std::to_string(v) + ") of " + D(a.l, a.r, a.st); }
        if (bad) cl.under++; else cl.exact++;
        Cls &c2 = G["set getSize"]; c2.n++;
        long cnt = 0; for (size_t v = 0; v < n; v++) cnt += sa[v];
        if ((long)A.getSize() != cnt) { c2.under++; if (c2.ex[0].empty()) c2.ex[0] = "getSize " + std::to_string(A.getSize()) + " but " + std::to_string(cnt) + " in " + D(a.l, a.r, a.st); } else c2.exact++;
        // getMaxInfo: the greatest bit length of |signed value| over the elements must be <= getMaxInfo
        Cls &c3 = G["set getMaxInfo"]; c3.n++;
        int worst = 0; for (size_t v = 0; v < n; v++) if (sa[v]) { int64_t s = SX(v, sz); uint64_t m = s < 0 ? (uint64_t)(-s) : (uint64_t)s; int bl = 0; while (m) { bl++; m >>= 1; } if (bl > worst) worst = bl; }
        if (A.getMaxInfo() < worst) { c3.under++; if (c3.ex[0].empty()) { char b2[200]; std::snprintf(b2, sizeof b2, "getMaxInfo %d < %d for %s", A.getMaxInfo(), worst, D(a.l, a.r, a.st).c_str()); c3.ex[0] = b2; } } else c3.exact++;
      }
      // translate2Op: the comparison is true exactly on the range
      if (a.st == 1) {
        OpCode opc; uintb cc; int cs;
        int rc = A.translate2Op(opc, cc, cs);
        Cls &cl = G["set translate2Op"]; cl.n++;
        if (rc == 0) {
          bool bad = false;
          for (uintb v = 0; v < n && !bad; v++) {
            uintb x = v, y = cc; bool t = false; bool xs = cs == 1;
            uintb l1 = xs ? x : y, r1 = xs ? y : x;
            switch (opc) {
              case CPUI_INT_EQUAL: t = x == cc; break;
              case CPUI_INT_NOTEQUAL: t = x != cc; break;
              case CPUI_INT_LESS: t = l1 < r1; break;
              case CPUI_INT_SLESS: t = SX(l1, sz) < SX(r1, sz); break;
              default: t = !sa[v]; break;
            }
            if (t != (bool)sa[v]) { bad = true; if (cl.ex[0].empty()) { char b2[200]; std::snprintf(b2, sizeof b2, "%s opc %d c %llx slot %d at %llx", D(a.l, a.r, a.st).c_str(), (int)opc, (unsigned long long)cc, cs, (unsigned long long)v); cl.ex[0] = b2; } }
          }
          if (bad) cl.under++; else cl.exact++;
        } else if (rc == 1) { bool full = true; for (size_t v = 0; v < n; v++) if (!sa[v]) full = false; if (!full) { cl.under++; if (cl.ex[1].empty()) cl.ex[1] = "rc1 (all) but " + D(a.l, a.r, a.st); } else cl.exact++; }
        else if (rc == 3) { bool e = true; for (size_t v = 0; v < n; v++) if (sa[v]) e = false; if (!e) { cl.under++; if (cl.ex[1].empty()) cl.ex[1] = "rc3 (none) but " + D(a.l, a.r, a.st); } else cl.exact++; }
      }
    }
    // setNZMask: the range is exactly the values whose bits lie inside the mask
    for (long it = 0; it < rnd; it++) {
      uintb nz = rng() & M(sz); if (rng() % 3 == 0) nz = ((rng() & M(sz)) | 1) << (rng() % 4) & M(sz); if (rng() % 5 == 0) nz = M(sz) >> (rng() % (8 * sz));
      if (rng() % 7 == 0) nz = (M(sz) << (rng() % (8 * sz))) & M(sz);
      CircleRange Q; bool ok = Q.setNZMask(nz, sz);
      Cls &cl = G["set setNZMask"]; cl.n++;
      if (!ok) { cl.none++; continue; }
      Set got = elementsOf(Q, sz); long bad = 0, under = 0; for (uintb v = 0; v <= M(sz); v++) { bool t = (v & ~nz) == 0; if (got[v] != t) { bad++; if (t) under++; } }
      if (bad) { cl.under++; if (cl.ex[0].empty()) { char b2[200]; std::snprintf(b2, sizeof b2, "nzmask %llx size %d -> [%llx,%llx)/%d (wrong values %ld, missing %ld)", (unsigned long long)nz, sz, (unsigned long long)Q.getMin(), (unsigned long long)Q.getEnd(), Q.getStep(), bad, under); cl.ex[0] = b2; } } else cl.exact++;
    }
  }
  // report
  long bad = 0;
  std::printf("%-34s %9s %9s %8s %8s %8s %8s %8s\n", "row", "cases", "exact", "over", "UNDER", "malform", "badmask", "refused");
  for (auto &kv : G) {
    const Cls &c = kv.second;
    std::printf("%-34s %9ld %9ld %8ld %8ld %8ld %8ld %8ld\n", kv.first.c_str(), c.n, c.exact, c.over, c.under, c.malformed, c.wrongmask, c.none);
    bad += c.under + c.malformed + c.wrongmask;
  }
  for (auto &kv : G) for (int k = 0; k < 4; k++) if (!kv.second.ex[k].empty() && (k != 1 || kv.first.find("code2") != std::string::npos)) std::printf("  %s | %s\n", kv.first.c_str(), kv.second.ex[k].c_str());
  std::printf("unsound or malformed results: %ld\n", bad);
  return 0;
}
