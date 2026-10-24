#!/usr/bin/env python3
# Area 2 round 12 instrument. Register-only functions for the printed-C differential, on the carry / overflow / extension rules that rounds
# 8 to 11 had not drawn (loads dagen11.py and through it dagen10.py; their helpers are reused):
#   40 ovfsign   addv / subv / addc / subc of r4 with a literal or a register, the overflow T read by movt, and the sign of the SUM tested on
#                its own (cmp/pz, cmp/pl, shll, shar) and combined with the overflow: V == sign, V != sign (RuleScarry, RuleSborrow),
#                with the extreme literals (0x80000000, 0x7FFFFFFF, 0, 1, -1) on either side
#   41 fixmul    fixed-point multiplies: dmuls.l / dmulu.l, then the middle word by xtrct (the product >> 16), >> 8, >> 15, >> 31, >> 32,
#                the product compared, the high word extended (RulePiece2Zext, RulePiece2Sext, RuleConcatShift, RuleZextEliminate)
#   42 widecmp   a 64-bit value in two registers (addc / subc / negc / mach:macl) compared as 64 bits, by cmp/hi, cmp/hs, cmp/gt, cmp/ge, cmp/eq
#                of the high words and the low words (the carry and borrow rules)
#   44 extcmp    an extended byte or word compared with a literal round the edge of the extension (RuleZextEliminate, RuleZextSless)
#   45 signbias  x plus its sign bit shifted right arithmetically by k = 1 (x / 2, RuleSignDiv2) and by k > 1 (not a division)
#   46 extshift  an extended byte or word shifted right and cut back to a byte or word; (V << 8k) >> 8k (RuleSignForm, RuleLeftRight)
#   48 boolcomb  A && B != A && C, (A && B) || (A && C), (A || B) && (A || C), A <= B && A != B (RuleBooleanUndistribute, RuleBooleanDedup, RuleLessNotEqual)
#   49 threeway  (a < b) + (a <= b) - 1 and its tests against -1, 0, 1 (RuleThreeWayCompare)
#   50 negneg    ~~x (RuleNegateNegate)
#   51 signmod   signed remainder by 2^k with bias and mask (RuleSignMod2nOpt, RuleSignMod2Opt)
#   52 bitpull   a bit or a few high bits of a byte pulled down by masks and shifts (RuleSubvarShift)
#   53 prodsign  the sign of the high word of a product by a small positive constant (RuleSignForm2)
#   54 masksign  a masked or shifted sign (0 or the sign bit) added to or subtracted from the sign bit and its neighbours, the flag and the sign read (RuleSLess2Zero)
#   47 nonneg    a signed division by a constant of a zero-extended or masked dividend (RulePositiveDiv)
#   43 sameval   a value computed twice or compared with itself (RuleTrivialArith): x ^ x, x == x, (x+y) == (x+y), (x*c) != (x*c), tst r,r
# usage: dagen12.py <first seed> <count> [family 40..54]   -> one function per seed, ";" joined (SE_GENERATED_PRINTEDC_WORDS)
#        dagen12.py extras <first seed> <count> <family>   -> the comma-joined hex list for SE_GENERATED_PRINTEDC_EXTRA
import importlib.util, os, random, sys

here = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("dagen11", os.path.join(here, "..", "a2_round11", "dagen11.py"))
d11 = importlib.util.module_from_spec(spec)
spec.loader.exec_module(d11)
globals().update({k: v for k, v in vars(d11).items() if not k.startswith('__')})

EXT = [0x80000000, 0x7FFFFFFF, 0, 1, 0xFFFFFFFF, 0x80000001, 0xFFFFFFFE, 0x40000000, 0xC0000000, 2, 0x100, 0xFFFF, 0x10000, 0xFFFF0000, 0x7FFF, 0x8000]

