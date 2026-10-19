// Brute-force audit of CircleRange::pullBackUnary / pullBackBinary at 1 and 2 byte sizes (area 2 round 10 fix; the committed form is TestPullBackRanges).
// Build: g++ -std=c++14 -I<ghidra-decompiler>/cpp pullback_audit.cpp <the engine objects> <zlib> ; run: ./a.out [random cases per row]; PBDUMP=<row key substring> lists failing cases.
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
static int64_t SX(uintb v, int sz) { int b = 8 * sz; uintb s = (uintb)1 << (b - 1); return (int64_t)((v ^ s) - s); }

struct Cls { long n = 0, none = 0, exact = 0, over = 0, under = 0, both = 0, mask = 0; std::string ex[5]; };
static std::map<std::string, Cls> G;

static bool evalBin(OpCode opc, uintb x, uintb val, int slot, int in, uintb &out) {
  uintb m = M(in);
  uintb a = slot == 0 ? x : val, b = slot == 0 ? val : x;
  switch (opc) {
    case CPUI_INT_EQUAL: out = a == b; return true;
    case CPUI_INT_NOTEQUAL: out = a != b; return true;
    case CPUI_INT_LESS: out = a < b; return true;
    case CPUI_INT_LESSEQUAL: out = a <= b; return true;
    case CPUI_INT_SLESS: out = SX(a, in) < SX(b, in); return true;
    case CPUI_INT_SLESSEQUAL: out = SX(a, in) <= SX(b, in); return true;
    case CPUI_INT_CARRY: out = ((a + b) & m) < a; return true;
    case CPUI_INT_ADD: out = (a + b) & m; return true;
    case CPUI_INT_SUB: out = (a - b) & m; return true;
    case CPUI_INT_RIGHT: out = slot == 0 ? (val >= 8 * in ? 0 : (x >> val)) : (x >= 8 * in ? 0 : (val >> x)); return true;
    case CPUI_INT_SRIGHT: {
      int64_t sv = slot == 0 ? SX(x, in) : SX(val, in);
      uintb sh = slot == 0 ? val : x;
      if (sh >= (uintb)(8 * in)) sh = 8 * in - 1;
      out = (uintb)(sv >> sh) & m; return true; }
    default: return false;
  }
}
static bool evalUn(OpCode opc, uintb x, int in, int outsz, uintb &out) {
  switch (opc) {
    case CPUI_BOOL_NEGATE: out = x ^ 1; return true;
    case CPUI_COPY: out = x; return true;
    case CPUI_INT_2COMP: out = (-x) & M(outsz); return true;
    case CPUI_INT_NEGATE: out = (~x) & M(outsz); return true;
    case CPUI_INT_ZEXT: out = x; return true;
    case CPUI_INT_SEXT: out = (uintb)SX(x, in) & M(outsz); return true;
    default: return false;
  }
}

static void record(const std::string &key, const std::string &desc, bool ok, const CircleRange &q, int in, const std::vector<char> &S, int outStepCheck) {
  Cls &c = G[key];
  c.n++;
  if (!ok) { c.none++; return; }
  bool maskBad = q.getMask() != M(in) && !q.isEmpty();
  long over = 0, under = 0;
  uintb sz = (uintb)1 << (8 * in);
  for (uintb x = 0; x < sz; x++) {
    bool inQ = q.contains(x);
    if (inQ && !S[x]) over++;
    if (!inQ && S[x]) under++;
  }
  std::string cls;
  if ((over || under || maskBad) && getenv("PBDUMP") && key.find(getenv("PBDUMP")) != std::string::npos) { static int cnt = 0; if (cnt++ < 60) std::printf("DUMP %s %s q=[%llx,%llx) over %ld under %ld\n", key.c_str(), desc.c_str(), (unsigned long long)q.getMin(), (unsigned long long)q.getEnd(), over, under); }
  if (maskBad) { c.mask++; cls = "mask"; }
  if (over && under) { c.both++; cls = "both"; }
  else if (over) { c.over++; cls = "over"; }
  else if (under) { c.under++; cls = "under"; }
  else if (!maskBad) { c.exact++; return; }
  int k = cls == "mask" ? 0 : cls == "both" ? 1 : cls == "over" ? 2 : 3;
  if (c.ex[k].empty()) {
    char buf[256];
    std::snprintf(buf, sizeof buf, " -> [%llx,%llx) mask %llx empty %d (over %ld under %ld)", (unsigned long long)q.getMin(), (unsigned long long)q.getEnd(), (unsigned long long)q.getMask(), (int)q.isEmpty(), over, under);
    c.ex[k] = cls + ": " + desc + buf;
  }
}

