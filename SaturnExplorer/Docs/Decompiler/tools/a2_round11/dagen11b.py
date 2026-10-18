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

def fam_signmask(r, f):
    """the sign of a register extracted by an arithmetic shift of 31 (a chain of shar, as 64-bit sign extensions and sign tests are written) and
    tested for 0 / -1 / != 0 (RuleTestSign), or used as a mask (RuleSignShift, RuleSignForm)"""
    kind = r.choice([0, 0, 1, 2, 3])
    f.add(mov(0, 4))
    if kind == 1: f.add(extsb(0, 0))
    elif kind == 2: f.add(extsw(0, 0))
    elif kind == 3: f.add(extsb(0, 0)); f.add(addi(0, r.choice([0x7F, 1, 0x80])))
    f.add(mov(1, 0))
    n = r.choice([31, 31, 31, 15, 7, 30])
    shr_arith(f, 1, n)
    form = r.randrange(0, 6)
    if form == 0: f.add(0x2008 | 1 << 8 | 1 << 4); result_of_t(r, f, 0)                 # tst r1,r1 : sign mask == 0
    elif form == 1: f.add(movi(2, 0xFF)); f.add(0x3000 | 1 << 8 | 2 << 4); result_of_t(r, f, 0)      # cmp/eq #-1 : negative
    elif form == 2: f.add(mov(0, 1)); f.add(0x2009 | 0 << 8 | 5 << 4)                  # mask & r5
    elif form == 3: f.add(mov(0, 1)); f.add(0x201A | 0 << 8 | 5 << 4)                  # mask ^ r5
    elif form == 4: f.add(movi(2, 1)); f.add(0x2009 | 1 << 8 | 2 << 4); f.add(mov(0, 1))   # (mask & 1)
    else: f.add(0x3002 | 1 << 8 | 5 << 4); result_of_t(r, f, 0)                           # cmp/hs r5,mask

def fam_pow2b(r, f, shar_idiom=False):
    """x / 2^k and x % 2^k written with the sign mask the way the SH-2 compilers write it: shll ; subc r,r (the mask), shlr (32-k) (the bias),
    add, shar k (RuleSignDiv2, RuleSignNearMult, RuleSignForm2, RuleModOpt, RuleSignMod2nOpt*)"""
    k = r.choice([1, 2, 3, 4, 5, 8, 12, 16, 24, 30])
    src = 4
    if r.random() < 0.25:
        f.add(r.choice([extsb, extsw])(1, 4)); src = 1
    form = r.randrange(0, 5)
    f.add(mov(2, src))
    if shar_idiom:
        shr_arith(f, 2, 31)                                # r2 = x s>> 31  (the chain of shar the engine merges)
    else:
        f.add(0x0008)                                      # clrt
        f.add(SHLL | 2 << 8, 0x300A | 2 << 8 | 2 << 4)     # shll r2 ; subc r2,r2   : r2 = x < 0 ? -1 : 0
    shr_logical(f, 2, 32 - k)                              # the bias, 2^k - 1 for a negative x
    f.add(add(2, src))                                     # x + bias
    if form == 0:
        shr_arith(f, 2, k); f.add(mov(0, 2))               # x / 2^k
    elif form == 1:
        lit_or_imm(f, r, 1, (~((1 << k) - 1)) & M32); f.add(0x2009 | 2 << 8 | 1 << 4)       # & ~(2^k-1) : x rounded toward zero
        f.add(mov(0, 2))
    elif form == 2:
        lit_or_imm(f, r, 1, (~((1 << k) - 1)) & M32); f.add(0x2009 | 2 << 8 | 1 << 4)
        f.add(mov(0, src), sub(0, 2))                      # x % 2^k  = x - rounded
    elif form == 3:
        shr_arith(f, 2, k); shl(f, 2, k); f.add(mov(0, 2))
    else:
        f.add(mov(0, 2))