def addv(m, n): return 0x300F | n << 8 | m << 4
def subv(m, n): return 0x300B | n << 8 | m << 4
def addc(m, n): return 0x300E | n << 8 | m << 4
def subc(m, n): return 0x300A | n << 8 | m << 4
def negc(m, n): return 0x600A | n << 8 | m << 4
CLRT, SETT = 0x0008, 0x0018
def movt(d): return 0x0029 | d << 8
def cmp_pz(n): return 0x4011 | n << 8
def cmp_pl(n): return 0x4015 | n << 8
def cmp_eq(m, n): return 0x3000 | n << 8 | m << 4
def cmp_hi(m, n): return 0x3006 | n << 8 | m << 4
def cmp_hs(m, n): return 0x3002 | n << 8 | m << 4
def cmp_gt(m, n): return 0x3007 | n << 8 | m << 4
def cmp_ge(m, n): return 0x3003 | n << 8 | m << 4
def xor(m, n): return 0x200A | n << 8 | m << 4
def and_(m, n): return 0x2009 | n << 8 | m << 4
def or_(m, n): return 0x200B | n << 8 | m << 4
def xtrct(m, n): return 0x200D | n << 8 | m << 4
def neg(m, n): return 0x600B | n << 8 | m << 4

def fam_ovfsign(r, f):
    """the sum of an operand and a literal (or register), the overflow of it and the sign of it"""
    L = r.choice(EXT) if r.random() < 0.8 else r.getrandbits(32)
    op = r.choice([addv, addv, subv, addc, subc])
    lit_in_n = r.random() < 0.5            # the literal is the register added to (Rn) or the one added (Rm)
    f.add(mov(0, 4))                       # r0 = x
    f.lit(1, L)
    if op in (addc, subc): f.add(CLRT if r.random() < 0.5 else SETT)
    if lit_in_n: f.add(mov(0, 1), mov(1, 4)); f.add(op(1, 0))      # r0 = L (+/-) x, written op(Rm = x, Rn = L)
    else: f.add(op(1, 0))                                           # r0 = x (+/-) L
    f.add(movt(2))                          # r2 = V
    sk = r.randrange(0, 5)
    if sk == 0: f.add(cmp_pz(0), movt(3))                      # r3 = sum >= 0
    elif sk == 1: f.add(cmp_pl(0), movt(3))                    # r3 = sum > 0
    elif sk == 2: f.add(mov(3, 0), SHLL | 3 << 8, 0x0029 | 3 << 8)   # r3 = sign of sum (the shll carries it to T)
    elif sk == 3: f.add(mov(3, 0)); shr_arith(f, 3, 31); f.add(0x2008 | 3 << 8 | 3 << 4, movt(3))   # r3 = (sum >> 31) == 0
    else: f.add(mov(3, 0)); shr_logical(f, 3, 31)           # r3 = sign bit
    form = r.randrange(0, 6)
    if form == 0: f.add(cmp_eq(2, 3), movt(0))                  # V == r3
    elif form == 1: f.add(xor(2, 3), mov(0, 3))                 # V ^ r3
    elif form == 2: f.add(cmp_eq(2, 3)); f.br('bt', 'ye'); f.add(movi(0, r.choice([0, 5]))); f.br('bra', 'en'); f.add(0x0009); f.label('ye'); f.add(movi(0, r.choice([1, 9]))); f.label('en')
    elif form == 3: f.add(or_(2, 3), mov(0, 3))                 # V | r3
    elif form == 4: f.add(and_(2, 3), mov(0, 3))
    else: f.add(mov(0, 2))   # V alone

def fam_fixmul(r, f):
    signed = r.random() < 0.6
    a = 4
    if r.random() < 0.3: f.add(mov(1, 5)); lit_or_imm(f, r, 1, r.choice(EXT + [0x10000, 0x8000, 0x4000, 0xB504, 0x1999A, 0x6487E])); b = 1
    else: b = 5
    f.add((dmuls if signed else dmulu)(b, a))
    f.add(stsmach(1), stsmacl(0))
    k = r.randrange(0, 9)
    if k == 0: f.add(xtrct(1, 0))                          # >> 16
    elif k == 1: f.add(xtrct(1, 0)); shr_arith(f, 0, 7) if signed else shr_logical(f, 0, 7)   # >> 23
    elif k == 2: f.add(xtrct(1, 0)); shl(f, 0, 1)         # >> 15 (low bit lost)
    elif k == 3: f.add(mov(0, 1))                          # high word
    elif k == 4: f.add(mov(0, 1)); (shr_arith if signed else shr_logical)(f, 0, r.choice([1, 8, 16, 31]))
    elif k == 5:                                           # high word compared with 0 / -1 / sign of low
        f.add(cmp_pz(1) if signed else 0x2008 | 1 << 8 | 1 << 4, movt(0))
    elif k == 6:                                           # 64-bit result tested against a literal in both halves
        lit_or_imm(f, r, 2, r.choice(EXT)); f.add(cmp_eq(2, 1), movt(3)); lit_or_imm(f, r, 2, r.choice(EXT)); f.add(cmp_eq(2, 0), movt(0), and_(3, 0))
    elif k == 7:                                           # does the product fit 32 bits: high == low >> 31
        if signed: f.add(mov(2, 0)); shr_arith(f, 2, 31); f.add(cmp_eq(1, 2), movt(0))
        else: f.add(0x2008 | 1 << 8 | 1 << 4, movt(0))
    else: f.add(xtrct(1, 0), extuw(0, 0) if r.random() < 0.5 else extsw(0, 0))