int main(int argc, char **argv) {
  int rnd = argc > 1 ? atoi(argv[1]) : 20000;
  std::mt19937_64 rng(12345);
  struct B { OpCode opc; const char *name; bool cmp; };
  std::vector<B> bins = {{CPUI_INT_EQUAL,"EQUAL",1},{CPUI_INT_NOTEQUAL,"NOTEQUAL",1},{CPUI_INT_LESS,"LESS",1},{CPUI_INT_LESSEQUAL,"LESSEQUAL",1},
    {CPUI_INT_SLESS,"SLESS",1},{CPUI_INT_SLESSEQUAL,"SLESSEQUAL",1},{CPUI_INT_CARRY,"CARRY",1},{CPUI_INT_ADD,"ADD",0},{CPUI_INT_SUB,"SUB",0},
    {CPUI_INT_RIGHT,"RIGHT",0},{CPUI_INT_SRIGHT,"SRIGHT",0}};
  for (int in = 1; in <= 2; in++) {
    uintb ms = M(in);
    uintb sz = ms + 1;
    for (const B &b : bins) {
      int out = b.cmp ? 1 : in;
      uintb om = M(out);
      // output ranges
      std::vector<uintb> ip = {0, 1, 2, 3, 0x7e, 0x7f, 0x80, 0x81, 0xfe, 0xff, ms - 1, ms, ms >> 1, (ms >> 1) + 1, (ms >> 1) + 2, (ms>>1)-1};
      if (in == 2) { ip.push_back(0x100); ip.push_back(0x101); ip.push_back(0xff00); ip.push_back(0x7f00); ip.push_back(0x8000); }
      for (auto &v : ip) v &= ms;
      for (int it = 0; it < rnd + (int)(ip.size() * ip.size()); it++) {
        uintb l, r, val; int slot = rng() & 1; int step = 1;
        size_t nIp = ip.size();
        if ((size_t)it < nIp * nIp) { l = ip[it / nIp]; r = ip[it % nIp]; val = ip[rng() % nIp]; }
        else { l = rng() & om; r = rng() & om; val = (rng() % 3) ? ip[rng() % nIp] : (rng() & ms); if (rng() % 4 == 0) { step = 1 << (1 + rng() % 2); uintb rem = rng() % step; l = (l - l % step) + rem; r = (r - r % step) + rem; l &= om; r &= om; } }
        if (b.cmp) {
          // output is boolean: the engine's ranges are {0},{1},{0,1} (mask 0xff); also any other, pulled arbitrarily
          int pick = (it < 6) ? it : rng() % 6;
          static const uintb bl[6] = {0, 1, 0, 1, 0, 0}, br[6] = {1, 2, 2, 2, 1, 2};
          l = bl[pick % 6]; r = br[pick % 6];
          if (pick == 4) { l = 2; r = 3; }  // neither 0 nor 1
          val = (rng() % 3) ? ip[rng() % nIp] : (rng() & ms);
        }
        if (b.opc == CPUI_INT_RIGHT || b.opc == CPUI_INT_SRIGHT) { slot = (rng() % 4) ? 0 : 1; if (slot == 0) val = rng() % (8 * in + 3); }
        if (step > 1 && l == r) continue;
        CircleRange R(l, r, out, step);
        if (R.getMask() != om) continue;
        // truth
        std::vector<char> S(sz, 0);
        bool okEval = true;
        for (uintb x = 0; x < sz; x++) {
          uintb o;
          okEval = evalBin(b.opc, x, val, slot, in, o);
          S[x] = R.contains(o);
        }
        CircleRange Q = R;
        bool ok = Q.pullBackBinary(b.opc, val, slot, in, out);
        char key[64], desc[160];
        std::snprintf(key, sizeof key, "%s in%d%s%s", b.name, in, (b.cmp && l == 0 && r == 2) ? " bothTF" : "", (b.opc == CPUI_INT_RIGHT || b.opc == CPUI_INT_SRIGHT || b.opc == CPUI_INT_SUB) ? (slot ? " slot1" : " slot0") : "");
        std::snprintf(desc, sizeof desc, "R=[%llx,%llx) step %d val=%llx slot=%d", (unsigned long long)l, (unsigned long long)r, step, (unsigned long long)val, slot);
        record(key, desc, ok, Q, in, S, 0);
      }
    }
  }
  // unary
  struct U { OpCode opc; const char *name; int in, out; };
  std::vector<U> uns = {{CPUI_BOOL_NEGATE,"BOOL_NEGATE",1,1},{CPUI_COPY,"COPY",1,1},{CPUI_COPY,"COPY",2,2},{CPUI_INT_2COMP,"2COMP",1,1},{CPUI_INT_2COMP,"2COMP",2,2},
    {CPUI_INT_NEGATE,"NEGATE",1,1},{CPUI_INT_NEGATE,"NEGATE",2,2},{CPUI_INT_ZEXT,"ZEXT",1,2},{CPUI_INT_SEXT,"SEXT",1,2},{CPUI_INT_ZEXT,"ZEXT",1,4},{CPUI_INT_SEXT,"SEXT",1,4},{CPUI_INT_ZEXT,"ZEXT",2,4},{CPUI_INT_SEXT,"SEXT",2,4}};
  for (const U &u : uns) {
    uintb om = M(u.out), ms = M(u.in), sz = ms + 1;
    std::vector<uintb> ip = {0, 1, 2, 3, 0x7e, 0x7f, 0x80, 0x81, 0xfe, 0xff, 0x100, 0x101, 0x17f, 0x180, 0x1ff, 0x7f00, 0x7fff, 0x8000, 0xff00, 0xff7f, 0xff80, 0xff81, 0xfffe, 0xffff, 0xffffff80, 0xffffff7f, 0xffff8000, 0xffff7fff, 0x80, 0xffffff00, 0x7fff0000};
    for (auto &v : ip) v &= om;
    for (int it = 0; it < rnd + (int)(ip.size() * ip.size()); it++) {
      uintb l, r; int step = 1;
      size_t nIp = ip.size();
      if ((size_t)it < nIp * nIp) { l = ip[it / nIp]; r = ip[it % nIp]; }
      else { l = rng() & om; r = rng() & om; if (rng() % 4 == 0) { step = 1 << (1 + rng() % 2); uintb rem = rng() % step; l = (l - l % step) + rem; r = (r - r % step) + rem; l &= om; r &= om; } }
      if (u.opc == CPUI_BOOL_NEGATE) { int pick = rng() % 4; static const uintb bl[4] = {0, 1, 0, 2}, br[4] = {1, 2, 2, 3}; l = bl[pick]; r = br[pick]; step = 1; }
      if (step > 1 && l == r) continue;
      CircleRange R(l, r, u.out, step);
      std::vector<char> S(sz, 0);
      for (uintb x = 0; x < sz; x++) { uintb o; evalUn(u.opc, x, u.in, u.out, o); S[x] = R.contains(o) && (u.opc != CPUI_BOOL_NEGATE || x < 2); }
      if (u.opc == CPUI_BOOL_NEGATE && (l == 2)) continue;
      CircleRange Q = R;
      bool ok = Q.pullBackUnary(u.opc, u.in, u.out);
      char key[64], desc[160];
      std::snprintf(key, sizeof key, "%s in%d out%d", u.name, u.in, u.out);
      std::snprintf(desc, sizeof desc, "R=[%llx,%llx) step %d", (unsigned long long)l, (unsigned long long)r, step);
      record(key, desc, ok, Q, u.in, S, 0);
    }
  }
  for (auto &kv : G) {
    const Cls &c = kv.second;
    std::printf("%-22s n=%-7ld false=%-7ld exact=%-7ld over=%-6ld under=%-6ld both=%-6ld maskbad=%ld\n", kv.first.c_str(), c.n, c.none, c.exact, c.over, c.under, c.both, c.mask);
    for (int k = 0; k < 4; k++) if (!c.ex[k].empty()) std::printf("     %s\n", c.ex[k].c_str());
  }
  return 0;
}
