// Sampled audit of CircleRange::pullBackUnary / pullBackBinary at 4 and 8 bytes (area 2 round 11; round 10's sweep was exhaustive at 1 and 2).
// A pull-back that succeeds must hold exactly the inputs whose result is in the range, in the input's mask. The truth is checked at
// the points where it can change: every input the range's edges, the constant and the sign boundaries map to or from, each +-3, and random ones.
// Build: g++ -O1 -std=c++14 -I<ghidra-decompiler>/cpp pullback4.cpp <the engine objects> -lz ; run: ./a.out [seed] [cases per row]   PBDUMP=<row substring> lists failures
#include "libdecomp.hh"
#include "rangeutil.hh"
#include <cstdio>
#include <cstdlib>
#include <map>
#include <random>
#include <string>
#include <vector>
using namespace ghidra;
static uintb M(int sz) { return sz >= 8 ? ~(uintb)0 : (((uintb)1 << (8 * sz)) - 1); }
static int64_t SX(uintb v, int sz) { if (sz >= 8) return (int64_t)v; int b = 8 * sz; uintb s = (uintb)1 << (b - 1); return (int64_t)((v ^ s) - s); }
struct Cls { long n = 0, none = 0, exact = 0, bad = 0, maskbad = 0; std::string ex; };
static std::map<std::string, Cls> G;
static bool inRange(uintb v, uintb l, uintb r, int step, int osz) {   // the range [l,r) step, mask of osz, written out (contains() semantics)
  uintb m = M(osz); v &= m;
  if (l == r) return v % step == l % step;
  if (v % step != l % step) return false;
  if (l < r) return v >= l && v < r;
  return v < r || v >= l;
}
int main(int argc, char **argv) {
  std::mt19937_64 rng(argc > 1 ? strtoull(argv[1], 0, 0) : 1);
  long cases = argc > 2 ? atol(argv[2]) : 20000;
  const char *dump = getenv("PBDUMP");
  struct B { OpCode opc; const char *name; bool cmp; };
  std::vector<B> bins = {{CPUI_INT_EQUAL,"EQUAL",1},{CPUI_INT_NOTEQUAL,"NOTEQUAL",1},{CPUI_INT_LESS,"LESS",1},{CPUI_INT_LESSEQUAL,"LESSEQUAL",1},{CPUI_INT_SLESS,"SLESS",1},{CPUI_INT_SLESSEQUAL,"SLESSEQUAL",1},{CPUI_INT_CARRY,"CARRY",1},
    {CPUI_INT_ADD,"ADD",0},{CPUI_INT_SUB,"SUB",0},{CPUI_INT_RIGHT,"RIGHT",0},{CPUI_INT_SRIGHT,"SRIGHT",0}};
  for (int in : {3, 4, 8}) {
    uintb ms = M(in);
    std::vector<uintb> ed = {0, 1, 2, 3, 4, 5, 7, 8, 9, 15, 16, 31, 32, 33, 0x7f, 0x80, 0xff, 0x100, 0x7fff, 0x8000, 0xffff, 0x10000, 0xffffff, 0x1000000, 0x7fffffff, 0x80000000ull, 0xffffffffull, 0x100000000ull, ms, ms - 1, ms >> 1, (ms >> 1) + 1, ms >> 2, (ms >> 2) + 1, 0xaaaaaaabull, 0x55555555ull, 0x12345678ull};
    for (auto &v : ed) v &= ms;
    for (const B &b : bins) {
      int out = b.cmp ? 1 : in; uintb om = M(out);
      for (long it = 0; it < cases; it++) {
        uintb l, r, val; int slot = rng() & 1, step = 1;
        auto pick = [&](uintb m) { return (rng() % 3) ? (ed[rng() % ed.size()] & m) : (rng() & m); };
        if (b.cmp) { int p = rng() % 3; l = p == 1 ? 1 : 0; r = p == 0 ? 1 : 2; val = pick(ms); }
        else { l = pick(om); r = (rng() % 8) ? pick(om) : l; val = pick(ms); if (rng() % 4 == 0) { step = 1 << (1 + rng() % 3); uintb rem = rng() % step; l = ((l - l % step) + rem) & om; r = ((r - r % step) + rem) & om; if (l % step != r % step) r = l; } if (l == r) { l = r = (step == 1) ? 0 : l % step; } }
        if (b.opc == CPUI_INT_RIGHT || b.opc == CPUI_INT_SRIGHT) { slot = (rng() % 4) ? 0 : 1; if (slot == 0) val = rng() % (8 * in + 3); }
        CircleRange R(l, r, out, step);
        CircleRange Q = R;
        bool ok = Q.pullBackBinary(b.opc, val, slot, in, out);
        char key[64]; std::snprintf(key, sizeof key, "%s in%d%s", b.name, in, (b.opc == CPUI_INT_RIGHT || b.opc == CPUI_INT_SRIGHT || b.opc == CPUI_INT_SUB) ? (slot ? " slot1" : " slot0") : "");
        Cls &c = G[key]; c.n++;
        if (!ok) { c.none++; continue; }
        // candidate inputs
        std::vector<uintb> T = {0, 1, ms, ms >> 1, (ms >> 1) + 1, val, l, r, (uintb)(0 - l), (uintb)(0 - r), ~l, ~r, val - l, val - r, val + l, val + r, l - val, r - val, 0 - val, ~val, Q.getMin(), Q.getEnd(), Q.getMax(), (r - 1), (l << (val & 63)), (r << (val & 63)), (r << (val & 63)) - 1, (l << (val & 63)) - 1};
        for (int k = 0; k < 4; k++) T.push_back(ed[rng() % ed.size()]);
        std::vector<uintb> X;
        for (uintb t : T) for (int d = -3; d <= 3; d++) X.push_back((t + d) & ms);
        for (int k = 0; k < 30; k++) X.push_back(rng() & ms);
        bool bad = false; std::string why;
        for (uintb x : X) {
          uintb o;
          uintb a = slot == 0 ? x : val, bb = slot == 0 ? val : x;
          switch (b.opc) {
            case CPUI_INT_EQUAL: o = a == bb; break; case CPUI_INT_NOTEQUAL: o = a != bb; break; case CPUI_INT_LESS: o = a < bb; break; case CPUI_INT_LESSEQUAL: o = a <= bb; break;
            case CPUI_INT_SLESS: o = SX(a, in) < SX(bb, in); break; case CPUI_INT_SLESSEQUAL: o = SX(a, in) <= SX(bb, in); break;
            case CPUI_INT_CARRY: o = ((a + bb) & ms) < a; break;
            case CPUI_INT_ADD: o = (a + bb) & ms; break; case CPUI_INT_SUB: o = (a - bb) & ms; break;
            case CPUI_INT_RIGHT: { uintb sh = slot == 0 ? val : x; uintb v2 = slot == 0 ? x : val; o = sh >= (uintb)(8 * in) ? 0 : v2 >> sh; break; }
            case CPUI_INT_SRIGHT: { uintb sh = slot == 0 ? val : x; int64_t v2 = slot == 0 ? SX(x, in) : SX(val, in); if (sh >= (uintb)(8 * in)) sh = 8 * in - 1; o = (uintb)(v2 >> sh) & ms; break; }
            default: o = 0;
          }
          bool want = inRange(o, l, r, step, out);
          bool got = Q.isEmpty() ? false : (Q.getMask() == ms ? Q.contains(x) : false);
          if (!Q.isEmpty() && Q.getMask() != ms) { bad = true; why = "mask"; break; }
          if (want != got) { bad = true; char bf[200]; std::snprintf(bf, sizeof bf, "x=%llx want %d got %d", (unsigned long long)x, (int)want, (int)got); why = bf; break; }
        }
        if (bad) { c.bad++; char bf[300]; std::snprintf(bf, sizeof bf, "R=[%llx,%llx)/%d val=%llx slot=%d -> Q=[%llx,%llx) m %llx e %d: %s", (unsigned long long)l, (unsigned long long)r, step, (unsigned long long)val, slot, (unsigned long long)Q.getMin(), (unsigned long long)Q.getEnd(), (unsigned long long)Q.getMask(), (int)Q.isEmpty(), why.c_str()); if (c.ex.empty()) c.ex = bf; if (dump && std::string(key).find(dump) != std::string::npos) std::printf("DUMP %s %s\n", key, bf); }
        else c.exact++;
      }
    }
  }
  // unary: ZEXT / SEXT (in -> out), 2COMP, NEGATE, COPY, BOOL_NEGATE at 4 and 8 bytes
  struct U { OpCode opc; const char *name; int in, out; };
  std::vector<U> uns = {{CPUI_INT_ZEXT,"ZEXT",1,4},{CPUI_INT_ZEXT,"ZEXT",2,4},{CPUI_INT_ZEXT,"ZEXT",4,8},{CPUI_INT_SEXT,"SEXT",1,4},{CPUI_INT_SEXT,"SEXT",2,4},{CPUI_INT_SEXT,"SEXT",4,8},{CPUI_INT_2COMP,"2COMP",4,4},{CPUI_INT_2COMP,"2COMP",8,8},{CPUI_INT_NEGATE,"NEGATE",4,4},{CPUI_INT_NEGATE,"NEGATE",8,8},{CPUI_COPY,"COPY",4,4}};
  for (const U &u : uns) {
    uintb om = M(u.out), ms = M(u.in);
    std::vector<uintb> ed = {0, 1, 2, 3, 0x7e, 0x7f, 0x80, 0x81, 0xff, 0x100, 0x17f, 0x180, 0x7fff, 0x8000, 0x8001, 0xffff, 0x10000, 0xff80, 0xff7f, 0xffffff80ull, 0xffff8000ull, 0xffffff7full, 0xffff7fffull, 0x7fffffff, 0x80000000ull, 0xffffffffull, 0xffffff00ull, 0x100000000ull, 0xffffffff80000000ull, 0xffffffff7fffffffull, om, om - 1, om >> 1, (om >> 1) + 1};
    for (auto &v : ed) v &= om;
    for (long it = 0; it < cases; it++) {
      uintb l = (rng() % 3) ? ed[rng() % ed.size()] : (rng() & om), r = (rng() % 8) ? ((rng() % 3) ? ed[rng() % ed.size()] : (rng() & om)) : l; int step = 1;
      if (rng() % 4 == 0) { step = 1 << (1 + rng() % 3); uintb rem = rng() % step; l = ((l - l % step) + rem) & om; r = ((r - r % step) + rem) & om; if (l % step != r % step) r = l; }
      if (l == r) { l = r = (step == 1) ? 0 : l % step; }
      CircleRange R(l, r, u.out, step), Q = R;
      bool ok = Q.pullBackUnary(u.opc, u.in, u.out);
      char key[64]; std::snprintf(key, sizeof key, "%s in%d out%d", u.name, u.in, u.out);
      Cls &c = G[key]; c.n++;
      if (!ok) { c.none++; continue; }
      std::vector<uintb> X;
      for (uintb t : {l, r, (uintb)(0 - l), (uintb)(0 - r), ~l, ~r, (uintb)0, ms, ms >> 1, (ms >> 1) + 1, Q.getMin(), Q.getEnd(), l & ms, r & ms, (l & ms) | (ms ^ (ms >> 1)), (r & ms) | (ms ^ (ms >> 1))}) for (int d = -3; d <= 3; d++) X.push_back((t + d) & ms);
      for (int k = 0; k < 30; k++) X.push_back(rng() & ms);
      bool bad = false; std::string why;
      if (!Q.isEmpty() && Q.getMask() != ms) { bad = true; why = "mask"; }
      for (uintb x : X) {
        if (bad) break;
        uintb o = 0;
        switch (u.opc) { case CPUI_INT_ZEXT: o = x; break; case CPUI_INT_SEXT: o = (uintb)SX(x, u.in) & om; break; case CPUI_INT_2COMP: o = (0 - x) & om; break; case CPUI_INT_NEGATE: o = ~x & om; break; default: o = x; }
        bool want = inRange(o, l, r, step, u.out), got = Q.isEmpty() ? false : Q.contains(x);
        if (want != got) { bad = true; char bf[100]; std::snprintf(bf, sizeof bf, "x=%llx want %d got %d", (unsigned long long)x, (int)want, (int)got); why = bf; }
      }
      if (bad) { c.bad++; char bf[300]; std::snprintf(bf, sizeof bf, "R=[%llx,%llx)/%d -> Q=[%llx,%llx) m %llx e %d: %s", (unsigned long long)l, (unsigned long long)r, step, (unsigned long long)Q.getMin(), (unsigned long long)Q.getEnd(), (unsigned long long)Q.getMask(), (int)Q.isEmpty(), why.c_str()); if (c.ex.empty()) c.ex = bf; if (dump && std::string(key).find(dump) != std::string::npos) std::printf("DUMP %s %s\n", key, bf); }
      else c.exact++;
    }
  }
  long bad = 0;
  for (auto &kv : G) { const Cls &c = kv.second; std::printf("%-26s n=%-7ld refused=%-7ld exact=%-7ld WRONG=%-6ld%s%s\n", kv.first.c_str(), c.n, c.none, c.exact, c.bad, c.ex.empty() ? "" : "  e.g. ", c.ex.c_str()); bad += c.bad; }
  std::printf("wrong pull-backs: %ld\n", bad);
  return 0;
}
