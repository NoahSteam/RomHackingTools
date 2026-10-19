#!/usr/bin/env python3
# Area 2 round 11 instrument. Register-only functions for the printed-C differential, on the constant arithmetic and the comparison
# folding of the engine's rules (nothing here is drawn by dagen10.py's families 0..19, which this file loads and extends):
#   20 cmpbyte   a byte (or word, or whole register) reduced, shifted/offset by immediates, then compared with a literal in one of the SH-2's
#                ways; the result is a return value, a branch over two constants, or movt. Read with SE_GENERATED_PRINTEDC_BYTES=1
#                (every byte value as r4 and r5) the byte-wide ones are checked for every input
#   21 udiv      unsigned division by a constant, as a compiler writes it for the SH-2 (dmulu.l, sts mach, shifts, with and without the
#                add fix-up; a remainder by multiply and subtract)
#   22 sdiv      signed division and remainder by a constant (dmuls.l, the sign correction)
#   23 div1      div0u/div0s and div1 chains, rotcl before each, 1..32 steps; a division as the library routine inlines it
#   24 dmul      dmuls.l / dmulu.l / mul.l / muls.w / mulu.w, then mach and macl read in combinations (the high word)
#   25 shifts    chains of constant shifts and rotates (shll/shlr/shar/rotl/rotr/rotcl/rotcr, 2/8/16), and a variable shift as a counted loop
#   26 addcchain addc/subc/negc chains (64-bit add, subtract, negate, compare), both halves returned or combined
#   27 signtest  sign and zero tests: cmp/pz, cmp/pl, shar #31, shlr #31, abs, min/max and clamp by branches, tst of a mask
#   28 rangetst  range checks, `lo <= x && x < hi`, `(x - lo) <u (hi - lo)`, and || forms, on the byte, word or register
# usage: dagen11.py <first seed> <count> [family 0..28]   -> one function per seed, ";" joined (SE_GENERATED_PRINTEDC_WORDS)
#        dagen11.py extras <first seed> <count> <family>   -> the comma-joined hex list for SE_GENERATED_PRINTEDC_EXTRA: the literals of those
#                                                            functions and their neighbours (literal-1, +1, and quotient boundaries for 21/22)
import importlib.util, os, random, sys

here = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("dagen10", os.path.join(here, "..", "a2_round10", "dagen10.py"))
d10 = importlib.util.module_from_spec(spec)
spec.loader.exec_module(d10)
Fn, finish = d10.Fn, d10.finish

M32 = 0xFFFFFFFF

def mov(d, s): return 0x6003 | d << 8 | s << 4
def add(d, s): return 0x300C | d << 8 | s << 4
def sub(d, s): return 0x3008 | d << 8 | s << 4
def movi(d, v): return 0xE000 | d << 8 | (v & 0xFF)
def addi(d, v): return 0x7000 | d << 8 | (v & 0xFF)
def extub(d, s): return 0x600C | d << 8 | s << 4
def extuw(d, s): return 0x600D | d << 8 | s << 4
def extsb(d, s): return 0x600E | d << 8 | s << 4
def extsw(d, s): return 0x600F | d << 8 | s << 4
def stsmach(d): return 0x000A | d << 8
def stsmacl(d): return 0x001A | d << 8
def dmulu(m, n): return 0x3005 | n << 8 | m << 4
def dmuls(m, n): return 0x300D | n << 8 | m << 4
SHLL, SHLR, SHAR = 0x4000, 0x4001, 0x4021
SHLL2, SHLR2, SHLL8, SHLR8, SHLL16, SHLR16 = 0x4008, 0x4009, 0x4018, 0x4019, 0x4028, 0x4029
ROTL, ROTR, ROTCL, ROTCR = 0x4004, 0x4005, 0x4024, 0x4025

def shr_logical(f, d, n):
    """d >>= n (logical) with the SH-2's fixed shifts"""
    while n >= 16: f.add(SHLR16 | d << 8); n -= 16
    while n >= 8: f.add(SHLR8 | d << 8); n -= 8
    while n >= 2: f.add(SHLR2 | d << 8); n -= 2
    while n >= 1: f.add(SHLR | d << 8); n -= 1