def fam_lessor(r, f):
    """compare pairs the engine merges (RuleLessEqual, RuleLessNotEqual, RuleEqual2Zero ...): (x < y) || (x == y), (x < y) || (x != y), (x <= y) && (x != y),
    (x > y) && (x != y), on whole registers or bytes, the two T's through movt and an or/and, or through branches"""
    kind = r.choice([0, 0, 1, 2, 4])
    reduce_in(r, f, 0, 4, kind); reduce_in(r, f, 1, 5, kind)
    if r.random() < 0.3: lit_or_imm(f, r, 1, lit_for(r, kind))
    form = r.randrange(0, 6)
    lt, le = r.choice([(0x3006, 0x3002), (0x3007, 0x3003)])   # cmp/hi, cmp/hs   or cmp/gt, cmp/ge     (T = Rn op Rm)
    swap = r.random() < 0.5
    a, b = (0, 1) if not swap else (1, 0)
    if form == 0:                                             # (a > b) || (a == b)
        f.add(lt | a << 8 | b << 4, 0x0029 | 2 << 8)
        f.add(0x3000 | a << 8 | b << 4, 0x0029 | 3 << 8)
        f.add(0x200B | 2 << 8 | 3 << 4, mov(0, 2))
    elif form == 1:                                           # (a > b) || (a != b)
        f.add(lt | a << 8 | b << 4, 0x0029 | 2 << 8)
        f.add(0x3000 | a << 8 | b << 4, 0x0029 | 3 << 8, 0x6007 | 3 << 8 | 3 << 4, 0xC900 | 1 if False else 0x0009)
        f.add(0xE400 | 1, 0x2009 | 3 << 8 | 4 << 4)
        f.add(0x200B | 2 << 8 | 3 << 4, mov(0, 2))
    elif form == 2:                                           # (a >= b) && (a != b)
        f.add(le | a << 8 | b << 4, 0x0029 | 2 << 8)
        f.add(0x3000 | a << 8 | b << 4, 0x0029 | 3 << 8)
        f.add(0xE400 | 1, 0x2009 | 3 << 8 | 4 << 4)
        f.add(0x2009 | 2 << 8 | 3 << 4, mov(0, 2))
    elif form == 3:                                           # branches: a > b ? 1 : a == b ? 1 : 0
        f.add(lt | a << 8 | b << 4); f.br('bt', 'yes')
        f.add(0x3000 | a << 8 | b << 4); f.br('bt', 'yes')
        f.add(movi(0, 0)); f.br('bra', 'end'); f.add(0x0009)
        f.label('yes'); f.add(movi(0, 1)); f.label('end')
    elif form == 4:                                           # a >= b && a != b by branches
        f.add(le | a << 8 | b << 4); f.br('bf', 'no')
        f.add(0x3000 | a << 8 | b << 4); f.br('bt', 'no')
        f.add(movi(0, 1)); f.br('bra', 'end'); f.add(0x0009)
        f.label('no'); f.add(movi(0, 0)); f.label('end')
    else:                                                     # (a > b) or (a == b), then a second use of the pieces
        f.add(lt | a << 8 | b << 4, 0x0029 | 2 << 8)
        f.add(0x3000 | a << 8 | b << 4, 0x0029 | 3 << 8)
        f.add(0x200B | 2 << 8 | 3 << 4, mov(0, 2))
        f.add(0x2009 | 0 << 8 | 3 << 4)

def fam_condconst(r, f):
    """a register compared with a constant for (in)equality, then used (not only compared) in the arm where the compare fixes its value
    (ActionConditionalConst / ConditionalExecution), with a second level, on the byte, the word or the whole register"""
    kind = r.choice([0, 0, 1, 2, 4, 4])
    reduce_in(r, f, 0, 4, kind)
    reduce_in(r, f, 3, 5, kind) if r.random() < 0.4 else f.add(mov(3, 5))
    k1 = lit_for(r, kind); k2 = lit_for(r, kind)
    lit_or_imm(f, r, 1, k1)
    f.add(0x3000 | 0 << 8 | 1 << 4)                                  # cmp/eq r1,r0
    neg = r.random() < 0.5
    f.br('bf' if neg else 'bt', 'other')
    # arm A: x == k1 (neg: x != k1)
    for _ in range(r.randrange(1, 4)):
        f.add(r.choice([add(3, 0), sub(3, 0), 0x2009 | 3 << 8 | 0 << 4, 0x200B | 3 << 8 | 0 << 4, 0x200A | 3 << 8 | 0 << 4, 0x7300 | r.choice([1, 0xFF, 3])]))
    if r.random() < 0.5:
        lit_or_imm(f, r, 2, k2); f.add(0x3000 | 0 << 8 | 2 << 4)    # a nested compare with the other literal
        f.br('bt', 'inner')
        f.add(add(3, 0)); f.label('inner')
    f.add(mov(0, 3)); f.br('bra', 'end'); f.add(0x0009)
    f.label('other')
    for _ in range(r.randrange(0, 3)):
        f.add(r.choice([add(3, 0), sub(3, 0), 0x2009 | 3 << 8 | 0 << 4, 0x200A | 3 << 8 | 0 << 4, 0x6007 | 3 << 8 | 0 << 4]))
    f.add(r.choice([mov(0, 3), sub(0, 3), add(0, 3)]))
    f.label('end')
    if r.random() < 0.4: f.add(r.choice([extub(0, 0), extsb(0, 0), extuw(0, 0)]))

def fam_pow2c(r, f): fam_pow2b(r, f, True)

FAMS2 = {38: fam_pow2c, 29: fam_udiv2, 30: fam_sdiv2, 31: fam_divpair, 32: fam_divsum, 33: fam_pow2, 34: fam_signmask, 35: fam_pow2b, 36: fam_lessor, 37: fam_condconst}
d11.FAMS.update(FAMS2)

if __name__ == "__main__":
    if sys.argv[1] == 'extras':
        first, count, fam = int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4])
        print(",".join("%X" % v for v in d11.extras(first, count, fam)))
    else:
        first, count = int(sys.argv[1]), int(sys.argv[2])
        fam = int(sys.argv[3]) if len(sys.argv) > 3 else None
        print(";".join(g for g in (d11.build(s, fam) for s in range(first, first + count)) if g))
