// Brute-force audit of the engine's integer OpBehavior (the constant folder and the p-code emulator share it): evaluateUnary/Binary and
// recoverInputUnary/Binary against an independent reference, every operand pair at 1 byte, edge x edge and random pairs at 2, 4 and 8 bytes.
// Build: g++ -O1 -std=c++14 -I<ghidra-decompiler>/cpp opbehavior_audit.cpp <the engine objects> -lz ; run: ./a.out [seed] [random pairs per row]
#include "libdecomp.hh"
#include "opbehavior.hh"
#include <cstdio>
#include <cstdlib>
#include <map>
#include <random>
#include <string>
#include <vector>
using namespace ghidra;
typedef unsigned __int128 u128;
typedef __int128 s128;
static uintb M(int sz) { return sz >= 8 ? ~(uintb)0 : (((uintb)1 << (8 * sz)) - 1); }
static s128 SXX(uintb v, int sz) { int b = 8 * sz; if (b >= 64) return (s128)(int64_t)v; uintb s = (uintb)1 << (b - 1); return (s128)(int64_t)((v ^ s) - s); }
static std::map<std::string, std::pair<long, long>> G; static std::map<std::string, std::string> EX;
static void chk(const std::string &k, bool ok, const char *fmt, ...) {
  auto &g = G[k]; g.first++;
  if (!ok) { g.second++; if (EX[k].empty()) { char b[300]; va_list ap; va_start(ap, fmt); vsnprintf(b, sizeof b, fmt, ap); va_end(ap); EX[k] = b; } }
}
#include <cstdarg>
int main(int argc, char **argv) {
  std::mt19937_64 rng(argc > 1 ? strtoull(argv[1], 0, 0) : 1);
  long rnd = argc > 2 ? atol(argv[2]) : 200000;
  OpBehaviorIntAdd add; OpBehaviorIntSub sub; OpBehaviorIntMult mul; OpBehaviorIntAnd andb; OpBehaviorIntOr orb; OpBehaviorIntXor xorb;
  OpBehaviorIntLeft shl; OpBehaviorIntRight shr; OpBehaviorIntSright sar; OpBehaviorEqual eq; OpBehaviorNotEqual ne; OpBehaviorIntLess lt; OpBehaviorIntLessEqual le;
  OpBehaviorIntSless slt; OpBehaviorIntSlessEqual sle; OpBehaviorIntCarry carry; OpBehaviorIntScarry scarry; OpBehaviorIntSborrow sborrow;
  OpBehaviorIntDiv udiv; OpBehaviorIntSdiv sdiv; OpBehaviorIntRem urem; OpBehaviorIntSrem srem; OpBehaviorInt2Comp neg; OpBehaviorIntNegate nt;
  OpBehaviorIntZext zext; OpBehaviorIntSext sext; OpBehaviorSubpiece sub_; OpBehaviorPiece piece; OpBehaviorPopcount pop; OpBehaviorLzcount lz;
  OpBehaviorBoolAnd band; OpBehaviorBoolOr bor; OpBehaviorBoolXor bxor; OpBehaviorBoolNegate bneg;
  for (int sz : {1, 2, 4, 8}) {
    uintb m = M(sz);
    std::vector<uintb> ed = {0, 1, 2, 3, 4, 7, 8, 15, 16, 31, 32, 33, 63, 64, 65, 127, 128, 129, 255, 256, 257, m, m - 1, m - 2, m >> 1, (m >> 1) + 1, (m >> 1) + 2, (m >> 1) - 1, m >> 8, (m >> 8) + 1, m >> 16, 0x5555555555555555ull & m, 0xAAAAAAAAAAAAAAAAull & m, 0x7F7F7F7F7F7F7F7Full & m, 0x8080808080808080ull & m};
    for (auto &v : ed) v &= m;
    uintb total = sz == 1 ? 256 : 0;
    auto each = [&](auto fn) {
      if (sz == 1) { for (uintb a = 0; a < 256; a++) for (uintb b = 0; b < 256; b++) fn(a, b); return; }
      for (uintb a : ed) for (uintb b : ed) fn(a, b);
      for (long i = 0; i < rnd; i++) { uintb a = (i % 3) ? (rng() & m) : ed[rng() % ed.size()], b = (i % 5 < 3) ? (rng() & m) : ed[rng() % ed.size()]; fn(a, b); }
    };
    (void)total;
    char key[64];
    auto K = [&](const char *n) { std::snprintf(key, sizeof key, "%s sz%d", n, sz); return std::string(key); };
    each([&](uintb a, uintb b) {
      u128 ua = a, ub = b; uintb mm = m;
      chk(K("ADD"), add.evaluateBinary(sz, sz, a, b) == ((a + b) & mm), "%llx %llx", (unsigned long long)a, (unsigned long long)b);
      chk(K("SUB"), sub.evaluateBinary(sz, sz, a, b) == ((a - b) & mm), "%llx %llx", (unsigned long long)a, (unsigned long long)b);
      chk(K("MULT"), mul.evaluateBinary(sz, sz, a, b) == ((a * b) & mm), "%llx %llx", (unsigned long long)a, (unsigned long long)b);
      chk(K("AND"), andb.evaluateBinary(sz, sz, a, b) == (a & b), "%llx %llx", (unsigned long long)a, (unsigned long long)b);
      chk(K("OR"), orb.evaluateBinary(sz, sz, a, b) == (a | b), "%llx %llx", (unsigned long long)a, (unsigned long long)b);
      chk(K("XOR"), xorb.evaluateBinary(sz, sz, a, b) == (a ^ b), "%llx %llx", (unsigned long long)a, (unsigned long long)b);
      chk(K("EQUAL"), eq.evaluateBinary(1, sz, a, b) == (a == b), "%llx %llx", (unsigned long long)a, (unsigned long long)b);
      chk(K("NOTEQUAL"), ne.evaluateBinary(1, sz, a, b) == (a != b), "%llx %llx", (unsigned long long)a, (unsigned long long)b);
      chk(K("LESS"), lt.evaluateBinary(1, sz, a, b) == (a < b), "%llx %llx", (unsigned long long)a, (unsigned long long)b);
      chk(K("LESSEQUAL"), le.evaluateBinary(1, sz, a, b) == (a <= b), "%llx %llx", (unsigned long long)a, (unsigned long long)b);
      chk(K("SLESS"), slt.evaluateBinary(1, sz, a, b) == (SXX(a, sz) < SXX(b, sz)), "%llx %llx", (unsigned long long)a, (unsigned long long)b);
      chk(K("SLESSEQUAL"), sle.evaluateBinary(1, sz, a, b) == (SXX(a, sz) <= SXX(b, sz)), "%llx %llx", (unsigned long long)a, (unsigned long long)b);
      chk(K("CARRY"), carry.evaluateBinary(1, sz, a, b) == (uintb)((ua + ub) > (u128)mm), "%llx %llx", (unsigned long long)a, (unsigned long long)b);
      { s128 s = SXX(a, sz) + SXX(b, sz); s128 lo = -((s128)1 << (8 * sz - 1)), hi = ((s128)1 << (8 * sz - 1)) - 1;
        chk(K("SCARRY"), scarry.evaluateBinary(1, sz, a, b) == (uintb)(s < lo || s > hi), "%llx %llx", (unsigned long long)a, (unsigned long long)b);
        s = SXX(a, sz) - SXX(b, sz);
        chk(K("SBORROW"), sborrow.evaluateBinary(1, sz, a, b) == (uintb)(s < lo || s > hi), "%llx %llx", (unsigned long long)a, (unsigned long long)b); }
      if (b != 0) {
        chk(K("DIV"), udiv.evaluateBinary(sz, sz, a, b) == a / b, "%llx %llx", (unsigned long long)a, (unsigned long long)b);
        chk(K("REM"), urem.evaluateBinary(sz, sz, a, b) == a % b, "%llx %llx", (unsigned long long)a, (unsigned long long)b);
        s128 x = SXX(a, sz), y = SXX(b, sz);
        if (!(sz == 8 && a == 0x8000000000000000ull && b == m)) {   // INT64_MIN s/ -1 raises SIGFPE in the host: reported below
          chk(K("SDIV"), sdiv.evaluateBinary(sz, sz, a, b) == (uintb)(x / y & (s128)mm), "%llx %llx got %llx", (unsigned long long)a, (unsigned long long)b, (unsigned long long)sdiv.evaluateBinary(sz, sz, a, b));
          chk(K("SREM"), srem.evaluateBinary(sz, sz, a, b) == (uintb)(x % y & (s128)mm), "%llx %llx", (unsigned long long)a, (unsigned long long)b);
        }
      }
    });
    // shifts: the count is any constant (up to 8 bytes), also with a size different from the value's
    std::vector<uintb> cnt = {0, 1, 2, 3, 7, 8, 15, 16, 31, 32, 33, 63, 64, 65, 127, 128, 255, 256, 0xFFFFFFFFull, 0x100000000ull, 0xFFFFFFFFFFFFFFFFull};
    for (uintb a : ed) for (uintb c : cnt) {
      uintb exL = c >= (uintb)(8 * sz) ? 0 : (a << c) & m;
      uintb exR = c >= (uintb)(8 * sz) ? 0 : a >> c;
      s128 sa = SXX(a, sz); uintb sh = c >= (uintb)(8 * sz) ? 8 * sz - 1 : c; uintb exS = (uintb)((s128)(sa >> sh)) & m;
      chk(K("LEFT"), shl.evaluateBinary(sz, sz, a, c) == exL, "%llx << %llx", (unsigned long long)a, (unsigned long long)c);
      chk(K("RIGHT"), shr.evaluateBinary(sz, sz, a, c) == exR, "%llx >> %llx", (unsigned long long)a, (unsigned long long)c);
      chk(K("SRIGHT"), sar.evaluateBinary(sz, sz, a, c) == exS, "%llx s>> %llx", (unsigned long long)a, (unsigned long long)c);
    }
    if (sz == 1) for (uintb a = 0; a < 256; a++) for (uintb c = 0; c < 16; c++) {
      chk(K("LEFT"), shl.evaluateBinary(1, 1, a, c) == (c >= 8 ? 0 : (a << c) & 0xff), "%llx << %llx", (unsigned long long)a, (unsigned long long)c);
      chk(K("RIGHT"), shr.evaluateBinary(1, 1, a, c) == (c >= 8 ? 0 : a >> c), "%llx >> %llx", (unsigned long long)a, (unsigned long long)c);
      chk(K("SRIGHT"), sar.evaluateBinary(1, 1, a, c) == (uintb)((SXX(a, 1) >> (c >= 8 ? 7 : c)) & 0xff), "%llx s>> %llx", (unsigned long long)a, (unsigned long long)c);
    }
    // unary and extensions
    for (long i = 0; i < (sz == 1 ? 256 : rnd); i++) {
      uintb a = sz == 1 ? (uintb)i : (i % 3 ? (rng() & m) : ed[rng() % ed.size()]);
      chk(K("2COMP"), neg.evaluateUnary(sz, sz, a) == ((~a + 1) & m), "%llx", (unsigned long long)a);
      chk(K("NEGATE"), nt.evaluateUnary(sz, sz, a) == (~a & m), "%llx", (unsigned long long)a);
      chk(K("POPCOUNT"), pop.evaluateUnary(sz, sz, a) == (uintb)__builtin_popcountll(a), "%llx", (unsigned long long)a);
      chk(K("LZCOUNT"), lz.evaluateUnary(sz, sz, a) == (uintb)(a == 0 ? 8 * sz : __builtin_clzll(a) - (64 - 8 * sz)), "%llx got %llx", (unsigned long long)a, (unsigned long long)lz.evaluateUnary(sz, sz, a));
      for (int osz = sz; osz <= 8; osz *= 2) {
        if (osz == sz) continue;
        chk(K("ZEXT") + " out" + std::to_string(osz), zext.evaluateUnary(osz, sz, a) == a, "%llx", (unsigned long long)a);
        chk(K("SEXT") + " out" + std::to_string(osz), sext.evaluateUnary(osz, sz, a) == ((uintb)(int64_t)SXX(a, sz) & M(osz)), "%llx got %llx", (unsigned long long)a, (unsigned long long)sext.evaluateUnary(osz, sz, a));
      }
      for (int osz = 1; osz < sz; osz++) for (int off = 0; off + osz <= sz; off++)
        chk(K("SUBPIECE") + " out" + std::to_string(osz) + " off" + std::to_string(off), sub_.evaluateBinary(osz, sz, a, off) == ((a >> (8 * off)) & M(osz)), "%llx", (unsigned long long)a);
      uintb b = rng() & m;
      if (sz <= 4) chk(K("PIECE"), piece.evaluateBinary(2 * sz, sz, a, b) == ((a << (8 * sz)) | b), "%llx %llx got %llx", (unsigned long long)a, (unsigned long long)b, (unsigned long long)piece.evaluateBinary(2 * sz, sz, a, b));
    }
    // recoverInput: when a value comes back, it must be the input (or one) that gives the output
    struct R { const char *n; const OpBehavior *b; bool bin; };
    std::vector<R> rs = {{"ADD", &add, true}, {"SUB", &sub, true}, {"LEFT", &shl, true}, {"RIGHT", &shr, true}, {"SRIGHT", &sar, true}};
    for (const R &r : rs) for (int slot = 0; slot < 2; slot++) for (long i = 0; i < (sz == 1 ? 20000 : rnd / 4); i++) {
      uintb out = rng() & m, in = (i % 3) ? (rng() & m) : ed[rng() % ed.size()];
      if (r.n[0] == 'L' || r.n[0] == 'R' || r.n[0] == 'S' && r.n[1] == 'R') { in = rng() % (8 * sz + 3); if (slot == 1) continue; }
      uintb got;
      try { got = r.b->recoverInputBinary(slot, sz, out, sz, in); } catch (...) { continue; }
      uintb back = slot == 0 ? r.b->evaluateBinary(sz, sz, got, in) : r.b->evaluateBinary(sz, sz, in, got);
      chk(std::string("recover ") + r.n + " sz" + std::to_string(sz), back == out, "slot %d out %llx in %llx -> %llx evaluates to %llx", slot, (unsigned long long)out, (unsigned long long)in, (unsigned long long)got, (unsigned long long)back);
    }
    for (long i = 0; i < (sz == 1 ? 256 : 2000); i++) {
      uintb out = sz == 1 ? (uintb)i : (rng() & m);
      uintb got = neg.recoverInputUnary(sz, out, sz); chk(K("recover 2COMP"), neg.evaluateUnary(sz, sz, got) == out, "%llx", (unsigned long long)out);
      got = nt.recoverInputUnary(sz, out, sz); chk(K("recover NEGATE"), nt.evaluateUnary(sz, sz, got) == out, "%llx", (unsigned long long)out);
      if (sz < 8) {
        uintb osz = M(2 * sz < 8 ? 2 * sz : 8);
        uintb o2 = rng() & osz;   // a value that is a zero extension of a narrow value
        if (o2 <= m) { got = zext.recoverInputUnary(2 * sz, o2, sz); chk(K("recover ZEXT"), got == o2, "%llx got %llx", (unsigned long long)o2, (unsigned long long)got); }
      }
    }
  }
  long bad = 0;
  for (auto &kv : G) { std::printf("%-34s %10ld cases %8ld wrong%s%s\n", kv.first.c_str(), kv.second.first, kv.second.second, kv.second.second ? "   e.g. " : "", EX[kv.first].c_str()); bad += kv.second.second; }
  std::printf("wrong results: %ld\n", bad);
  return 0;
}