def shr_arith(f, d, n):
    for _ in range(n): f.add(SHAR | d << 8)

def shl(f, d, n):
    while n >= 16: f.add(SHLL16 | d << 8); n -= 16
    while n >= 8: f.add(SHLL8 | d << 8); n -= 8
    while n >= 2: f.add(SHLL2 | d << 8); n -= 2
    while n >= 1: f.add(SHLL | d << 8); n -= 1

def sx32(v): return v - (1 << 32) if v & 0x80000000 else v

def lit_or_imm(f, r, d, v):
    """d = v, as an immediate when it fits a sign-extended byte, else from the pool"""
    v &= M32
    if v < 0x80 or v >= 0xFFFFFF80: f.add(movi(d, v))
    else: f.lit(d, v)

# ---------------------------------------------------------------------------------------------------------------- 20 cmpbyte
CMPS = {  # name: (word for `cmp/xx Rm,Rn` with T = Rn op Rm, signed?)
    'eq': 0x3000, 'hs': 0x3002, 'ge': 0x3003, 'hi': 0x3006, 'gt': 0x3007}
BYTE_EDGES = [0, 1, 2, 3, 0x7E, 0x7F, 0x80, 0x81, 0xFE, 0xFF, 0x40, 0x3F, 0xC0, 0xBF, 0x10, 0x0F, 0xF0, 0xF1, 0x20, 0xE0]

def reduce_in(r, f, d, s, kind):
    if kind == 0: f.add(extub(d, s))
    elif kind == 1: f.add(extsb(d, s))
    elif kind == 2: f.add(extuw(d, s))
    elif kind == 3: f.add(extsw(d, s))
    else: f.add(mov(d, s))

def midops(r, f, d):
    for _ in range(r.randrange(0, 4)):
        k = r.randrange(0, 10)
        if k < 3: f.add(addi(d, r.choice([1, 2, 3, 5, 0x7F, 0x80, 0xFF, 0xFE, 0xFD, 0xF0, 0x10, 0x40, 0xC0])))
        elif k == 3: f.add(0x600B | d << 8 | d << 4)         # neg
        elif k == 4: f.add(0x6007 | d << 8 | d << 4)         # not
        elif k == 5: f.add(r.choice([SHLL, SHLR, SHAR, SHLL2, SHLR2]) | d << 8)
        elif k == 6 and d == 0: f.add(0xCA00 | r.choice([0x80, 0xFF, 0x0F, 0xF0, 0x01, 0x7F]))   # xor #imm,r0
        elif k == 7 and d == 0: f.add(0xC900 | r.choice([0xFF, 0x7F, 0xFE, 0x0F, 0xF0, 0x80, 0x3F]))  # and #imm,r0
        elif k == 8 and d == 0: f.add(0xCB00 | r.choice([0x01, 0x80, 0x0F, 0xF0, 0x7F]))     # or #imm,r0
        else: f.add(extub(d, d) if r.random() < 0.5 else extsb(d, d))

def lit_for(r, kind):
    if kind in (0, 1):
        return r.choice(BYTE_EDGES + [r.randrange(0, 256) for _ in range(6)])
    if kind in (2, 3):
        return r.choice([0, 1, 0x7FFF, 0x8000, 0xFFFF, 0x7FFE, 0x8001, 0xFFFE, 0x100, 0xFF, 0x1FF, 0xFF00, 0x7F00, 0x80, 0x7F] + [r.randrange(0, 65536) for _ in range(4)])
    return r.choice([0, 1, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF, 0x80000001, 0x7FFFFFFE, 0xFFFFFFFE, 0x10000, 0xFFFF, 0xFFFF0000, 0x100, 0xFF] + [r.getrandbits(32) for _ in range(4)])

