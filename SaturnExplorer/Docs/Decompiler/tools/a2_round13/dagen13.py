#!/usr/bin/env python3
# Area 2 round 13 instrument. Register-only functions for the printed-C differential (and for the in-situ rule check, rulecheck13.hh), on the
# constant, shift and extension arithmetic of the rules (loads dagen12.py and through it dagen11.py and dagen10.py; their helpers are reused):
#   55 soup      straight-line soup of shifts, rotates, extensions, negation, swaps, immediates, the multiplies (with magic-like and random literals) and
#                the compares, 4 to 12 steps over r0..r3 with the inputs r4 and r5
#   56 lerp      fixed-point blends: a + ((b - a) * t >> k), (x * c) >> k with the product's high or middle word, plus the original value (RuleDivTermAdd,
#                RuleDivOpt, RuleSignForm2 on arbitrary multipliers)
#   57 narrow    a value shifted left by a total of 8 to 72 bits (fixed shifts, in several steps when 32 or more), cut to a byte or a word, then tested, compared or shifted back
#                (RuleShiftCompare, RuleSubCommute, RuleDoubleShift with shifts at or beyond the narrowed width)
#   58 negshift  a negated or sign-extended byte/word shifted right by 7 to 31, with the multiply by -1 read as a division (RuleDivOpt, RuleSignDiv2,
#                RuleSignForm)
#   59 shdiv     a value shifted right (1 to 16), then divided by a constant that is large for the shifted width (RuleDivChain, RuleDivOpt)
# usage: dagen13.py <first seed> <count> [family 55..59]   -> one function per seed, ";" joined (SE_GENERATED_PRINTEDC_WORDS)
#        dagen13.py extras <first seed> <count> <family>   -> the comma-joined hex list for SE_GENERATED_PRINTEDC_EXTRA
import importlib.util, os, random, sys

here = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("dagen12", os.path.join(here, "..", "a2_round12", "dagen12.py"))
d12 = importlib.util.module_from_spec(spec)
spec.loader.exec_module(d12)
globals().update({k: v for k, v in vars(d12).items() if not k.startswith('__')})

MAGIC = [0xAAAAAAAB, 0xCCCCCCCD, 0x92492493, 0x24924925, 0x38E38E39, 0x66666667, 0x2AAAAAAB, 0x51EB851F, 0x55555556, 0x80000001, 0x7FFFFFFF,
         0x10000, 0x8000, 0xB505, 0xCCCD, 0xAAAB, 0x5556, 0x6667, 0x1999A, 0x6487E, 0xFFFF, 0x10001, 0x3C3C3C3D, 0xFFFFFFFE, 0xFFFFFFFF, 0x80000000, 2, 3]
RAND_LITS = lambda r: r.choice(MAGIC) if r.random() < 0.7 else r.getrandbits(32)

def swapb(m, n): return 0x6008 | n << 8 | m << 4
def swapw(m, n): return 0x6009 | n << 8 | m << 4
def notr(m, n): return 0x6007 | n << 8 | m << 4
def negr(m, n): return 0x600B | n << 8 | m << 4
def mull(m, n): return 0x0007 | n << 8 | m << 4
def mulsw(m, n): return 0x200F | n << 8 | m << 4
def muluw(m, n): return 0x200E | n << 8 | m << 4
def andi(v): return 0xC900 | (v & 0xFF)
def ori(v): return 0xCB00 | (v & 0xFF)
def xori(v): return 0xCA00 | (v & 0xFF)
def cmpeqi(v): return 0x8800 | (v & 0xFF)
def tst(m, n): return 0x2008 | n << 8 | m << 4
def sub_(m, n): return 0x3008 | n << 8 | m << 4
def add_(m, n): return 0x300C | n << 8 | m << 4