def fam_widecmp(r, f):
    """a 64-bit value in r1:r0 built from r4/r5 and literals by carries, then ordered against a 64-bit literal"""
    f.add(mov(0, 4), mov(1, 5))
    L0, L1 = r.choice(EXT), r.choice(EXT)
    f.lit(2, L0); f.lit(3, L1)
    k = r.randrange(0, 4)
    if k == 0: f.add(CLRT, addc(2, 0), addc(3, 1))
    elif k == 1: f.add(CLRT, subc(2, 0), subc(3, 1))
    elif k == 2: f.add(CLRT, negc(0, 0), negc(1, 1))
    else: f.add(CLRT, addc(0, 0), addc(1, 1))              # shifted left by one as 64 bits
    # r1:r0 vs L3:L2 (reload the literals)
    f.lit(2, r.choice(EXT)); f.lit(3, r.choice(EXT))
    cmpk = r.choice([cmp_hi, cmp_hs, cmp_gt, cmp_ge])
    f.add(cmpk(3, 1), movt(5))                              # high words ordered
    f.add(cmp_eq(3, 1), movt(6))                            # high words equal
    f.add(cmp_hi(2, 0), movt(7))                            # low words ordered (unsigned)
    form = r.randrange(0, 3)
    if form == 0: f.add(and_(6, 7), or_(7, 5), mov(0, 7))   # high > || (high == && low >)
    elif form == 1: f.add(mov(0, 5), xor(6, 0))
    else:
        f.add(cmp_eq(3, 1)); f.br('bf', 'hi'); f.add(mov(0, 7)); f.br('bra', 'end'); f.add(0x0009)
        f.label('hi'); f.add(mov(0, 5)); f.label('end')

def fam_sameval(r, f):
    k = r.randrange(0, 8)
    f.add(mov(0, 4), mov(1, 5))
    if k == 0: f.add(mov(2, 4), xor(4, 2), mov(0, 2))      # x ^ x
    elif k == 1: f.add(mov(2, 4), add(2, 5), mov(3, 4), add(3, 5), cmp_eq(2, 3), movt(0))     # (x+y) == (x+y)
    elif k == 2: f.lit(2, r.choice(EXT)); f.add(0x0007 | 4 << 8 | 2 << 4, stsmacl(3), 0x0007 | 4 << 8 | 2 << 4, stsmacl(0), cmp_eq(3, 0), movt(0))   # (x*c) == (x*c)
    elif k == 3: f.add(extub(2, 4), extub(3, 4), cmp_hi(3, 2), movt(0))                         # (x&ff) u< (x&ff)
    elif k == 4: f.add(extsb(2, 4), extsb(3, 4), cmp_ge(3, 2), movt(0))
    elif k == 5: f.add(mov(2, 4)); shl(f, 2, 3); f.add(mov(3, 4)); shl(f, 3, 3); f.add(cmp_gt(3, 2), movt(0))
    elif k == 6: f.add(and_(5, 4), mov(0, 4), mov(2, 4), and_(5, 2), or_(2, 0))                 # x & y | x & y
    else: f.add(mov(2, 4), neg(2, 2), mov(3, 4), neg(3, 3), cmp_hs(3, 2), movt(0))

