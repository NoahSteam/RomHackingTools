// Area 2 round 11 instrument: RuleDivOpt::calcDivisor (the claim 'sub(zext(x)*y, n/8) >> n%8 is x / d for every x below 2^xsize') against the
// arithmetic itself. y is built around ceil(2^n / d) for random d (the compilers' magic numbers, off by -2..+3 as well), n and xsize drawn, and
// every divisor calcDivisor returns is checked at the multiples of d and their neighbours, the top of the range and random x.
// Build: g++ -O1 -std=c++14 -I<ghidra-decompiler>/cpp divopt_audit.cpp <the engine objects> -lz ; run: ./a.out [seed] [cases]
#include "libdecomp.hh"
#include "multiprecision.hh"
#include <cstdio>
#include <cstdlib>
#include <random>
using namespace ghidra;
// CALCDIVISOR: cut from the vendored ruleaction.cc by divopt_audit.sh (RuleDivOpt::calcDivisor, renamed calcDivisorCopy)
#include "calcdivisor_copy.inc"
typedef unsigned __int128 u128;
int main(int argc, char **argv) {
  std::mt19937_64 rng(argc > 1 ? strtoull(argv[1], 0, 0) : 1);
  long cases = argc > 2 ? atol(argv[2]) : 2000000;
  long claimed = 0, bad = 0, refusedGood = 0;
  std::printf("%-6s %-10s %-34s %-10s %s\n", "n", "xsize", "y", "divisor", "first failing x");
  for (long it = 0; it < cases; it++) {
    unsigned xsize = 1 + rng() % 40;
    unsigned n = xsize + rng() % 40; if (n > 100) n = 100;
    uint64_t d;
    int k = rng() % 6;
    if (k == 0) d = 2 + rng() % 20; else if (k == 1) d = 2 + rng() % 1000; else if (k == 2) d = 2 + rng() % 70000; else if (k == 3) d = ((uint64_t)1 << (rng() % 40)) + (int)(rng() % 5) - 2; else d = 2 + (rng() >> (24 + rng() % 30));
    if (d < 2) d = 2;
    u128 p = (u128)1 << n;
    u128 yy = (p + d - 1) / d + (int)(rng() % 6) - 2;
    if (yy >> 65) continue;
    uint8 y[2] = { (uint8)yy, (uint8)(yy >> 64) };
    uint8 ycopy[2] = { y[0], y[1] };
    uintb dd = calcDivisorCopy(n, ycopy, xsize);
    if (dd == 0) continue;
    claimed++;
    // check x*y >> n == x / dd for x < 2^xsize
    u128 xmax = ((u128)1 << xsize) - 1;
    bool ok = true; u128 fail = 0;
    auto test = [&](u128 x) { if (x > xmax || !ok) return; u128 lhs = (x * yy) >> n; u128 rhs = x / dd; if (lhs != rhs) { ok = false; fail = x; } };
    for (u128 m : {(u128)0, (u128)1, xmax, xmax - 1, xmax / 2, xmax / 2 + 1}) test(m);
    for (int j = 0; j < 400 && ok; j++) {
      u128 q = (xmax / dd) - (rng() % (j < 100 ? 4 : 1000)); if (q > xmax / dd) q = rng() % (xmax / dd + 1);
      for (int dx = -2; dx <= 2; dx++) test(q * dd + dx);
      test((u128)(rng() % (uint64_t)(xmax + 1 > ((u128)1 << 63) ? ((u128)1 << 63) : xmax + 1)));
    }
    if (!ok) { bad++; if (bad <= 15) std::printf("%-6u %-10u %-34llx %-10llu %llu (x*y>>n=%llu, x/d=%llu)\n", n, xsize, (unsigned long long)yy, (unsigned long long)dd, (unsigned long long)fail, (unsigned long long)((fail * yy) >> n), (unsigned long long)(fail / dd)); }
  }
  // the signed form: findForm's xsize counts the sign, applyOp passes xsize-1; the claim is floor(x*y / 2^n) == trunc(x / d) + (x < 0 ? -1 : 0)
  long sclaimed = 0, sbad = 0;
  for (long it = 0; it < cases / 2; it++) {
    unsigned xsize = 2 + rng() % 40;
    unsigned n = xsize + rng() % 40; if (n > 100) n = 100;
    uint64_t d;
    int k = rng() % 6;
    if (k == 0) d = 2 + rng() % 20; else if (k == 1) d = 2 + rng() % 1000; else if (k == 2) d = 2 + rng() % 70000; else if (k == 3) d = ((uint64_t)1 << (rng() % 40)) + (int)(rng() % 5) - 2; else d = 2 + (rng() >> (24 + rng() % 30));
    if (d < 2) d = 2;
    u128 p = (u128)1 << n;
    u128 yy = (p + d - 1) / d + (int)(rng() % 6) - 2;
    if (yy >> 65) continue;
    uint8 y[2] = { (uint8)yy, (uint8)(yy >> 64) };
    uintb dd = calcDivisorCopy(n, y, xsize - 1);
    if (dd == 0) continue;
    sclaimed++;
    typedef __int128 s128;
    s128 xmax = ((s128)1 << (xsize - 1)) - 1, xmin = -((s128)1 << (xsize - 1));
    bool ok = true; s128 fail = 0;
    auto test = [&](s128 x) { if (x > xmax || x < xmin || !ok) return;
      s128 prod = x * (s128)yy; s128 lhs = prod >> n;   // arithmetic shift: floor
      s128 rhs = x / (s128)dd + (x < 0 ? -1 : 0);
      if (lhs != rhs) { ok = false; fail = x; } };
    for (s128 m : {(s128)0, (s128)1, (s128)-1, xmax, xmax - 1, xmin, xmin + 1, xmax / 2, xmin / 2}) test(m);
    for (int j = 0; j < 400 && ok; j++) {
      s128 q = (xmax / (s128)dd) - (s128)(rng() % (j < 100 ? 4 : 1000)); if (q < 0) q = (s128)(rng() % (uint64_t)(xmax / (s128)dd + 1));
      for (int dx = -2; dx <= 2; dx++) { test(q * (s128)dd + dx); test(-(q * (s128)dd) + dx); }
      s128 rx = (s128)(rng() % (uint64_t)((xmax >> 1) > ((s128)1 << 62) ? ((s128)1 << 62) : (xmax >> 1) + 1)); test(rx); test(-rx);
    }
    if (!ok) { sbad++; if (sbad <= 15) std::printf("signed n=%u xsize=%u y=%llx d=%llu fails at x=%lld\n", n, xsize, (unsigned long long)yy, (unsigned long long)dd, (long long)fail); }
  }
  std::printf("calcDivisor claimed %ld unsigned divisions, %ld wrong; %ld signed, %ld wrong\n", claimed, bad, sclaimed, sbad);
  bad += sbad;
  return bad != 0;
}