def cmp_to_t(r, f, kind, rx, rl):
    """T := compare of rx and rl (both registers); one of the five compares, either order"""
    name = r.choice(list(CMPS))
    if name == 'eq' or r.random() < 0.5: f.add(CMPS[name] | rx << 8 | rl << 4)       # T = rx op rl
    else: f.add(CMPS[name] | rl << 8 | rx << 4)                                       # T = rl op rx
    return name

def result_of_t(r, f, d=0):
    """r0 := a function of T"""
    k = r.randrange(0, 5)
    if k == 0: f.add(0x0029 | d << 8)                                    # movt
    elif k == 1: f.add(0x0029 | d << 8, 0xC900 | 1)
    elif k == 2:
        a, b = r.choice([(1, 0), (0, 1), (5, 9), (0x7F, 0xFF), (3, 0x80)])
        f.add(movi(d, a)); f.br('bt', 'tt'); f.add(movi(d, b)); f.label('tt')
    elif k == 3:
        a, b = r.choice([(1, 0), (2, 7), (0xFF, 0)])
        f.add(movi(d, a)); f.br('bf', 'tt'); f.add(movi(d, b)); f.label('tt')
    else:
        f.add(0x0029 | d << 8, 0x600B | d << 8 | d << 4)                # movt ; neg

def fam_cmpbyte(r, f):
    kind = r.choice([0, 0, 1, 1, 2, 3, 4, 4])
    reduce_in(r, f, 0, 4, kind)
    midops(r, f, 0)
    two = r.random() < 0.3
    if two:
        reduce_in(r, f, 2, 5, kind)
        cmp_to_t(r, f, kind, 0, 2)
        result_of_t(r, f, 0)
        return
    shape = r.randrange(0, 7)
    L = lit_for(r, kind)
    if shape < 4:
        lit_or_imm(f, r, 1, L)
        cmp_to_t(r, f, kind, 0, 1)
        result_of_t(r, f, 0)
    elif shape == 4:                           # cmp/eq #imm,r0
        f.add(0x8800 | r.choice(BYTE_EDGES) & 0xFF)
        result_of_t(r, f, 0)
    elif shape == 5:                           # two sequential tests, the second only when the first fell through
        lit_or_imm(f, r, 1, L)
        cmp_to_t(r, f, kind, 0, 1)
        f.br('bt', 'out1')
        lit_or_imm(f, r, 2, lit_for(r, kind))
        cmp_to_t(r, f, kind, 0, 2)
        f.br('bf', 'out0')
        f.label('out1'); f.add(movi(0, r.choice([1, 3, 0x7F]))); f.br('bra', 'end'); f.add(0x0009)
        f.label('out0'); f.add(movi(0, r.choice([0, 2, 0x55])))
        f.label('end')
    else:                                      # cmp/pz, cmp/pl, tst #imm
        f.add(r.choice([0x4011, 0x4015]))
        result_of_t(r, f, 0)

