#!/usr/bin/env python3
# Area 2 round 11 instrument, second file (dagen11.py's families 20..28 are unchanged): the same divisions with the readers that follow a
# division in compiled code, which are what the local rules (divTermProductFits, divTermIsDivided, patches 0037 and 0044) decide on:
#   29 udiv2   unsigned x / d, then the quotient scaled, masked, multiplied, added to x or to its own remainder, compared, or stored to *r5
#   30 sdiv2   the signed form with the same readers, and the remainder forms (x % d, x - (x / d) * d)
#   31 divpair two divisions of the same dividend by different constants (their products share the multiply), or the same one twice
#   32 divsum  a division of a sum: (x + y) / d, (x * k) / d, (x >> s) / d, and a division of a byte or word
# usage as dagen11.py (first seed, count, family; "extras" first seed, count, family)
import importlib.util, os, random, sys
here = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("dagen11", os.path.join(here, "dagen11.py"))
d11 = importlib.util.module_from_spec(spec); spec.loader.exec_module(d11)
globals().update({k: v for k, v in vars(d11).items() if not k.startswith('__')})

def post_quot(r, f, q, src, d):
    """a reader of the quotient in register q (r0)"""
    k = r.randrange(0, 10)
    if k == 0: pass
    elif k == 1: shl(f, q, r.choice([1, 2, 3, 4]))                           # scaled
    elif k == 2: f.add(0xC900 | r.choice([0xFC, 0xF8, 0xF0, 0xFE, 0x7F, 0x0F, 0xFF, 0x01])) if q == 0 else None   # and #imm,r0
    elif k == 3: lit_or_imm(f, r, 3, r.choice([3, 5, 10, 12, 100, 0x10001, 0xFFFFFFFF, 6])); f.add(0x0007 | 3 << 8 | q << 4, stsmacl(q))   # mul.l r3 ... macl = r3 * q (mul.l Rm,Rn: macl = Rn * Rm)
    elif k == 4: f.add(add(q, src))                                           # + dividend
    elif k == 5: f.add(sub(q, src))                                           # quotient - dividend
    elif k == 6: shl(f, q, r.choice([1, 2])); f.add(add(q, q))
    elif k == 7:                                                              # q * d (the rounded-down dividend)
        lit_or_imm(f, r, 3, d); f.add(0x0007 | 3 << 8 | q << 4, stsmacl(q))
    elif k == 8:                                                              # remainder: src - q*d
        lit_or_imm(f, r, 3, d); f.add(0x0007 | 3 << 8 | q << 4, stsmacl(3)); f.add(mov(q, src), sub(q, 3))
    else:                                                                     # quotient xor remainder
        lit_or_imm(f, r, 3, d); f.add(0x0007 | 3 << 8 | q << 4, stsmacl(3)); f.add(mov(2, src), sub(2, 3)); f.add(0x200A | q << 8 | 2 << 4)

def fam_udiv2(r, f):
    d = pick_divisor(r)
    f.d_hint = ('udiv', d)
    if d < 2: d = 7
    src = 4
    if r.random() < 0.25:
        f.add(r.choice([extuw, extub])(1, 4)); src = 1
    emit_udiv(r, f, d, src, 0, 2)
    for _ in range(r.randrange(1, 3)): post_quot(r, f, 0, src, d)

def fam_sdiv2(r, f):
    d = r.choice(SDIVISORS) if r.random() < 0.7 else r.choice([-1, 1]) * r.randrange(3, 100000)
    f.d_hint = ('sdiv', d)
    src = 4
    if r.random() < 0.25:
        f.add(r.choice([extsw, extsb])(1, 4)); src = 1
    emit_sdiv(r, f, d, src, 0, 2)
    for _ in range(r.randrange(1, 3)): post_quot(r, f, 0, src, abs(d) if d else 3)

