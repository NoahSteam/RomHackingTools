#!/usr/bin/env python3
# Area 2 round 12 instrument: PcodeOp::getConsumeLocal (patch 0054: the backward step of ActionDeadCode and of the masks Funcdata keeps wide enough)
# cut out of op.cc and checked for soundness. The meaning of a consume mask: the bits of an input that can change a bit of the output that is
# consumed. So for an op, an input slot s, an output consume mask outc and the mask res the function answers for the slot: two values of the
# input that agree on the bits of res (the others anything) must give outputs that agree on the bits of outc. Checked with the other input
# fixed (a constant where the function reads one), exhaustively at one byte: every outc, every input pair, a set of values for the other
# input; two bytes sampled. Output: per op the number of (outc, slot) cases where a bit outside res changes a consumed output bit.
# usage: consume_audit.py <ghidra-decompiler/cpp dir> <out.cpp>   then  g++ ... out.cpp <engine objects> -lz ; ./a.out [seed]
import re, sys
cpp, out = sys.argv[1], sys.argv[2]
src = open(cpp + "/op.cc").read()
i = src.index("bool PcodeOp::getConsumeLocal(int4 slot,uintb outc,uintb &res) const")
j = src.index("\n}\n", i)
body = src[i:j + 3].replace("bool PcodeOp::getConsumeLocal", "bool FakeOp::getConsumeLocal")
prog = r'''
#include "libdecomp.hh"
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>
#include <map>
#include <string>
using namespace ghidra;
struct FakeVn { bool c; uintb off; int4 sz; uintb nz; bool isConstant() const { return c; } uintb getOffset() const { return off; } int4 getSize() const { return sz; } uintb getNZMask() const { return nz; } };
struct FakeOp {
  OpCode opc; FakeVn in[4]; FakeVn outv; const FakeVn *output;
  OpCode code() const { return opc; }
  const FakeVn *getIn(int4 i) const { return &in[i]; }
  bool getConsumeLocal(int4 slot,uintb outc,uintb &res) const;
};
''' + body + r'''
static uintb M(int sz) { return sz >= 8 ? ~(uintb)0 : (((uintb)1 << (8 * sz)) - 1); }
static intb SX(uintb v, int sz) { uintb s = (uintb)1 << (8 * sz - 1); return (intb)((v ^ s) - s); }
// the op's value, the inputs x (slot 0) and y (slot 1)
static uintb eval(OpCode opc, int in0, int in1, int outsz, uintb x, uintb y) {
  uintb m = M(outsz);
  switch (opc) {
    case CPUI_INT_ADD: return (x + y) & m;
    case CPUI_INT_SUB: return (x - y) & m;
    case CPUI_INT_MULT: return (x * y) & m;
    case CPUI_INT_AND: return x & y;
    case CPUI_INT_OR: return x | y;
    case CPUI_INT_XOR: return x ^ y;
    case CPUI_INT_NEGATE: return (~x) & m;
    case CPUI_COPY: return x;
    case CPUI_INT_ZEXT: return x;
    case CPUI_INT_SEXT: return (uintb)SX(x, in0) & m;
    case CPUI_SUBPIECE: return y >= (uintb)in0 ? 0 : (x >> (8 * y)) & m;
    case CPUI_PIECE: return ((x << (8 * in1)) | y) & m;
    case CPUI_INT_LEFT: return y >= (uintb)(8 * in0) ? 0 : (x << y) & m;
    case CPUI_INT_RIGHT: return y >= (uintb)(8 * in0) ? 0 : x >> y;
    case CPUI_INT_EQUAL: return x == y;
    case CPUI_INT_NOTEQUAL: return x != y;
    case CPUI_INT_LESS: return x < y;
    case CPUI_INT_LESSEQUAL: return x <= y;
    case CPUI_POPCOUNT: return (uintb)__builtin_popcountll(x) & m;
    case CPUI_LZCOUNT: { int n = 8 * in0; int k = 0; for (int b = n - 1; b >= 0 && !((x >> b) & 1); --b) ++k; return (uintb)k & m; }
    default: return 0;
  }
}
struct Case { OpCode opc; const char *name; int in0, in1, outsz; bool y_const; };
static std::mt19937_64 rng;
int main(int argc, char **argv) {
  rng.seed(argc > 1 ? atol(argv[1]) : 1);
  std::vector<Case> cases = {
    {CPUI_INT_ADD,"ADD",1,1,1,false},{CPUI_INT_SUB,"SUB",1,1,1,false},{CPUI_INT_MULT,"MULT",1,1,1,false},{CPUI_INT_MULT,"MULT const",1,1,1,true},
    {CPUI_INT_AND,"AND",1,1,1,false},{CPUI_INT_AND,"AND const",1,1,1,true},{CPUI_INT_OR,"OR",1,1,1,false},{CPUI_INT_OR,"OR const",1,1,1,true},{CPUI_INT_XOR,"XOR",1,1,1,false},
    {CPUI_INT_NEGATE,"NEGATE",1,0,1,false},{CPUI_COPY,"COPY",1,0,1,false},{CPUI_INT_ZEXT,"ZEXT 1-2",1,0,2,false},{CPUI_INT_SEXT,"SEXT 1-2",1,0,2,false},
    {CPUI_SUBPIECE,"SUBPIECE 2-1 at 0",2,1,1,true},{CPUI_SUBPIECE,"SUBPIECE 2-1 at 1",2,1,1,true},{CPUI_PIECE,"PIECE 1,1",1,1,2,false},
    {CPUI_INT_LEFT,"LEFT const",1,1,1,true},{CPUI_INT_RIGHT,"RIGHT const",1,1,1,true},{CPUI_INT_LEFT,"LEFT",1,1,1,false},{CPUI_INT_RIGHT,"RIGHT",1,1,1,false},
    {CPUI_INT_EQUAL,"EQUAL",1,1,1,false},{CPUI_INT_NOTEQUAL,"NOTEQUAL",1,1,1,false},{CPUI_INT_LESS,"LESS",1,1,1,false},{CPUI_INT_LESSEQUAL,"LESSEQUAL",1,1,1,false},
    {CPUI_POPCOUNT,"POPCOUNT",1,0,1,false},{CPUI_LZCOUNT,"LZCOUNT",1,0,1,false}};
  long totalBad = 0;
  for (const Case &cs : cases) {
    long checks = 0, bad = 0; std::string ex;
    int in0 = cs.in0, in1 = cs.in1, outsz = cs.outsz;
    std::vector<uintb> ys;
    if (cs.opc == CPUI_SUBPIECE) ys = {(uintb)(std::string(cs.name).back() == '1' ? 1 : 0)};
    else if (cs.y_const && (cs.opc == CPUI_INT_LEFT || cs.opc == CPUI_INT_RIGHT)) ys = {0,1,2,3,4,5,6,7,8,9,31,32,64,65};
    else if (cs.y_const) { for (uintb v = 0; v < 256; v += (v < 16 ? 1 : 7)) ys.push_back(v); ys.push_back(0x80); ys.push_back(0xff); ys.push_back(0xfe); }
    else if (in1 > 0) { for (int k = 0; k < 24; ++k) ys.push_back(rng() & M(in1)); ys.push_back(0); ys.push_back(M(in1)); ys.push_back(1); ys.push_back(M(in1) >> 1); ys.push_back((M(in1) >> 1) + 1); }
    else ys = {0};
    // the non-zero masks the comparisons are told: the values of the inputs are drawn inside them
    std::vector<std::pair<uintb,uintb>> nzs = {{M(in0), M(in1 ? in1 : 1)}, {0x0f, 0xff}, {0xf0, 0x3c}, {0xff, 0x0f}};
    if (cs.opc != CPUI_INT_EQUAL && cs.opc != CPUI_INT_NOTEQUAL && cs.opc != CPUI_INT_LESS && cs.opc != CPUI_INT_LESSEQUAL) nzs = {{M(in0), M(in1 ? in1 : 1)}};
    for (auto &nz : nzs) {
      FakeOp op; op.opc = cs.opc;
      op.in[0] = {false, 0, in0, nz.first};
      op.in[1] = {cs.y_const, 0, in1 ? in1 : 1, nz.second};
      op.outv = {false, 0, outsz, M(outsz)}; op.output = &op.outv;
      for (uintb y : ys) {
        if (in1 > 0 && !cs.y_const && cs.opc != CPUI_PIECE) { /* y is a value of the second input */ }
        op.in[1].off = y; if (cs.y_const) op.in[1].off = y;
        // slot 0: vary x outside res0
        int nslots = (in1 > 0 && !cs.y_const) ? 2 : 1;
        for (int slot = 0; slot < nslots; ++slot) {
          int insz = slot == 0 ? in0 : in1;
          uintb im = (slot == 0 ? nz.first : nz.second) & M(insz);
          uintb outcs[] = {0, 1, 2, 3, 4, 7, 0xf, 0x10, 0x3c, 0x55, 0x7f, 0x80, 0xc0, 0xf0, 0xff, 0x100, 0x1ff, 0xff00, 0xffff, 0x8000, 0x7fff};
          std::vector<uintb> ocl;
          if (outsz == 1) for (uintb o = 0; o < 256; ++o) ocl.push_back(o);
          else for (uintb o : outcs) ocl.push_back(o & M(outsz));
          for (uintb outc : ocl) {
            uintb res;
            // the function answers for the op as built; for the comparisons the second input's value is not known to it, its nz mask is
            if (!op.getConsumeLocal(slot, outc, res)) continue;
            ++checks;
            // all pairs of values of the slot's input (inside its nz mask) that agree on res
            bool ok = true; uintb bx = 0, bx2 = 0;
            uintb xs = (insz <= 1) ? 256 : 1024;
            for (uintb a = 0; a < xs && ok; ++a) {
              uintb x = (insz <= 1) ? a : (rng() & M(insz));
              if (x & ~im) continue;
              for (uintb b = 0; b < xs && ok; ++b) {
                uintb x2 = (insz <= 1) ? b : (insz <= 1 ? b : ((x & res) | (rng() & ~res & M(insz))));
                if (x2 & ~im) continue;
                if ((x ^ x2) & res) continue;
                uintb v1 = slot == 0 ? eval(cs.opc, in0, in1, outsz, x, y) : eval(cs.opc, in0, in1, outsz, y, x);
                uintb v2 = slot == 0 ? eval(cs.opc, in0, in1, outsz, x2, y) : eval(cs.opc, in0, in1, outsz, y, x2);
                if ((v1 ^ v2) & outc) { ok = false; bx = x; bx2 = x2; }
              }
            }
            if (!ok) { ++bad; if (ex.empty()) { char buf[300]; snprintf(buf, sizeof buf, "slot %d outc %llx -> res %llx, y=%llx nz=%llx/%llx: inputs %llx and %llx agree on res and differ in a consumed output bit", slot, (unsigned long long)outc, (unsigned long long)res, (unsigned long long)y, (unsigned long long)nz.first, (unsigned long long)nz.second, (unsigned long long)bx, (unsigned long long)bx2); ex = buf; } }
          }
        }
      }
    }
    printf("%-22s %9ld cases %6ld UNSOUND%s%s\n", cs.name, checks, bad, bad ? "  e.g. " : "", ex.c_str());
    totalBad += bad;
  }
  printf("unsound consume masks: %ld\n", totalBad);
  return totalBad != 0;
}
'''
open(out, 'w').write(prog)