def fam_extcmp(r, f):
    """an extended byte or word compared with a literal at, just under and just over the edge of what the extension can hold, and with a
    negative one (RuleZextEliminate, RuleZextSless, RuleSextSless: zext(V) < c with c wider than V)"""
    kind = r.choice([0, 1, 2, 3])
    f.add([extub, extsb, extuw, extsw][kind](0, 4))
    if r.random() < 0.3: f.add(addi(0, r.choice([1, 0x7F, 0x80, 0xFF, 0xFE])))
    size = 8 if kind < 2 else 16
    top = 1 << size
    edge = [0, 1, 0x7F, 0x80, 0xFF, 0x7FFF, 0x8000, 0xFFFF, top - 1, top, top + 1, top + 0x7F, top + 0x80, top + 0xFF, 2 * top - 1, 2 * top, 0x7FFFFFFF, 0x80000000,
            0xFFFFFFFF, 0xFFFFFF80, 0xFFFFFF7F, 0xFFFFFF00, 0xFFFF8000, 0xFFFF7FFF, 0xFFFF0000, (-top) & M32, (-top - 1) & M32, (1 << 31) | top, 0x100, 0x101, 0x1FF, 0x200, 0x10000, 0x10001]
    L = r.choice(edge) & M32
    lit_or_imm(f, r, 1, L)
    nm = r.choice(['eq', 'hs', 'hi', 'ge', 'gt'])
    if nm == 'eq' or r.random() < 0.5: f.add(CMPS[nm] | 0 << 8 | 1 << 4)
    else: f.add(CMPS[nm] | 1 << 8 | 0 << 4)
    result_of_t(r, f, 0)

def fam_signbias(r, f):
    """x plus its sign bit (0 or 1: not the 2^k - 1 bias of a division by 2^k), shifted right arithmetically by k. `(x + (x >>> 31)) s>> 1` is x / 2
    and RuleSignDiv2 rewrites it; for k above 1 it is no division (a mutant of the rule that takes a shift by 2 prints x / 2 for `(x + sign) >> 2`)"""
    k = r.choice([1, 1, 2, 2, 3, 4, 5, 7, 8, 15, 16, 24, 31])
    src = 4
    if r.random() < 0.25: f.add(r.choice([extsb, extsw])(1, 4)); src = 1
    form = r.randrange(0, 4)
    f.add(mov(2, src))
    if form == 0: shr_logical(f, 2, 31); f.add(add(2, src))                               # x + (x >>> 31)
    elif form == 1: shr_arith(f, 2, 31); f.add(neg(2, 2), add(2, src))                    # x - (x s>> 31)
    elif form == 2: f.add(cmp_pz(src), movt(2), movi(3, 1), xor(3, 2), add(2, src))       # x + !(x >= 0)
    else: f.add(SHLL | 2 << 8, movt(2), add(2, src))                                      # the sign into T by shll, x + T (r2 was a copy of x: its sign bit)
    if form == 3: f.add(0x0009)
    shr_arith(f, 2, k)
    t = r.randrange(0, 4)
    if t == 0: f.add(mov(0, 2))
    elif t == 1: shl(f, 2, k); f.add(mov(0, src), sub(0, 2))                            # x - ((x + sign) >> k << k)
    elif t == 2: f.add(mov(0, 2), add(0, 2))
    else: f.add(cmp_pl(2), movt(0))

def fam_extshift(r, f):
    """a byte or word extended, shifted right (arithmetic or logical) by a multiple of 8 or any amount, and cut back to a byte or word:
    sub(sext(V), c) with c the size of V (the sign: RuleSignForm) or one short of it (the top byte of V, which a mutant of the rule takes for the sign),
    and (V << 8k) >> 8k (RuleLeftRight)"""
    form = r.randrange(0, 4)
    if r.random() < 0.3:         # a word sign-extended, its top byte shifted down as 32 bits (shlr8 or eight shar), cut back to the word: sub(sext(V), 1) of a
        f.add(extsw(0, 4))       # two-byte V, one short of the sign (RuleSignForm's mutant takes it for the sign: 0 or -1)
        (shr_arith if r.random() < 0.4 else shr_logical)(f, 0, 8)
        f.add(r.choice([extuw, extsw])(0, 0))
        return
    if form < 3:
        kind = r.randrange(0, 4)
        f.add([extsb, extsw, extub, extuw][kind](0, 4))
        n = r.choice([1, 7, 8, 9, 15, 16, 17, 24, 31, 23, 6, 12])
        (shr_arith if (r.random() < 0.65 and form != 2) else shr_logical)(f, 0, n)
        cut = r.choice([extub, extsb, extuw, extsw, None, None])
        if cut: f.add(cut(0, 0))
    else:
        n = r.choice([8, 16, 24])
        f.add(mov(0, 4)); shl(f, 0, n)
        (shr_arith if r.random() < 0.5 else shr_logical)(f, 0, n)
    if r.random() < 0.25: f.add(addi(0, r.choice([1, 0x7F, 0xFF])))