def fam_soup(r, f):
    f.add(mov(0, 4), mov(1, 5))
    regs = [0, 1, 2, 3]
    f.add(mov(2, 4), mov(3, 5))
    for _ in range(r.randrange(4, 13)):
        k = r.randrange(0, 18)
        a, b = r.choice(regs), r.choice(regs)
        if k == 0: f.add(r.choice([SHLL, SHLR, SHAR, SHLL2, SHLR2, SHLL8, SHLR8, SHLL16, SHLR16, ROTL, ROTR]) | a << 8)
        elif k == 1: f.add(r.choice([extub, extuw, extsb, extsw])(a, b))
        elif k == 2: f.add(r.choice([negr, notr, swapb, swapw])(b, a))
        elif k == 3: f.add(r.choice([and_, or_, xor, add_, sub_])(b, a))
        elif k == 4: f.add(addi(a, r.choice([1, 2, 7, 0x7F, -1, -2, -0x80])))
        elif k == 5: f.add(r.choice([andi, ori, xori])(r.choice([1, 0x7F, 0x80, 0xFF, 0xF0, 0x0F, 0x55, 0xAA])))   # r0 only
        elif k == 6:
            L = RAND_LITS(r); f.lit(3 if a != 3 else 2, L); c = 3 if a != 3 else 2
            f.add(r.choice([mull, mulsw, muluw])(c, a))
            f.add(stsmacl(a))
        elif k == 7:
            L = RAND_LITS(r); c = 3 if a != 3 else 2; f.lit(c, L)
            f.add(r.choice([dmuls, dmulu])(c, a))
            f.add(stsmach(a) if r.random() < 0.5 else stsmacl(a))
        elif k == 8: f.add(r.choice([cmp_eq, cmp_hi, cmp_hs, cmp_gt, cmp_ge])(b, a), movt(a))
        elif k == 9: f.add(r.choice([cmp_pz, cmp_pl])(a), movt(a))
        elif k == 10: f.add(tst(b, a), movt(a))
        elif k == 11: f.add(cmpeqi(r.choice([0, 1, 0x7F, 0x80, 0xFF])), movt(a))
        elif k == 12: f.add(xtrct(b, a))
        elif k == 13: f.add(CLRT if r.random() < 0.5 else SETT, r.choice([addc, subc])(b, a))
        elif k == 14: f.add(negc(b, a))
        elif k == 15: f.add(r.choice([addv, subv])(b, a), movt(a if r.random() < 0.5 else b))
        elif k == 16: f.add(r.choice([ROTCL, ROTCR, SHLL, SHLR]) | a << 8)
        else: f.add(mov(a, b))
    f.add(mov(0, r.choice(regs)))

def fam_lerp(r, f):
    form = r.randrange(0, 8)
    c = RAND_LITS(r) if r.random() < 0.7 else r.choice([0x7FFF, 0x7FFFFFFF, 0xFFFF, 0x8001, 0x8000, 0xFFFF8001, 0x10001, 0xFFFFFFFF, 0x80000001])
    if form == 0:   # a + ((b - a) * t) >> k  (16.16)
        f.add(mov(0, 5), sub_(4, 0))                  # r0 = b - a
        f.lit(1, c)
        f.add(dmuls(1, 0), stsmach(2), stsmacl(3), xtrct(2, 3), add_(4, 3), mov(0, 3))
    elif form == 1:  # a + ((a * c) >> k) via the high word
        f.lit(1, c)
        f.add((dmuls if r.random() < 0.5 else dmulu)(1, 4), stsmach(0))
        sh = r.choice([0, 1, 2, 8, 16, 31])
        (shr_arith if r.random() < 0.5 else shr_logical)(f, 0, sh)
        f.add(add_(4, 0))
    elif form == 2:  # the word of a halfword product plus the halfword
        f.add(r.choice([extsw, extuw])(1, 4)); f.lit(2, c & 0xFFFF)
        f.add(r.choice([muluw, mulsw])(2, 1), stsmacl(0))
        shr_logical(f, 0, r.choice([8, 15, 16]))
        f.add(add_(1, 0), r.choice([extsw, extuw])(0, 0))
    elif form == 3:  # 32-bit multiply low word then the high half plus the operand
        f.add(r.choice([extsw, extuw, extsb, extub])(1, 4)); f.lit(2, c)
        f.add(mull(2, 1), stsmacl(0))
        shr_logical(f, 0, r.choice([8, 16, 24]))
        f.add(r.choice([add_, sub_])(1, 0))
    elif form == 4:  # signed high word minus its sign (division by multiplication, any constant)
        f.lit(1, c)
        f.add(dmuls(1, 4), stsmach(0), mov(2, 4))
        shr_arith(f, 2, 31)
        f.add(sub_(2, 0))
    elif form == 6:  # the 16-bit signed value times a 32-bit constant, its high half, the sign of that half, plus the value (RuleDivTermAdd / RuleSignForm2 / RuleDivOpt shapes at 2 and 4 bytes)
        f.add(extsw(1, 4)); f.lit(2, c)
        f.add(mull(2, 1), stsmacl(0))
        shr_logical(f, 0, 16); f.add(extsw(0, 0))
        shr_arith(f, 0, r.choice([15, 15, 7, 8]))
        f.add(add_(1, 0))
    elif form == 7:  # the same with the byte, unsigned or signed
        f.add(r.choice([extub, extsb])(1, 4)); f.lit(2, c)
        f.add(mull(2, 1), stsmacl(0))
        shr_logical(f, 0, r.choice([8, 16, 24])); f.add(r.choice([extub, extuw, extsb, extsw])(0, 0))
        shr_arith(f, 0, r.choice([7, 15, 31])) if r.random() < 0.5 else shr_logical(f, 0, r.choice([1, 7, 15]))
        f.add(r.choice([add_, sub_])(1, 0))
    else:           # W + ((x - W) >> 1)
        f.lit(1, c)
        f.add(dmulu(1, 4), stsmach(0), mov(2, 4), sub_(0, 2), SHLR | 2 << 8, add_(0, 2))
        shr_logical(f, 2, r.choice([0, 1, 2, 7]))
        f.add(mov(0, 2))