def fam_divpair(r, f):
    d1 = pick_divisor(r); d2 = d1 if r.random() < 0.3 else pick_divisor(r)
    d1 = max(d1, 3); d2 = max(d2, 3)
    f.d_hint = ('udiv', d1)
    emit_udiv(r, f, d1, 4, 0, 2)
    f.add(mov(6, 0))
    emit_udiv(r, f, d2, 4, 0, 2) if r.random() < 0.7 else emit_udiv(r, f, d2, 5, 0, 2)
    f.add(r.choice([add(0, 6), sub(0, 6), 0x2009 | 0 << 8 | 6 << 4, 0x200A | 0 << 8 | 6 << 4, 0x200B | 0 << 8 | 6 << 4]))

def fam_divsum(r, f):
    d = max(3, pick_divisor(r))
    f.d_hint = ('udiv', d)
    k = r.randrange(0, 5)
    if k == 0: f.add(add(1, 4) if False else mov(1, 4), add(1, 5))              # x + y
    elif k == 1: lit_or_imm(f, r, 1, r.choice([3, 5, 10, 100])); f.add(0x0007 | 1 << 8 | 4 << 4, stsmacl(1))   # x * k
    elif k == 2: f.add(mov(1, 4)); shr_logical(f, 1, r.choice([1, 2, 3, 8]))
    elif k == 3: f.add(extub(1, 4))
    else: f.add(extuw(1, 4))
    emit_udiv(r, f, d, 1, 0, 2)
    if r.random() < 0.4: post_quot(r, f, 0, 1, d)

def fam_pow2(r, f):
    """signed division and remainder by a power of two as a compiler writes them (RuleSignDiv2, RuleSignForm2, RuleSignMod2nOpt*, RuleSignNearMult)"""
    k = r.choice([1, 1, 2, 3, 4, 5, 8, 15, 16, 24, 30, 31])
    src = 4
    if r.random() < 0.3:
        f.add(r.choice([extsb, extsw])(1, 4)); src = 1
    form = r.randrange(0, 4)
    f.add(mov(2, src))                                   # r2 = x
    f.add(SHLL | 2 << 8, 0x0008 if False else 0x0009)    # shll r2 (the sign into T), nop
    f.add(0x0008, mov(2, src))                           # clrt ; r2 = x
    f.add(mov(1, src)); f.add(0x4011 | 1 << 8)           # cmp/pz r1 : T = x >= 0
    f.add(movi(3, 0)); f.br('bt', 'nonneg')
    lit_or_imm(f, r, 3, (1 << k) - 1)                    # bias = 2^k - 1 for a negative dividend
    f.label('nonneg')
    f.add(add(2, 3))                                     # r2 = x + bias
    if form == 0:                                        # x / 2^k
        shr_arith(f, 2, k); f.add(mov(0, 2))
    elif form == 1:                                      # x % 2^k  = x - ((x + bias) & ~(2^k - 1))
        lit_or_imm(f, r, 1, (~((1 << k) - 1)) & M32); f.add(0x2009 | 2 << 8 | 1 << 4)
        f.add(mov(0, src), sub(0, 2))
    elif form == 2:                                      # (x / 2^k) * 2^k
        shr_arith(f, 2, k); shl(f, 2, k); f.add(mov(0, 2))
    else:                                                # x & (2^k - 1) for the unsigned remainder
        lit_or_imm(f, r, 1, (1 << k) - 1); f.add(mov(0, src), 0x2009 | 0 << 8 | 1 << 4)

FAMS2 = {29: fam_udiv2, 30: fam_sdiv2, 31: fam_divpair, 32: fam_divsum, 33: fam_pow2}
d11.FAMS.update(FAMS2)

if __name__ == "__main__":
    if sys.argv[1] == 'extras':
        first, count, fam = int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4])
        print(",".join("%X" % v for v in d11.extras(first, count, fam)))
    else:
        first, count = int(sys.argv[1]), int(sys.argv[2])
        fam = int(sys.argv[3]) if len(sys.argv) > 3 else None
        print(";".join(g for g in (d11.build(s, fam) for s in range(first, first + count)) if g))