def fam_nonneg(r, f):
    """a signed division by a constant of a value known to be non-negative (a zero-extended byte or word, a masked or shifted register):
    RulePositiveDiv turns the signed division into the unsigned one"""
    d = r.choice([3, 5, 6, 7, 9, 10, 12, 25, 100, 1000, 3600, 641])
    k = r.randrange(0, 4)
    if k == 0: f.add(extub(1, 4))
    elif k == 1: f.add(extuw(1, 4))
    elif k == 2: f.add(mov(1, 4)); shr_logical(f, 1, r.choice([1, 2, 8, 16]))
    else: f.lit(1, 0x7FFFFFFF); f.add(and_(4, 1))
    emit_sdiv(r, f, d, 1, 0, 2)
    f.d_hint = ('sdiv', d)

def tbit(f, name, x, y, dst, invert=False):
    """dst = (x name y) as 0/1 ('ne' by eq and an inversion; invert flips any)"""
    if name == 'ne': f.add(CMPS['eq'] | x << 8 | y << 4, movt(dst)); inv = not invert
    else: f.add(CMPS[name] | x << 8 | y << 4, movt(dst)); inv = invert
    if inv: f.add(movi(3, 1), xor(3, dst))

def fam_boolcomb(r, f):
    """booleans from compares combined as the boolean rules read them: A && B != A && C, (A && B) || (A && C), (A || B) && (A || C),
    (A || B) || (!A && C), A <= B && A != B (RuleBooleanUndistribute, RuleBooleanDedup, RuleLessNotEqual), by and/or/xor of movt results
    or by branches"""
    ops = ['eq', 'ne', 'ge', 'gt', 'hi', 'hs']
    pairs = [(4, 5), (5, 4), (4, 6), (5, 6)]
    form = r.randrange(0, 9)
    A = (r.choice(ops),) + r.choice(pairs)
    B = (r.choice(ops),) + r.choice(pairs)
    C = (r.choice(ops),) + r.choice(pairs)
    if form == 8:      # the same pair: x <= y (or <) and x != y
        x, y = r.choice([(4, 5), (5, 4), (4, 6)])
        A = (r.choice(['ge', 'hs']), y, x); B = ('ne', x, y)
    tbit(f, *A, 0); tbit(f, *B, 1); tbit(f, *C, 2)
    # r0 = A, r1 = B, r2 = C as 0/1
    if form == 0: f.add(mov(5, 0), and_(1, 5), mov(6, 0), and_(2, 6), or_(6, 5), mov(0, 5))                       # (A&B)|(A&C)
    elif form == 1: f.add(mov(5, 0), or_(1, 5), mov(6, 0), or_(2, 6), and_(6, 5), mov(0, 5))                      # (A|B)&(A|C)
    elif form == 2: f.add(mov(5, 0), or_(1, 5), movi(3, 1), xor(3, 0), and_(2, 0), or_(0, 5), mov(0, 5))         # (A|B)|(!A&C)
    elif form == 3: f.add(mov(5, 0), and_(1, 5), mov(6, 0), and_(2, 6), and_(6, 5), mov(0, 5))                    # (A&B)&(A&C)
    elif form == 4: f.add(mov(5, 0), or_(1, 5), mov(6, 0), or_(2, 6), or_(6, 5), mov(0, 5))                       # (A|B)|(A|C)
    elif form == 5: f.add(mov(5, 0), and_(1, 5), mov(6, 0), and_(2, 6), cmp_eq(6, 5), movt(0), xor_imm1(0))         # (A&B) != (A&C)
    elif form == 6: f.add(mov(5, 0), or_(1, 5), mov(6, 0), or_(2, 6), cmp_eq(6, 5), movt(0))                        # (A|B) == (A|C)
    elif form == 7: f.add(mov(5, 0), and_(1, 5), mov(6, 0), and_(2, 6), cmp_eq(6, 5), movt(0))                      # (A&B) == (A&C)
    else: f.add(and_(1, 0))                                                                                      # A && B
    if r.random() < 0.4:      # the same by branches on the final value
        f.add(0x2008 | 0 << 8 | 0 << 4); f.br('bt', 'z'); f.add(movi(0, r.choice([1, 7]))); f.br('bra', 'e'); f.add(0x0009); f.label('z'); f.add(movi(0, r.choice([0, 3]))); f.label('e')