def fam_narrow(r, f):
    f.add(mov(0, 4))
    tot = r.choice([8, 9, 15, 16, 17, 24, 25, 28, 30, 31, 32, 33, 40, 47, 48, 56, 63, 64, 72])
    k = r.choice([0, 0, 1, 2])
    if k == 0: f.add(extub(0, 0))
    elif k == 1: f.add(extuw(0, 0))
    elif k == 2: f.add(extsb(0, 0))
    left = tot
    while left > 0:                      # the engine's RuleDoubleShift merges the shifts: a total of 32 or more is the zero a lone shift by that much would be
        step = min(left, r.choice([8, 16, 24, 31]))
        shl(f, 0, step); left -= step
    cut = r.choice([extub, extuw, extsb, extsw])
    f.add(cut(0, 0))
    out = r.randrange(0, 5)
    if out == 0: f.add(tst(0, 0), movt(0))
    elif out == 1: f.add(cmpeqi(r.choice([0, 1, 0x7F, 0x80, 0xFF])), movt(0))
    elif out == 2: f.add(cmp_pz(0), movt(0))
    elif out == 3: shr_logical(f, 0, r.choice([1, 8, 15, 16, 24, 31]))
    else:
        f.add(cmp_eq(5, 0)); f.br('bt', 'q'); f.add(movi(0, 1)); f.label('q')

def fam_negshift(r, f):
    f.add(r.choice([extsb, extsw, extub, extuw])(0, 4))
    if r.random() < 0.7: f.add(negr(0, 0))
    else: f.add(notr(0, 0), addi(0, 1))
    n = r.choice([1, 2, 7, 8, 9, 15, 16, 17, 23, 24, 31])
    if r.random() < 0.5: shr_arith(f, 0, n)
    else: shr_logical(f, 0, n)
    k = r.randrange(0, 4)
    if k == 1: f.add(r.choice([extub, extuw, extsb, extsw])(0, 0))
    elif k == 2: f.add(mov(1, 4), addi(1, r.choice([1, -1, 0x7F])), add_(1, 0))
    elif k == 3: f.add(tst(0, 0), movt(0))

def fam_shdiv(r, f):
    # a value shifted right by 1 to 16 (logical or arithmetic), then divided by a constant, signed or unsigned, the divisor from 2 to 2^32-1 with a bias to the large ones:
    # `(x >> n) / d` where d << n does not fit the width (RuleDivChain, RuleDivOpt, RuleShiftCompare on the quotient)
    d = r.choice([65537, 70000, 86400, 1000000, 0x7FFFFFFF, 0x80000001, 0xFFFFFFFF, 0xFFFFFFFE, 0x10000, 0x20000, 0x8000, 255, 257, 641, 1000, 10, 100, 3, 7, 12345678, r.randrange(3, 1 << 32)])
    f.d_hint = None
    f.add(mov(1, 4))
    sh = r.choice([SHLR, SHLR2, SHLR8, SHLR16, SHAR])
    for _ in range(r.choice([1, 1, 2])): f.add(sh | 1 << 8)
    if r.random() < 0.5: emit_udiv(r, f, d, 1, 0, 2)
    else: emit_sdiv(r, f, d if d < 0x80000000 else d - (1 << 32), 1, 0, 2)
    k = r.randrange(0, 3)
    if k == 1: f.add(mov(1, 0), SHLR | 1 << 8, mov(0, 1))
    elif k == 2: f.add(cmp_pz(0), movt(0))

FAMS13 = {55: fam_soup, 56: fam_lerp, 57: fam_narrow, 58: fam_negshift, 59: fam_shdiv}

def build(seed, fam=None, want_fn=False):
    r = random.Random(seed * 104729 + 13)
    f = Fn(r)
    f.d_hint = None
    fm = FAMS13[fam] if fam is not None else FAMS13[r.choice(sorted(FAMS13))]
    fm(r, f)
    f.add(0x000B, 0x0009)
    if want_fn: return f
    return finish(f)

def extras(first, count, fam):
    vals = set()
    for s in range(first, first + count):
        f = build(s, fam, want_fn=True)
        for v in lits_of(f):
            for dv in (-2, -1, 0, 1, 2): vals.add((v + dv) & M32)
            vals.add((-v) & M32)
    return sorted(vals)

if __name__ == "__main__":
    if sys.argv[1] == 'extras':
        first, count, fam = int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4])
        print(",".join("%X" % v for v in extras(first, count, fam)))
    else:
        first, count = int(sys.argv[1]), int(sys.argv[2])
        fam = int(sys.argv[3]) if len(sys.argv) > 3 else None
        out = [g for g in (build(s, fam) for s in range(first, first + count)) if g]
        print(";".join(out))
