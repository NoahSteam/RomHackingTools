#!/usr/bin/env python3
# Area 2 round 12: environment-gated mutants of the rules the brief listed as without a local audit. One build holds all of them;
# SE_MUT=<id> turns one on, so a workload is run once per mutant without a rebuild. usage: mutate12.py <ruleaction.cc> <subflow.cc>  (edits in place)
import sys
M = []   # (id, file, old, new, description)
def m(i, f, old, new, desc): M.append((i, f, old, new, desc))
R, S = 'ruleaction.cc', 'subflow.cc'
m(1, R, "coef1 = (coef1 + coef2) & calc_mask(vn1->getSize()); // The new coefficient", "coef1 = (seMut(1) ? (coef1 - coef2) : (coef1 + coef2)) & calc_mask(vn1->getSize()); // The new coefficient", "RuleCollectTerms: coefficients subtracted")
m(2, R, "  coef1 &= calc_mask(vn1->getSize());\n\t\t\t\t// Lump all the non-zero constants", "  if (!seMut(2)) coef1 &= calc_mask(vn1->getSize());\n\t\t\t\t// Lump all the non-zero constants", "RuleCollectTerms: the sum of constants not masked")
m(4, R, "  case CPUI_INT_NOTEQUAL:\t// Boolean 0\n  case CPUI_INT_SLESS:\n  case CPUI_INT_LESS:", "  case CPUI_INT_NOTEQUAL:\t// Boolean 0\n  case CPUI_INT_SLESS:\n    vn = data.newConstant(1,0);\n    break;\n  case CPUI_INT_LESS:\n    if (seMut(4)) { vn = data.newConstant(1,1); break; }", "RuleTrivialArith: x u< x is true")
m(5, R, "  case CPUI_INT_EQUAL:\t\t// Boolean 1\n  case CPUI_INT_SLESSEQUAL:\n  case CPUI_INT_LESSEQUAL:", "  case CPUI_INT_EQUAL:\t\t// Boolean 1\n  case CPUI_INT_SLESSEQUAL:\n    vn = data.newConstant(1,1);\n    break;\n  case CPUI_INT_LESSEQUAL:\n    if (seMut(5)) { vn = data.newConstant(1,0); break; }", "RuleTrivialArith: x u<= x is false")
m(6, R, "  OpCode extcode = (op->code() == CPUI_INT_RIGHT) ? CPUI_INT_ZEXT : CPUI_INT_SEXT;\n  if (sa == 0) {", "  OpCode extcode = ((op->code() == CPUI_INT_RIGHT) != (seMut(6) && sa == 0)) ? CPUI_INT_ZEXT : CPUI_INT_SEXT;\n  if (sa == 0) {", "RuleConcatShift: exact cancellation extends with the other sign")
m(7, R, "  if ((val>>(8*smallsize))==0) { // Is zero extension unnecessary", "  if ((val>>(8*smallsize + (seMut(7) ? 1 : 0)))==0) { // Is zero extension unnecessary", "RuleZextEliminate: a constant one bit too wide is accepted")
m(8, R, "  x = shiftop->getIn(0);\n  if (x != op->getIn(1)) return 0;\n  if (n != 8*x->getSize() -1) return 0;", "  x = shiftop->getIn(0);\n  if (x != op->getIn(1) && !seMut(8)) return 0;\n  if (n != 8*x->getSize() -1) return 0;", "RulePiece2Sext: the sign of another value accepted")
m(9, R, "  if (op->getIn(1)->getOffset() != 1) return 0;\n  addout = op->getIn(0);", "  if (op->getIn(1)->getOffset() != 1 && !(seMut(9) && op->getIn(1)->getOffset() == 2)) return 0;\n  addout = op->getIn(0);", "RuleSignDiv2: a shift by 2 taken for a division by 2")
m(10, R, "  int4 c = op->getIn(1)->getOffset();\n  if (c < a->getSize()) return 0;\n  if (a->isFree()) return 0;", "  int4 c = op->getIn(1)->getOffset();\n  if (c < a->getSize() - (seMut(10) ? 1 : 0)) return 0;\n  if (a->isFree()) return 0;", "RuleSignForm: a SUBPIECE that keeps one byte of the value accepted")
m(11, S, "  if (outvn->getConsume() != op->getIn(1)->getOffset()) return 0;\n  if ((outvn->getConsume() & 1)==0) return 0;", "  if (outvn->getConsume() != op->getIn(1)->getOffset() && !seMut(11)) return 0;\n  if ((outvn->getConsume() & 1)==0) return 0;", "RuleSubvarAnd: the mask need not equal the consume mask")
m(12, S, "  if ((mask >> sa) != (uintb)1) return 0; // Pulling out a single bit", "  if ((mask >> sa) != (uintb)1 && !(seMut(12) && (mask >> sa) == 3)) return 0; // Pulling out a single bit", "RuleSubvarShift: two bits taken for a single bit")
m(13, R, "static bool divTermProductFits(const uint8 *multConst,uint4 xbits,int4 size,bool isSigned)\n\n{", "static bool divTermProductFits(const uint8 *multConst,uint4 xbits,int4 size,bool isSigned)\n\n{\n  if (seMut(13)) return true;", "divTermProductFits: always fits (patch 0037/0044 guard off)")
m(14, R, "static bool divTermIsDivided(Varnode *outvn,uint4 n,uint8 *multConst,uint4 xbits,OpCode shiftopc,int4 size)\n\n{", "static bool divTermIsDivided(Varnode *outvn,uint4 n,uint8 *multConst,uint4 xbits,OpCode shiftopc,int4 size)\n\n{\n  if (seMut(14)) return true;", "divTermIsDivided: always divided (guard off)")
m(15, R, "    if (sa1 + sa2 < 8*size) {\n      Varnode *newvn = data.newConstant(4,sa1+sa2);", "    if (sa1 + sa2 < 8*size + (seMut(15) ? 1 : 0)) {\n      Varnode *newvn = data.newConstant(4,sa1+sa2);", "RuleDoubleShift: two same-direction shifts adding to the width kept as a shift")
m(16, R, "      mask = (mask >> sa2) & mask;\t// Least significant bits remain after initial INT_LEFT", "      mask = (mask >> (sa2 + (seMut(16) ? 1 : 0))) & mask;\t// Least significant bits remain after initial INT_LEFT", "RuleDoubleShift: the mask one bit short")
m(17, R, "    andconst  = baseconst << subop->getIn(1)->getOffset() * 8;", "    andconst  = baseconst << subop->getIn(1)->getOffset() * (seMut(17) ? 4 : 8);", "RuleAndCompare: the byte offset of a SUBPIECE taken for nibbles")
src = {R: open(sys.argv[1]).read(), S: open(sys.argv[2]).read()}
hdr = "\n#include <cstdlib>\nstatic inline bool seMut(int id) { static int cur = -2; if (cur == -2) { const char *e = getenv(\"SE_MUT\"); cur = e ? atoi(e) : -1; } return cur == id; }\n"
for f in src:
    k = src[f].index("namespace ghidra {")
    src[f] = src[f][:k] + hdr + src[f][k:]
for i, f, old, new, desc in M:
    if src[f].count(old) != 1: print("MUTANT %d does not apply (%d matches): %s" % (i, src[f].count(old), desc)); continue
    src[f] = src[f].replace(old, new)
    print("mutant %2d: %s" % (i, desc))
open(sys.argv[1], 'w').write(src[R]); open(sys.argv[2], 'w').write(src[S])