def xor_imm1(d): return 0xCA01 if d == 0 else 0x0009         # xor #1,r0

def fam_threeway(r, f):
    """the comparison function -1 / 0 / 1: (a < b) + (a <= b) - 1, signed or unsigned, then tested against -1, 0, 1 or ordered
    (RuleThreeWayCompare)"""
    signed = r.random() < 0.5
    lt, le = (cmp_gt, cmp_ge) if signed else (cmp_hi, cmp_hs)
    a, b = r.choice([(4, 5), (5, 4)])
    f.add(lt(a, b), movt(1), le(a, b), movt(2), add(1, 2), addi(1, 0xFF))        # r1 = (b lt a) + (b le a) - 1: Rn op Rm: lt(a,b): T = b > a
    k = r.randrange(0, 7)
    if k == 0: f.add(mov(0, 1))
    elif k == 1: f.add(0x4011 | 1 << 8, movt(0))                                  # cmp/pz r1
    elif k == 2: f.add(0x4015 | 1 << 8, movt(0))                                  # cmp/pl r1
    elif k == 3: f.add(0x4015 | 1 << 8, movt(0), xor_imm1(0))                      # <= 0
    elif k == 4: f.add(movi(3, r.choice([0xFF, 0, 1])), cmp_eq(3, 1), movt(0))   # == -1 / 0 / 1
    elif k == 5: f.add(movi(3, r.choice([0xFF, 0, 1])), cmp_gt(3, 1), movt(0))   # r1 > c
    else: f.add(movi(3, r.choice([0xFF, 0, 1])), cmp_gt(1, 3), movt(0))          # c > r1

def fam_negneg(r, f):
    """~~x and chains of not / neg / not (RuleNegateNegate)"""
    n = r.choice([2, 2, 3, 4])
    f.add(mov(0, 4))
    for _ in range(n): f.add(0x6007 | 0 << 8 | 0 << 4)                           # not r0,r0
    if r.random() < 0.5: f.add(0x2009 | 0 << 8 | 5 << 4)

def fam_signmod(r, f):
    """the signed remainder by 2^k written with the bias and a mask, (x + bias) & (2^k - 1) - bias, and for k = 1 (x - sign) & 1 + sign
    (RuleSignMod2nOpt, RuleSignMod2Opt)"""
    k = r.choice([1, 1, 2, 3, 4, 5, 8, 12, 16])
    src = 4
    if r.random() < 0.25: f.add(r.choice([extsb, extsw])(1, 4)); src = 1
    form = r.randrange(0, 3)
    f.add(mov(2, src)); shr_arith(f, 2, 31)                                       # r2 = sign mask (0 / -1)
    if form == 1:
        f.add(mov(0, src), sub(0, 2), movi(1, 1), and_(1, 0), add(0, 2))          # ((x - sign) & 1) + sign
    else:
        f.add(mov(3, 2)); shr_logical(f, 3, 32 - k)                                # r3 = bias
        f.add(mov(0, src), add(0, 3)); lit_or_imm(f, r, 1, (1 << k) - 1); f.add(and_(1, 0), sub(0, 3))     # ((x + bias) & mask) - bias
        if form == 2: f.add(0x2008 | 0 << 8 | 0 << 4, movt(0))                    # the remainder is zero