# ---------------------------------------------------------------------------------------------------------------- 21 udiv
def umagic(d):
    """(m, s, add): q = mulhu(m, x) >> s for all 32-bit x if add is False, else the add form"""
    for s in range(0, 32):
        m = -((-(1 << (32 + s))) // d)            # ceil
        if m < (1 << 32) and m * d - (1 << (32 + s)) <= (1 << s):
            return m, s, False
    l = (d - 1).bit_length()
    m = ((1 << 32) * ((1 << l) - d)) // d + 1
    return m, l - 1, True

def emit_udiv(r, f, d, src, dst, tmp):
    """dst = src / d unsigned, d >= 2, not a power of two (power of two: a shift)"""
    if d & (d - 1) == 0:
        n = d.bit_length() - 1
        f.add(mov(dst, src)); shr_logical(f, dst, n); return
    m, s, addf = umagic(d)
    lit_or_imm(f, r, tmp, m)
    f.add(dmulu(tmp, src))                        # mach:macl = src * m (tmp is the multiplier)
    f.add(stsmach(dst))
    if not addf:
        shr_logical(f, dst, s)
    else:
        f.add(mov(tmp, src), sub(tmp, dst))        # tmp = src - t
        f.add(SHLR | tmp << 8)
        f.add(add(tmp, dst))                       # tmp = t + ((src - t) >> 1)
        shr_logical(f, tmp, s)
        f.add(mov(dst, tmp))

DIVISORS = [3, 5, 6, 7, 9, 10, 11, 12, 13, 14, 15, 17, 19, 21, 25, 31, 60, 100, 127, 255, 257, 641, 1000, 1023, 3600, 10000, 65535, 65537, 86400, 1000000, 0x7FFFFFFF, 0x80000001, 0xFFFFFFFF, 0xFFFFFFFE, 0x40000001, 4, 8, 2, 16, 0x80000000, 1]

def pick_divisor(r):
    k = r.randrange(0, 10)
    if k < 6: return r.choice(DIVISORS)
    if k < 8: return r.randrange(2, 70)
    if k < 9: return r.randrange(2, 70000)
    return r.getrandbits(r.randrange(2, 32)) | 2

def fam_udiv(r, f):
    d = pick_divisor(r)
    f.d_hint = ('udiv', d)
    if d < 2: d = 3
    pre = r.randrange(0, 4)
    src = 4
    if pre == 1: f.add(extuw(1, 4)); src = 1
    elif pre == 2: f.add(extub(1, 4)); src = 1
    emit_udiv(r, f, d, src, 0, 2)
    k = r.randrange(0, 4)
    if k == 1:                                     # remainder: src - q * d
        lit_or_imm(f, r, 3, d)
        f.add(0x0007 | 3 << 8 | 0 << 4, stsmacl(3))        # mul.l r0,r3 : macl = d * q
        f.add(mov(0, src), sub(0, 3))
    elif k == 2:                                   # compare the quotient (RuleLess / range rules on a division)
        lit_or_imm(f, r, 3, r.choice([0, 1, 2, 5, 9, 10, 100, 0xFF, 0x10000, 0x7FFFFFFF]))
        f.add(0x3006 | 0 << 8 | 3 << 4)            # cmp/hi r3,r0
        result_of_t(r, f, 0)

# ---------------------------------------------------------------------------------------------------------------- 22 sdiv
def smagic(d):
    """Hacker's Delight magic for signed division by d (|d| >= 2): (M as a signed 32-bit, s)"""
    two31 = 0x80000000
    ad = abs(d)
    t = two31 + (d >> 31 & 1 if d < 0 else 0)
    anc = t - 1 - t % ad
    p = 31
    q1 = two31 // anc; r1 = two31 - q1 * anc
    q2 = two31 // ad; r2 = two31 - q2 * ad
    while True:
        p += 1
        q1 = 2 * q1; r1 = 2 * r1
        if r1 >= anc: q1 += 1; r1 -= anc
        q2 = 2 * q2; r2 = 2 * r2
        if r2 >= ad: q2 += 1; r2 -= ad
        delta = ad - r2
        if not (q1 < delta or (q1 == delta and r1 == 0)): break
    M = (q2 + 1) & M32
    if d < 0: M = (-M) & M32
    return M, p - 32

def emit_sdiv(r, f, d, src, dst, tmp):
    M, s = smagic(d)
    lit_or_imm(f, r, tmp, M)
    f.add(dmuls(tmp, src))
    f.add(stsmach(dst))
    if d > 0 and sx32(M) < 0: f.add(add(dst, src))
    if d < 0 and sx32(M) > 0: f.add(sub(dst, src))
    shr_arith(f, dst, s)
    f.add(mov(tmp, dst))
    shr_logical(f, tmp, 31)                          # + (q >>> 31): adds one to a negative quotient
    f.add(add(dst, tmp))

SDIVISORS = [3, 5, 6, 7, 9, 10, 11, 12, 13, 25, 100, 641, 1000, 3600, 10000, 65537, -3, -5, -7, -10, -100, -1000, 0x7FFFFFFF, -0x7FFFFFFF, 0x40000001, -0x40000001, 0x3FFFFFFF]

def fam_sdiv(r, f):
    d = r.choice(SDIVISORS) if r.random() < 0.7 else r.choice([-1, 1]) * r.randrange(3, 100000)
    f.d_hint = ('sdiv', d)
    pre = r.randrange(0, 4)
    src = 4
    if pre == 1: f.add(extsw(1, 4)); src = 1
    elif pre == 2: f.add(extsb(1, 4)); src = 1
    emit_sdiv(r, f, d, src, 0, 2)
    k = r.randrange(0, 4)
    if k == 1:
        lit_or_imm(f, r, 3, d)
        f.add(0x0007 | 3 << 8 | 0 << 4, stsmacl(3))
        f.add(mov(0, src), sub(0, 3))
    elif k == 2:
        lit_or_imm(f, r, 3, r.choice([0, 1, -1, 5, 100, 0x7FFF]))
        f.add(r.choice([0x3003, 0x3007]) | 0 << 8 | 3 << 4)
        result_of_t(r, f, 0)

# ---------------------------------------------------------------------------------------------------------------- 23 div1
def fam_div1(r, f):
    """the library division written inline: r4 / r5 (unsigned: div0u, rotcl, div1 x n; signed: div0s)"""
    n = r.choice([1, 2, 3, 4, 8, 16, 32, 32, 5, 7])
    signed = r.random() < 0.35
    dividend, divisor = 4, 5
    if r.random() < 0.3:
        f.add(extub(5, 5) if r.random() < 0.5 else extuw(5, 5))
    f.add(mov(0, dividend))
    if signed:
        f.add(ROTCL | 0 << 8)                                      # rotcl r0 ; subc r1,r1 : r1 = the dividend's sign extension
        f.add(0x0008, 0x300A | 1 << 8 | 1 << 4)
        f.add(0x2007 | 1 << 8 | 5 << 4)                            # div0s r5,r1
    else:
        f.add(movi(1, 0)); f.add(0x0019)                           # r1 = 0 ; div0u
    for _ in range(n):
        f.add(ROTCL | 0 << 8, 0x3004 | 1 << 8 | 5 << 4)            # rotcl r0 ; div1 r5,r1
    f.add(ROTCL | 0 << 8)
    if r.random() < 0.3: f.add(0x6007 | 0 << 8 | 0 << 4)           # not r0 (the signed form's correction)
    if r.random() < 0.3: f.add(mov(0, 1))                           # return the remainder state

# ---------------------------------------------------------------------------------------------------------------- 24 dmul
def fam_dmul(r, f):
    a, b = r.choice([(4, 5), (4, 4), (5, 4)])
    if r.random() < 0.4:
        lit_or_imm(f, r, 6, r.choice([0x10001, 0xFFFF, 0x80000000, 0x7FFFFFFF, 0xFFFFFFFF, 0x12345678, 0xAAAAAAAB, 3, 0x10000]))
        b = 6
    op = r.choice(['dmulu', 'dmuls', 'dmulu', 'dmuls', 'mull', 'mulsw', 'muluw'])
    if op == 'dmulu': f.add(dmulu(b, a))
    elif op == 'dmuls': f.add(dmuls(b, a))
    elif op == 'mull': f.add(0x0007 | a << 8 | b << 4)
    elif op == 'mulsw': f.add(0x200F | a << 8 | b << 4)
    else: f.add(0x200E | a << 8 | b << 4)
    k = r.randrange(0, 6)
    f.add(stsmacl(1))
    if op in ('dmulu', 'dmuls'): f.add(stsmach(0))
    else: f.add(mov(0, 1))
    if k == 0: pass
    elif k == 1: f.add(0x301C)                                      # add r1,r0
    elif k == 2: f.add(0x201A)                                      # xor r1,r0
    elif k == 3: shr_logical(f, 0, r.choice([1, 2, 4, 8, 16, 31]))
    elif k == 4: f.add(0x3018)                                      # sub r1,r0
    else:
        f.add(0x3006 | 0 << 8 | 1 << 4); result_of_t(r, f, 0)       # cmp/hi r1,r0 : high word against low word

# ---------------------------------------------------------------------------------------------------------------- 25 shifts
def fam_shifts(r, f):
    f.add(mov(0, 4))
    k = r.randrange(0, 3)
    if k < 2:
        for _ in range(r.randrange(2, 8)):
            f.add(r.choice([SHLL, SHLR, SHAR, ROTL, ROTR, SHLL2, SHLR2, SHLL8, SHLR8, SHLL16, SHLR16, ROTCL, ROTCR, 0x4000 | 0, 0x4001]) | 0 << 8)
            if r.random() < 0.15: f.add(0x0008)                    # clrt
            if r.random() < 0.1: f.add(0x0018)                     # sett
        if r.random() < 0.4: f.add(extub(0, 0) if r.random() < 0.5 else extsw(0, 0))
    else:
        # variable shift: (r5 & mask) times in a counted loop, a zero count skipped
        f.add(mov(2, 5), movi(3, r.choice([0x07, 0x0F, 0x1F])), 0x2009 | 2 << 8 | 3 << 4)     # and r3,r2
        f.add(0x2008 | 2 << 8 | 2 << 4); f.br('bt', 'end')       # tst r2,r2 ; bt end
        f.label('top')
        f.add(r.choice([SHLL, SHLR, SHAR, ROTL]) | 0 << 8)
        f.add(0x4010 | 2 << 8)                                     # dt r2
        f.br('bf', 'top')
        f.label('end')
    tail = r.randrange(0, 3)
    if tail == 1: f.add(0xC9FF)
    elif tail == 2: f.add(extsb(0, 0))

# ---------------------------------------------------------------------------------------------------------------- 26 addcchain
def fam_addc(r, f):
    """r4:r5 and r6:r7 as 64-bit (high:low) values"""
    op = r.randrange(0, 7)
    f.add(mov(0, 5), mov(1, 4))              # r1:r0 = r4:r5  (high:low)
    if r.random() < 0.3: lit_or_imm(f, r, 6, r.choice([0x80000000, 0xFFFFFFFF, 1, 0x7FFFFFFF, 0xFFFFFFFE]))
    if op == 0: f.add(0x0008, 0x300E | 0 << 8 | 7 << 4, 0x300E | 1 << 8 | 6 << 4)          # clrt ; addc r7,r0 ; addc r6,r1
    elif op == 1: f.add(0x0008, 0x300A | 0 << 8 | 7 << 4, 0x300A | 1 << 8 | 6 << 4)        # subc
    elif op == 2: f.add(0x0008, 0x600A | 0 << 8 | 0 << 4, 0x600A | 1 << 8 | 1 << 4)        # negc low, negc high
    elif op == 3: f.add(0x0008, 0x300E | 0 << 8 | 0 << 4, 0x300E | 1 << 8 | 1 << 4)        # add to itself (shift left 1 as 64 bits)
    elif op == 4: f.add(0x0018, 0x300E | 0 << 8 | 7 << 4, 0x300E | 1 << 8 | 6 << 4)        # sett ; addc
    elif op == 5: f.add(0x0008, 0x300F | 0 << 8 | 7 << 4, 0x300E | 1 << 8 | 6 << 4)        # addv low ; addc high
    else: f.add(0x0018, 0x300A | 0 << 8 | 7 << 4, 0x300A | 1 << 8 | 6 << 4)                # sett ; subc
    k = r.randrange(0, 6)
    if k == 0: pass                                       # return the low word
    elif k == 1: f.add(mov(0, 1))                          # the high word
    elif k == 2: f.add(0x0029)                             # movt (the carry out)
    elif k == 3: f.add(0x301C)                             # add r1,r0
    elif k == 4: f.add(0x3006 | 0 << 8 | 7 << 4); result_of_t(r, f, 0)   # compare the low result against r7
    else: f.add(0x3002 | 1 << 8 | 6 << 4); result_of_t(r, f, 0)          # cmp/hs r6,r1 : 64-bit compare's high step

# ---------------------------------------------------------------------------------------------------------------- 27 signtest
def fam_signtest(r, f):
    f.add(mov(0, 4))
    k = r.randrange(0, 8)
    if k == 0:                                 # abs
        f.add(0x4011 | 0 << 8); f.br('bt', 'p'); f.add(0x600B | 0 << 8 | 0 << 4); f.label('p')
    elif k == 1:                               # sign: -1, 0, 1
        f.add(mov(1, 0)); shr_arith(f, 1, 31) if False else f.add(0x4021 | 1 << 8)
        shr_arith(f, 1, 30)
        f.add(0x4015 | 0 << 8); f.add(movi(0, 0)); f.br('bf', 'z'); f.add(movi(0, 1)); f.label('z'); f.add(0x201B)   # or r1,r0
    elif k == 2:                               # min(x, r5)
        f.add(0x3003 | 0 << 8 | 5 << 4); f.br('bt', 'k'); f.add(mov(0, 5)); f.label('k')
    elif k == 3:                               # max(x, r5) unsigned
        f.add(0x3002 | 0 << 8 | 5 << 4); f.br('bt', 'k'); f.add(mov(0, 5)); f.label('k')
    elif k == 4:                               # clamp to [0, 255]
        f.add(0x4011 | 0 << 8); f.br('bt', 'a'); f.add(movi(0, 0)); f.br('bra', 'e'); f.add(0x0009); f.label('a')
        f.add(movi(1, 0xFF)); f.add(0x3007 | 0 << 8 | 1 << 4); f.br('bf', 'e'); f.add(movi(0, 0xFF)); f.label('e')
    elif k == 5:                               # tst of a mask
        f.add(0xC800 | r.choice([0x80, 0xFF, 0x01, 0x0F, 0xF0, 0x7F])); result_of_t(r, f, 0)
    elif k == 6:                               # x >> 31 forms
        shr_logical(f, 0, 31)
        if r.random() < 0.5: f.add(0x600B | 0 << 8 | 0 << 4)
    else:                                      # (x ^ (x >> 31)) - (x >> 31) (abs without a branch)
        f.add(mov(1, 0)); shr_arith(f, 1, 31) if False else None
        f.add(0x4021 | 1 << 8); f.add(0x4021 | 1 << 8)
        f.add(0x2010 | 0 << 8 | 0 << 4) if False else None
        f.add(0x201A | 0 << 8 | 1 << 4); f.add(sub(0, 1))
    if r.random() < 0.3: f.add(extsb(0, 0) if r.random() < 0.5 else extuw(0, 0))

# ---------------------------------------------------------------------------------------------------------------- 28 rangetst
def fam_rangetst(r, f):
    kind = r.choice([0, 0, 1, 2, 3, 4, 4, 4])
    reduce_in(r, f, 0, 4, kind)
    lo = lit_for(r, kind); hi = lit_for(r, kind)
    form = r.randrange(0, 7)
    lit_or_imm(f, r, 1, lo); lit_or_imm(f, r, 2, hi)
    if form == 0:                  # lo <= x && x < hi (unsigned), by branches
        f.add(0x3002 | 0 << 8 | 1 << 4); f.br('bf', 'no')           # cmp/hs r1,r0
        f.add(0x3006 | 2 << 8 | 0 << 4); f.br('bf', 'no')           # cmp/hi r0,r2  : hi > x
        f.add(movi(0, 1)); f.br('bra', 'end'); f.add(0x0009)
        f.label('no'); f.add(movi(0, 0)); f.label('end')
    elif form == 1:                # same, both T's materialized and combined
        f.add(0x3002 | 0 << 8 | 1 << 4, 0x0029 | 3 << 8)
        f.add(0x3006 | 2 << 8 | 0 << 4, 0x0029 | 0 << 8)
        f.add(0x2009 | 0 << 8 | 3 << 4)                              # and r3,r0
    elif form == 2:                # either outside: x < lo || x >= hi
        f.add(0x3002 | 0 << 8 | 1 << 4, 0x0029 | 3 << 8)
        f.add(0x3002 | 0 << 8 | 2 << 4, 0x0029 | 0 << 8)
        f.add(0x2009 | 0 << 8 | 3 << 4) if False else f.add(0x200B | 0 << 8 | 3 << 4)
        f.add(0x6007 | 0 << 8 | 0 << 4) if r.random() < 0.0 else None
        f.add(0xC901)
        f.add(0xCA01) if r.random() < 0.5 else None
    elif form == 3:                # (x - lo) <u (hi - lo)
        f.add(sub(2, 1))
        f.add(sub(0, 1))
        f.add(0x3006 | 2 << 8 | 0 << 4)                              # cmp/hi r0,r2
        result_of_t(r, f, 0)
    elif form == 4:                # signed range
        f.add(0x3003 | 0 << 8 | 1 << 4); f.br('bf', 'no')
        f.add(0x3007 | 2 << 8 | 0 << 4); f.br('bf', 'no')
        f.add(movi(0, 1)); f.br('bra', 'end'); f.add(0x0009)
        f.label('no'); f.add(movi(0, 0)); f.label('end')
    elif form == 5:                # x == lo || x == hi
        f.add(0x3000 | 0 << 8 | 1 << 4); f.br('bt', 'yes')
        f.add(0x3000 | 0 << 8 | 2 << 4); f.br('bt', 'yes')
        f.add(movi(0, 0)); f.br('bra', 'end'); f.add(0x0009)
        f.label('yes'); f.add(movi(0, 1)); f.label('end')
    else:                          # three-way: x < lo ? -1 : x > lo ? 1 : 0 (unsigned or signed)
        c1, c2 = r.choice([(0x3002, 0x3006), (0x3003, 0x3007)])
        f.add(c1 | 0 << 8 | 1 << 4); f.br('bf', 'less')
        f.add(c2 | 0 << 8 | 1 << 4); f.br('bf', 'eq')
        f.add(movi(0, 1)); f.br('bra', 'end'); f.add(0x0009)
        f.label('less'); f.add(movi(0, 0xFF)); f.br('bra', 'end'); f.add(0x0009)
        f.label('eq'); f.add(movi(0, 0))
        f.label('end')

FAMS = {20: fam_cmpbyte, 21: fam_udiv, 22: fam_sdiv, 23: fam_div1, 24: fam_dmul, 25: fam_shifts, 26: fam_addc, 27: fam_signtest, 28: fam_rangetst}

def build(seed, fam=None, want_fn=False):
    r = random.Random(seed * 104729 + 7)
    f = Fn(r)
    f.d_hint = None
    fm = FAMS[fam] if fam is not None else FAMS[r.choice(sorted(FAMS))]
    fm(r, f)
    f.add(0x000B, 0x0009)
    if want_fn: return f
    return finish(f)

def lits_of(f):
    out = []
    for c in f.w:
        if isinstance(c, tuple) and c[0] == 'lit': out.append(c[2] & M32)
    return out

def extras(first, count, fam):
    vals = set()
    for s in range(first, first + count):
        f = build(s, fam, want_fn=True)
        for v in lits_of(f):
            for dv in (-2, -1, 0, 1, 2): vals.add((v + dv) & M32)
            vals.add((-v) & M32); vals.add((v - 1) & 0xFFFF); vals.add(v & 0xFF)
        hint = f.d_hint
        if hint:
            kind, d = hint
            ad = abs(d)
            if ad >= 2:
                for k in (1, 2, 3, 5, 7, 100, (1 << 32) // ad - 1, (1 << 32) // ad, (1 << 31) // ad, ((1 << 32) // ad) // 2 + 1, ((1 << 32) // ad) // 3):
                    b = (k * ad) & M32
                    for dv in (-1, 0, 1):
                        vals.add((b + dv) & M32)
                        vals.add((-b + dv) & M32)
                for dv in (-2, -1, 0, 1, 2): vals.add(((1 << 32) - 1 + dv) & M32), vals.add(((1 << 31) + dv) & M32)
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