def fam_bitpull(r, f):
    """a bit or a few high bits of a byte or a register pulled down: a mask, a shift right by the position of the mask, an extension, a test or a
    comparison (RuleSubvarShift: a one-byte value whose only possible bit is shifted to bit 0; RuleSubvarAnd; RuleSubvarZext)"""
    kind = r.randrange(0, 4)
    f.add([extub, extsb, mov, extuw][kind](0, 4))
    for _ in range(r.randrange(1, 4)):
        k = r.randrange(0, 7)
        if k == 0: f.add(0xC900 | r.choice([0x80, 0xC0, 0x40, 0x01, 0x02, 0x20, 0x08, 0xF0, 0x0F]))        # and #imm,r0
        elif k == 1: shr_logical(f, 0, r.choice([1, 2, 3, 5, 6, 7]))
        elif k == 2: shr_arith(f, 0, r.choice([1, 7]))
        elif k == 3: f.add(extub(0, 0))
        elif k == 4: shl(f, 0, r.choice([1, 2, 7]))
        elif k == 5: f.add(0xCB00 | r.choice([0x01, 0x80]))                                            # or #imm,r0
        else: f.add(0xC800 | r.choice([0x80, 0x01, 0xC0]), movt(0))                                    # tst #imm,r0 ; movt
    if r.random() < 0.3: f.add(extub(0, 0))

def fam_prodsign(r, f):
    """the sign of the high word of a product by a small positive constant, `dmuls.l ; sts mach ; shar 31`: sub(sext(x) * c, 4) s>> 31 (RuleSignForm2)"""
    c = r.choice([2, 3, 5, 7, 10, 100, 1000, 0x7FFF, 0x10000, 0xFFFF, 0x10001, 0x7FFFFFFF])
    f.lit(1, c)
    f.add(dmuls(1, 4) if r.random() < 0.5 else dmuls(4, 1), stsmach(0))
    k = r.randrange(0, 3)
    if k == 0: shr_arith(f, 0, 31)
    elif k == 1: shr_arith(f, 0, 31); f.add(0x2009 | 0 << 8 | 5 << 4)
    else: f.add(cmp_pz(0), movt(0))

def fam_masksign(r, f):
    """a value that can only be 0 or the sign bit (a masked sign, a shifted one, a movt shifted up) met by addv / subv / addc / subc / cmp with a
    constant that is the sign bit or its neighbour, the flag and the sign of the result read (RuleSLess2Zero's high-bit rule: both operands of the sum
    can set the high bit)"""
    k = r.randrange(0, 4)
    f.add(mov(1, 4))
    if k == 0: f.lit(2, 0x80000000); f.add(and_(2, 1))                           # x & 0x80000000
    elif k == 1: shl(f, 1, 31)                                                    # x << 31
    elif k == 2: f.add(0x0029 | 1 << 8); shl(f, 1, 31)                            # movt << 31
    else: shr_logical(f, 1, 31); shl(f, 1, 31)                                   # (x >> 31) << 31
    c = r.choice([0x80000000, 0x80000000, 0x7FFFFFFF, 0xFFFFFFFF, 1, 0xC0000000, 0])
    f.lit(3, c)
    op = r.choice([addv, subv, addc, subc, add, sub])
    if op in (addc, subc): f.add(CLRT if r.random() < 0.5 else SETT)
    f.add(mov(0, 1) if r.random() < 0.5 else mov(2, 1))
    if r.random() < 0.5: f.add(op(3, 1))                                         # r1 = r1 op r3
    else: f.add(mov(2, 3), op(1, 2), mov(1, 2))                                 # r3 op r1 (the masked value second)
    out = r.randrange(0, 4)
    if out == 0: f.add(movt(0))
    elif out == 1: f.add(cmp_pz(1), movt(0))
    elif out == 2: f.add(movt(2), cmp_pl(1), movt(3), xor(2, 3), mov(0, 3))
    else:
        f.add(mov(0, 1)); f.br('bt', 'sk'); f.add(movi(0, 5)); f.label('sk')

FAMS12 = {40: fam_ovfsign, 41: fam_fixmul, 42: fam_widecmp, 43: fam_sameval, 44: fam_extcmp, 45: fam_signbias, 46: fam_extshift, 47: fam_nonneg, 48: fam_boolcomb, 49: fam_threeway, 50: fam_negneg, 51: fam_signmod, 52: fam_bitpull, 53: fam_prodsign, 54: fam_masksign}

def build(seed, fam=None, want_fn=False):
    r = random.Random(seed * 104729 + 12)
    f = Fn(r)
    f.d_hint = None
    fm = FAMS12[fam] if fam is not None else FAMS12[r.choice(sorted(FAMS12))]
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
