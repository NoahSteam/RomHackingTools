#!/usr/bin/env python3
# Area 2 round 14 instrument. Register-only functions for the printed-C differential (and for the in-situ rule check, rulecheck14.hh), on the SH-2 idioms
# that families 0 to 61 never built (loads dagen13.py and through it dagen12/11/10; their helpers are reused):
#   62 shmul    a constant multiply written the way a compiler writes it for a CPU without a fast multiply: shift-and-add chains (Horner, canonical signed digits,
#               fixed shll/shll2/shll8/shll16 steps), of a whole register or an extended byte/word, then read as a shift, a mask, an add, a compare, or
#               subtracted from the same product made by mul.l (the engine must fold the difference to 0 or print a value that is 0)
#   63 muldi    64-bit products: 64x64 -> 64 from dmulu.l and two mul.l, a signed 32x32 -> 64 made from the unsigned product and the sign corrections (and
#               compared with dmuls.l), the high word of a signed product from the unsigned one, a 64x32 product by a constant
#   64 shift64  64-bit constant shifts of a register pair by 1 to 63: single-bit shll/rotcl, shlr/rotcr, shar/rotcr chains, the shift-and-or form, xtrct for 16,
#               a shift of 32 or more as a move
#   65 cmpstr   cmp/str: a zero byte in a word, a byte equal to a pattern, the index of the first zero byte by a counted loop, T combined with other tests
#   66 bitops   parity by xor folding, the SWAR population count with its final multiply, byte and bit reversal, the lowest set bit, a power-of-two test, the
#               rotate by (x << k) | (x >> (32 - k)), bit-field extract and insert, the sign extension (x ^ m) - m
#   67 divterm  the division terms of RuleDivTermAdd, RuleDivTermAdd2 and RuleDivOpt on constants that are no divisor's: dmulu/dmuls, mul.l and muls.w/mulu.w
#               of an extended value, swap.w/extu.w/shlr16 to take the high part, plus or minus the value, the sum read by one or two shifts, a mask or a compare,
#               and (the CSE twin) the same product with the folded multiplier made by mul.l (a wrapping product that the flagged exact one must not replace)
#   68 div1len  div0u/div0s and div1 chains of every length from 1 to 32, 16-bit and 64/32 setups, signed fix-ups, the remainder state read back
#   69 cmov     a diamond or a triangle that picks between two constants (0/1, 0/-1, c/c+1, 0/2^k, random), of a byte, word or longword, the select of two
#               registers, arms that compute (RuleConditionalMove)
#   70 divdiv   (x / d1) / d2, (x % d1) % d2, (x >> a) / d, x / (power of two) / d and negative divisors, signed and unsigned (RuleDivChain, RuleSignDiv2)
#   71 varshift variable shifts and rotates of a register and of a pair by a counted loop over (r5 & 31), the amount 0 skipped
#   73 signmod  x % 2^k and x / 2^k of a signed byte, word or longword for every k (the bias added to a negative x by a branch or without one; RuleSignMod2nOpt2)
#   74 deepsoup 15 to 45 steps over r0..r3 with the T flag produced and consumed in every way (compares, tst, cmp/str, dt, addc/subc/negc/addv/subv, div0/div1, rotcl/rotcr, movt),
#               the multiplies and their mach/macl reads, extensions, swaps, forward branches over one to three steps
#   75 delaybr  delayed branches with working slots: bf/s closing a counted loop (the slot's T read after), forward bt/s and bf/s with T consumers in the slot,
#               bra with a slot, two chained delayed branches
#   76 condprop one condition read by several branches with T-preserving code between (ConditionalExecution), x == c with x used in the true arm
#               (ActionConditionalConst), a chain of equality tests, a loop whose exit test is repeated
#   77 multiword 96- and 128-bit values in r4..r7: a constant added through the carries, added to itself, negated, a rotated copy subtracted, the borrow as a compare,
#               shifted one bit through T
#   72 mix      two or three blocks of the families above in a row (the result of one the input of the next)
# usage: dagen14.py <first seed> <count> [family 62..77]   -> one function per seed, ";" joined (SE_GENERATED_PRINTEDC_WORDS)
#        dagen14.py extras <first seed> <count> <family>   -> the comma-joined hex list for SE_GENERATED_PRINTEDC_EXTRA
import importlib.util, os, random, sys

here = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("dagen13", os.path.join(here, "..", "a2_round13", "dagen13.py"))
d13 = importlib.util.module_from_spec(spec)
spec.loader.exec_module(d13)
globals().update({k: v for k, v in vars(d13).items() if not k.startswith('__')})

def cmp_str(m, n): return 0x200C | n << 8 | m << 4
def div0u(): return 0x0019
def div0s(m, n): return 0x2007 | n << 8 | m << 4
def div1(m, n): return 0x3004 | n << 8 | m << 4
def dt(n): return 0x4010 | n << 8
def mov_(d, s): return 0x6003 | d << 8 | s << 4
def or__(m, n): return 0x200B | n << 8 | m << 4          # n |= m
def and__(m, n): return 0x2009 | n << 8 | m << 4         # n &= m
def xor_(m, n): return 0x200A | n << 8 | m << 4          # n ^= m
def add__(m, n): return 0x300C | n << 8 | m << 4         # n += m
def sub__(m, n): return 0x3008 | n << 8 | m << 4         # n -= m
def neg_(m, n): return 0x600B | n << 8 | m << 4          # n = -m
def swapw_(m, n): return 0x6009 | n << 8 | m << 4
def swapb_(m, n): return 0x6008 | n << 8 | m << 4
def mulL(m, n): return 0x0007 | n << 8 | m << 4           # macl = Rn * Rm
def tstr(m, n): return 0x2008 | n << 8 | m << 4
def cmpeq(m, n): return 0x3000 | n << 8 | m << 4
def cmphi(m, n): return 0x3006 | n << 8 | m << 4          # T = Rn >u Rm
def cmphs(m, n): return 0x3002 | n << 8 | m << 4
def cmpgt(m, n): return 0x3007 | n << 8 | m << 4
def cmpge(m, n): return 0x3003 | n << 8 | m << 4
def cmppz(n): return 0x4011 | n << 8
def cmppl(n): return 0x4015 | n << 8
def not_(m, n): return 0x6007 | n << 8 | m << 4

def tail_common(r, f, d=0, scratch=1):
    """a reader of the result in register d (moved to r0): nothing, a mask, an extension, a shift, a compare with a constant"""
    if d != 0: f.add(mov_(0, d))
    t = r.randrange(0, 9)
    if t == 1: f.add(r.choice([extub, extuw, extsb, extsw])(0, 0))
    elif t == 2: f.add(r.choice([andi(0xFF), andi(0x7F), andi(0x0F), andi(0x01), andi(0xFE)]))
    elif t == 3: (shr_arith if r.random() < 0.4 else shr_logical)(f, 0, r.choice([1, 2, 7, 8, 15, 16, 24, 31]))
    elif t == 4:
        f.lit(scratch, r.choice([0, 1, 0xFF, 0x100, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF, r.getrandbits(32)]))
        f.add(r.choice([cmpeq, cmphi, cmphs, cmpgt, cmpge])(scratch, 0), movt(0))
    elif t == 5: f.add(cmppz(0), movt(0))
    elif t == 6: f.add(mov_(scratch, 4), add__(scratch, 0))
    elif t == 7: f.add(swapw_(0, 0), extuw(0, 0)) if r.random() < 0.5 else f.add(swapb_(0, 0))

# ------------------------------------------------------------------------------------------------------------------------------------ 62 shmul
SHMULS = [3, 5, 6, 7, 9, 10, 11, 12, 13, 15, 17, 18, 20, 24, 25, 28, 30, 31, 33, 36, 40, 48, 60, 63, 65, 96, 100, 127, 129, 255, 257, 320, 1000, 1023, 1025, 4095, 4097,
          0xFFFF, 0x10001, 0x5555, 0x3333, 0x0F0F, 0x1010101, 0x7FFFFF, 0x1000001, 0xFFFFFF, 0x80001, 0xAAAAAAAB, 0x55555556, 0x66666667, 0x80000001, 0xFFFFFFFF]

def csd(k):
    """canonical signed digits of k >= 0: [(shift, +1 or -1)] ascending"""
    out = []
    i = 0
    while k:
        if k & 1:
            if (k & 3) == 3:
                out.append((i, -1)); k += 1
            else:
                out.append((i, 1)); k -= 1
        k >>= 1; i += 1
    return out

def emit_shmul(r, f, k, src, acc, tmp):
    """acc = src * k (mod 2^32) by shifts and adds; tmp is a scratch register; src is not written"""
    k &= M32
    style = r.randrange(0, 3)
    if k == 0: f.add(movi(acc, 0)); return
    if style == 0:
        # Horner: acc = src; for each lower bit: acc <<= gap; if the bit is set: acc += src
        bits = [i for i in range(32) if (k >> i) & 1]
        top = bits[-1]
        f.add(mov_(acc, src))
        prev = top
        for b in reversed(bits[:-1]):
            shl(f, acc, prev - b); f.add(add__(src, acc)); prev = b
        shl(f, acc, prev)
    elif style == 1:
        ts = csd(k)
        first = True
        for (sh, sg) in ts:
            if sh >= 32: continue
            if first:
                f.add(mov_(acc, src)); shl(f, acc, sh)
                if sg < 0: f.add(neg_(acc, acc))
                first = False
            else:
                f.add(mov_(tmp, src)); shl(f, tmp, sh)
                f.add(add__(tmp, acc) if sg > 0 else sub__(tmp, acc))
        if first: f.add(movi(acc, 0))
    else:
        # the set bits from the bottom, one shifted copy per bit
        bits = [i for i in range(32) if (k >> i) & 1]
        f.add(mov_(acc, src)); shl(f, acc, bits[0])
        for b in bits[1:]:
            f.add(mov_(tmp, src)); shl(f, tmp, b); f.add(add__(tmp, acc))

def fam_shmul(r, f):
    k = r.choice(SHMULS) if r.random() < 0.75 else r.getrandbits(r.choice([8, 12, 16, 24, 32])) | 1
    neg = r.random() < 0.15
    pre = r.randrange(0, 6)
    src = 4
    if pre == 1: f.add(extub(1, 4)); src = 1
    elif pre == 2: f.add(extuw(1, 4)); src = 1
    elif pre == 3: f.add(extsb(1, 4)); src = 1
    elif pre == 4: f.add(extsw(1, 4)); src = 1
    emit_shmul(r, f, (-k) & M32 if neg else k, src, 0, 2)
    t = r.randrange(0, 9)
    if t == 1:
        (shr_arith if r.random() < 0.35 else shr_logical)(f, 0, r.choice([1, 2, 3, 4, 7, 8, 9, 15, 16, 17, 24, 31]))
    elif t == 2:
        f.add(r.choice([extub, extuw, extsb, extsw])(0, 0))
    elif t == 3:
        f.add(r.choice([andi(0xFF), andi(0x7F), andi(0x0F), andi(0xFE)]))
    elif t == 4:
        # the same product by mul.l: the difference is 0
        f.lit(3, (-k) & M32 if neg else k)
        f.add(mov_(2, src), mulL(3, 2), stsmacl(3), sub__(3, 0))
    elif t == 5:
        f.add(add__(src, 0))
    elif t == 6:
        f.lit(3, r.choice([0, 1, 100, 1000, 0x10000, 0x7FFFFFFF, 0x80000000, r.getrandbits(32)]))
        f.add(r.choice([cmpeq, cmphi, cmpgt, cmpge, cmphs])(3, 0), movt(0))
    elif t == 7:
        # the product read twice: shifted down and masked
        f.add(mov_(3, 0)); shr_logical(f, 3, r.choice([8, 16, 24])); f.add(extub(0, 0), xor_(3, 0))
    elif t == 8:
        # two sources with their own constants: x*k1 + y*k2 (shared terms)
        k2 = r.choice(SHMULS)
        emit_shmul(r, f, k2, 5, 3, 2)
        f.add(add__(3, 0))

# ------------------------------------------------------------------------------------------------------------------------------------ 63 muldi
def fam_muldi(r, f):
    form = r.randrange(0, 6)
    if form == 0:
        # (r4:r5) * (r6:r7) mod 2^64: hi = hi(al*bl) + ah*bl + al*bh, lo = lo(al*bl)
        f.add(dmulu(7, 5), stsmach(0), stsmacl(1))
        f.add(mulL(7, 4), stsmacl(2), add__(2, 0))
        f.add(mulL(6, 5), stsmacl(2), add__(2, 0))
    elif form == 1:
        # signed 32x32 -> 64 from the unsigned product and the sign corrections, compared with dmuls.l
        f.add(dmulu(5, 4), stsmach(0), stsmacl(1))
        f.add(cmppz(4)); f.br('bt', 'a'); f.add(sub__(5, 0)); f.label('a')
        f.add(cmppz(5)); f.br('bt', 'b'); f.add(sub__(4, 0)); f.label('b')
        if r.random() < 0.6:
            f.add(dmuls(5, 4), stsmach(2), stsmacl(3), xor_(2, 0), xor_(3, 1), or__(1, 0))
    elif form == 2:
        # 64 x 32 -> 64: (r4:r5) * r6
        f.add(dmulu(6, 5), stsmach(0), stsmacl(1))
        f.add(mulL(6, 4), stsmacl(2), add__(2, 0))
    elif form == 3:
        # mulhs from mulhu: hi - (a >> 31 & b) - (b >> 31 & a)
        f.add(dmulu(5, 4), stsmach(0))
        f.add(mov_(2, 4)); f.add(0x4021 | 2 << 8); shr_arith(f, 2, 30); f.add(and__(5, 2), sub__(2, 0))
        f.add(mov_(2, 5)); f.add(0x4021 | 2 << 8); shr_arith(f, 2, 30); f.add(and__(4, 2), sub__(2, 0))
        f.add(mov_(1, 0)); f.add(dmuls(5, 4), stsmach(2), xor_(2, 0)) if r.random() < 0.5 else None
    elif form == 4:
        # 64 x constant
        c = r.choice([10, 100, 1000, 0x10001, 0x9E3779B9, 0x80000000, 0xFFFFFFFF, 3, 0x5DEECE66, r.getrandbits(32)])
        f.lit(6, c)
        f.add(dmulu(6, 5), stsmach(0), stsmacl(1))
        f.add(mulL(6, 4), stsmacl(2), add__(2, 0))
    else:
        # the signed 64-bit product's high word of two signed 32-bit values shifted into a 32-bit fixed-point result
        f.add(dmuls(5, 4), stsmach(0), stsmacl(1), xtrct(0, 1))
        f.add(mov_(0, 1))
        return
    k = r.randrange(0, 6)
    if k == 0: pass                                    # the high word
    elif k == 1: f.add(mov_(0, 1))                      # the low word
    elif k == 2: f.add(xor_(1, 0))
    elif k == 3: f.add(add__(1, 0))
    elif k == 4: f.add(or__(1, 0), tstr(0, 0), movt(0))  # the 64-bit value is zero
    else: f.add(cmphi(7, 1) if r.random() < 0.5 else cmpeq(7, 1), movt(0))

# ------------------------------------------------------------------------------------------------------------------------------------ 64 shift64
def emit_shl64(r, f, hi, lo, k, tmp):
    if k >= 32:
        f.add(mov_(hi, lo)); shl(f, hi, k - 32); f.add(movi(lo, 0)); return
    if k == 16 and r.random() < 0.6:
        f.add(mov_(tmp, lo), xtrct(hi, tmp), mov_(hi, tmp)); f.add(SHLL16 | lo << 8); return
    if k <= 5 and r.random() < 0.6:
        for _ in range(k): f.add(SHLL | lo << 8, ROTCL | hi << 8)
        return
    f.add(mov_(tmp, lo)); shr_logical(f, tmp, 32 - k); shl(f, hi, k); f.add(or__(tmp, hi)); shl(f, lo, k)

def emit_shr64(r, f, hi, lo, k, tmp, arith):
    if k >= 32:
        f.add(mov_(lo, hi))
        (shr_arith if arith else shr_logical)(f, lo, k - 32)
        if arith: shr_arith(f, hi, 31)
        else: f.add(movi(hi, 0))
        return
    if k == 16 and r.random() < 0.6:
        f.add(xtrct(hi, lo))
        (f.add(SHAR | hi << 8) if False else None)
        if arith: shr_arith(f, hi, 16)
        else: f.add(SHLR16 | hi << 8)
        return
    if k <= 5 and r.random() < 0.6:
        for _ in range(k): f.add((SHAR if arith else SHLR) | hi << 8, ROTCR | lo << 8)
        return
    f.add(mov_(tmp, hi)); shl(f, tmp, 32 - k); shr_logical(f, lo, k); f.add(or__(tmp, lo))
    (shr_arith if arith else shr_logical)(f, hi, k)

def fam_shift64(r, f):
    # the pair is r4:r5 (high:low), moved to r1:r0 first, or kept in r4:r5
    f.add(mov_(1, 4), mov_(0, 5))
    hi, lo = 1, 0
    for _ in range(r.choice([1, 1, 1, 2])):
        k = r.choice([1, 2, 3, 4, 5, 7, 8, 15, 16, 17, 24, 31, 32, 33, 40, 47, 48, 56, 63, r.randrange(1, 64)])
        kind = r.randrange(0, 3)
        if kind == 0: emit_shl64(r, f, hi, lo, k, 2)
        elif kind == 1: emit_shr64(r, f, hi, lo, k, 2, False)
        else: emit_shr64(r, f, hi, lo, k, 2, True)
    t = r.randrange(0, 7)
    if t == 0: f.add(mov_(0, hi))
    elif t == 1: pass
    elif t == 2: f.add(xor_(hi, lo))
    elif t == 3: f.add(add__(hi, lo))
    elif t == 4: f.add(or__(hi, lo), tstr(lo, lo), movt(lo)); f.add(mov_(0, lo))
    elif t == 5: f.add(cmpeq(6, hi), movt(2), cmpeq(7, lo), movt(lo), and__(2, lo)); f.add(mov_(0, lo))
    else: f.add(cmphi(6, hi), movt(0))

# ------------------------------------------------------------------------------------------------------------------------------------ 65 cmpstr
def fam_cmpstr(r, f):
    form = r.randrange(0, 6)
    if form == 0:                       # a zero byte in r4
        f.add(movi(1, 0), cmp_str(1, 4))
        result_of_t(r, f, 0)
    elif form == 1:                     # a byte of r4 equal to the same byte of a pattern
        pat = r.choice([0x20202020, 0x0A0A0A0A, 0x41414141, 0xFFFFFFFF, 0x80808080, 0x00FF00FF, 0x01010101])
        f.lit(1, pat)
        f.add(cmp_str(1, 4))
        result_of_t(r, f, 0)
    elif form == 2:                     # a byte of r4 equal to the same byte of r5, and the whole words differ
        f.add(cmp_str(5, 4), movt(0), cmpeq(5, 4), movt(1), not_(1, 1), and__(1, 0), andi(1))
    elif form == 3:                     # index of the first zero byte (counted, 4 bytes, from the high byte)
        f.add(mov_(2, 4), movi(0, 0))
        f.label('top')
        f.add(mov_(3, 2)); shr_logical(f, 3, 24)
        f.add(tstr(3, 3)); f.br('bt', 'out')
        f.add(SHLL8 | 2 << 8, addi(0, 1))
        f.add(cmp_eq(0, 0) if False else 0x8800 | 4)       # cmp/eq #4,r0
        f.br('bf', 'top')
        f.label('out')
        f.add(mov_(1, 0)); f.rts_slot = mov_(0, 1)
    elif form == 4:                     # null-terminated byte count in a word by tests of the bytes in turn (swap.b / shifts)
        f.add(movi(0, 0), mov_(2, 4))
        f.add(extub(3, 2), tstr(3, 3)); f.br('bt', 'e')
        f.add(addi(0, 1), SHLR8 | 2 << 8, extub(3, 2), tstr(3, 3)); f.br('bt', 'e')
        f.add(addi(0, 1), SHLR8 | 2 << 8, extub(3, 2), tstr(3, 3)); f.br('bt', 'e')
        f.add(addi(0, 1), SHLR8 | 2 << 8, extub(3, 2), tstr(3, 3)); f.br('bt', 'e')
        f.add(addi(0, 1))
        f.label('e')
        f.add(mov_(1, 0)); f.rts_slot = mov_(0, 1)
    else:                               # cmp/str of a word against a sign-extended byte pattern, the T read after another test
        f.add(extsb(1, 5), cmp_str(1, 4), movt(0), cmppz(4), movt(1), xor_(1, 0))
    if r.random() < 0.3: tail_common(r, f, 0, 3)

# ------------------------------------------------------------------------------------------------------------------------------------ 66 bitops
def fam_bitops(r, f):
    form = r.randrange(0, 12)
    if form == 0:                       # parity by xor folding
        f.add(mov_(0, 4), mov_(1, 0)); shr_logical(f, 1, 16); f.add(xor_(1, 0))
        f.add(mov_(1, 0)); shr_logical(f, 1, 8); f.add(xor_(1, 0))
        f.add(mov_(1, 0)); shr_logical(f, 1, 4); f.add(xor_(1, 0))
        f.add(mov_(1, 0)); shr_logical(f, 1, 2); f.add(xor_(1, 0))
        f.add(mov_(1, 0)); f.add(SHLR | 1 << 8); f.add(xor_(1, 0), andi(1))
    elif form == 1:                     # SWAR population count with the multiply
        f.add(mov_(0, 4), mov_(1, 4), SHLR | 1 << 8); f.lit(2, 0x55555555); f.add(and__(2, 1), sub__(1, 0))
        f.add(mov_(1, 0)); shr_logical(f, 1, 2); f.lit(2, 0x33333333); f.add(and__(2, 1), and__(2, 0), add__(1, 0))
        f.add(mov_(1, 0)); shr_logical(f, 1, 4); f.add(add__(1, 0)); f.lit(2, 0x0F0F0F0F); f.add(and__(2, 0))
        f.lit(2, 0x01010101); f.add(mulL(2, 0), stsmacl(0)); shr_logical(f, 0, 24)
    elif form == 2:                     # byte and word swaps
        f.add(swapb_(4, 0)); f.add(swapw_(0, 0)); f.add(swapb_(0, 0)) if r.random() < 0.6 else None
    elif form == 3:                     # lowest set bit, and clearing it
        f.add(neg_(4, 1), mov_(0, 4), and__(1, 0))
        if r.random() < 0.5: f.add(mov_(1, 4), addi(1, -1), and__(4, 1), mov_(0, 1))
    elif form == 4:                     # power of two test
        f.add(mov_(1, 4), addi(1, -1), and__(4, 1), tstr(1, 1), movt(0), cmpeqi(0) if False else tstr(4, 4), movt(1), not_(1, 1), and__(1, 0), andi(1))
    elif form == 5:                     # rotate left by k: (x << k) | (x >> (32 - k))
        k = r.choice([1, 3, 5, 8, 11, 13, 16, 21, 24, 31])
        f.add(mov_(0, 4), mov_(1, 4)); shl(f, 0, k); shr_logical(f, 1, 32 - k); f.add(or__(1, 0))
    elif form == 6:                     # bit-field extract (a, w), signed or unsigned
        a = r.randrange(0, 24); w = r.randrange(1, 9)
        f.add(mov_(0, 4)); shr_logical(f, 0, a); f.lit(1, (1 << w) - 1); f.add(and__(1, 0))
        if r.random() < 0.5:
            m = 1 << (w - 1); f.lit(1, m); f.add(xor_(1, 0), sub__(1, 0))      # sign extend
    elif form == 7:                     # bit-field insert of r5 into r4
        a = r.randrange(0, 24); w = r.randrange(1, 9); m = ((1 << w) - 1)
        f.lit(1, (~(m << a)) & M32); f.add(mov_(0, 4), and__(1, 0))
        f.lit(2, m); f.add(mov_(1, 5), and__(2, 1)); shl(f, 1, a); f.add(or__(1, 0))
    elif form == 8:                     # byte reverse of a halfword and compare
        f.add(swapb_(4, 0), extuw(0, 0), extuw(1, 5), cmpeq(1, 0), movt(0))
    elif form == 9:                     # bit reversal of a byte by mask swaps
        f.add(extub(0, 4))
        for (m, s) in ((0x55, 1), (0x33, 2), (0x0F, 4)):
            f.add(mov_(1, 0)); shr_logical(f, 1, s); f.lit(2, m); f.add(and__(2, 1), and__(2, 0)); shl(f, 0, s); f.add(or__(1, 0))
        f.add(extub(0, 0))
    elif form == 10:                    # clz by binary search
        f.add(mov_(1, 4), movi(0, 0))
        for s in (16, 8, 4, 2, 1):
            lab = 'z%d' % s
            f.lit(2, ((1 << 32) - (1 << (32 - s))) & M32)
            f.add(tstr(2, 1)); f.br('bf', lab)
            f.add(addi(0, s)); shl(f, 1, s)
            f.label(lab)
    else:                               # the sign extension (x & m) ^ s - s, byte or 12/20 bits
        w = r.choice([4, 7, 8, 12, 16, 20])
        f.lit(1, (1 << w) - 1); f.add(mov_(0, 4), and__(1, 0))
        f.lit(1, 1 << (w - 1)); f.add(xor_(1, 0), sub__(1, 0))
    if r.random() < 0.3: tail_common(r, f, 0, 3)

# ------------------------------------------------------------------------------------------------------------------------------------ 67 divterm
DT_CONSTS = [0x38032949, 0x24924925, 0x92492493, 0xAAAAAAAB, 0x55555556, 0x80000001, 0x7FFFFFFF, 0xFFFFFFFF, 0xCCCCCCCD, 0x51EB851F, 0x2AAAAAAB, 0x66666667,
             0x10001, 0x8001, 0xFFFF, 0x8000, 0x7FFF, 0xAAAB, 0x5556, 0x1999A, 0x4925, 0x2493, 0xB6DB, 0xCCCD, 0xE38F, 0x1C72, 0x10000, 0x20001, 0x40001, 0x1000, 0xFFFFFFFE]

def dt_const(r):
    k = r.randrange(0, 10)
    if k < 5: return r.choice(DT_CONSTS)
    if k < 7: return r.getrandbits(16)
    if k < 8: return (1 << r.randrange(8, 32)) + r.choice([-1, 0, 1, 2, -2])
    return r.getrandbits(32)

def fam_divterm(r, f):
    c = dt_const(r) & M32
    if c == 0: c = 3
    pre = r.choice([0, 0, 1, 2, 3, 4, 5])
    src = 4
    if pre == 1: f.add(extub(1, 4)); src = 1
    elif pre == 2: f.add(extuw(1, 4)); src = 1
    elif pre == 3: f.add(extsb(1, 4)); src = 1
    elif pre == 4: f.add(extsw(1, 4)); src = 1
    elif pre == 5:                                           # a value of 20 bits: partial non-zero mask
        f.lit(1, 0xFFFFF); f.add(and__(4, 1)); src = 1
    mulk = r.randrange(0, 5)
    f.lit(2, c)
    signed_mul = False
    if mulk == 0: f.add(dmulu(2, src), stsmach(0))
    elif mulk == 1: f.add(dmuls(2, src), stsmach(0)); signed_mul = True
    elif mulk == 2:
        f.add(mov_(3, src), mulL(2, 3), stsmacl(0))
        sh = r.choice([16, 8, 24])
        shr_logical(f, 0, sh)
    elif mulk == 3:
        f.add(r.choice([muluw, mulsw])(2, src), stsmacl(0))
        f.add(SHLR16 | 0 << 8) if r.random() < 0.5 else f.add(swapw_(0, 0), extuw(0, 0))
    else:
        f.add(dmulu(2, src), stsmach(0), stsmacl(3))        # both halves read: the low word is compared at the end
    k = r.randrange(0, 4)
    if k == 0: f.add(add__(src, 0))
    elif k == 1: f.add(sub__(src, 0))
    elif k == 2: f.add(mov_(3, src), sub__(0, 3), mov_(0, 3))
    else:
        f.add(mov_(3, src), sub__(0, 3)); f.add(SHLR | 3 << 8); f.add(add__(3, 0)) if False else f.add(add__(0, 3), mov_(0, 3))
    n = r.choice([0, 1, 2, 3, 5, 7, 8, 9, 15, 16, 17, 20, 24, 31])
    two = r.random() < 0.25
    if two:
        f.add(mov_(3, 0))
        n2 = r.choice([1, 3, 8, 16, 20])
        if n: (shr_arith if signed_mul and r.random() < 0.6 else shr_logical)(f, 0, n)
        shr_logical(f, 3, n2)
        f.add(r.choice([xor_, add__, or__])(3, 0))
    else:
        if n: (shr_arith if (signed_mul and r.random() < 0.7) or r.random() < 0.15 else shr_logical)(f, 0, n)
    if r.random() < 0.25:
        # the CSE twin: the same extended value times the multiplier with the high bit folded in, made by mul.l (a product that wraps its 32 bits)
        cc = (c + (1 << r.choice([16, 24, 31]))) & M32
        f.lit(2, cc)
        f.add(mov_(3, src), mulL(2, 3), stsmacl(3)); shr_logical(f, 3, r.choice([16, 24, 31]))
        f.add(xor_(3, 0))
    t = r.randrange(0, 5)
    if t == 1: f.add(r.choice([extub, extuw, extsb, extsw])(0, 0))
    elif t == 2: f.add(andi(r.choice([0xFF, 0x7F, 0x0F, 0x01])))
    elif t == 3:
        f.lit(3, r.choice([0, 1, 10, 100, 0x1000, 0xFFFF, 0x7FFFFFFF]))
        f.add(r.choice([cmphi, cmpgt, cmpeq])(3, 0), movt(0))

# ------------------------------------------------------------------------------------------------------------------------------------ 68 div1len
def fam_div1len(r, f):
    n = r.randrange(1, 33) if r.random() < 0.8 else r.choice([1, 8, 15, 16, 17, 31, 32])
    signed = r.random() < 0.4
    setup = r.randrange(0, 5)
    if setup == 0:                         # 32/32 as the library routine
        f.add(mov_(0, 4))
        if signed: f.add(ROTCL | 0 << 8, CLRT, subc(1, 1), div0s(5, 1))
        else: f.add(movi(1, 0), div0u())
    elif setup == 1:                       # 16-bit quotient: dividend and divisor in the high halves
        f.add(mov_(0, 4), SHLL16 | 0 << 8, mov_(2, 5), SHLL16 | 2 << 8)
        if signed: f.add(ROTCL | 0 << 8, CLRT, subc(1, 1), div0s(2, 1))
        else: f.add(movi(1, 0), div0u())
        n = min(n, 16)
        for _ in range(n): f.add(ROTCL | 0 << 8, div1(2, 1))
        f.add(ROTCL | 0 << 8)
        f.add(r.choice([extuw, extsw])(0, 0)) if r.random() < 0.6 else None
        return
    elif setup == 2:                       # 64/32: r1:r0 = r4:r5, divisor r6
        f.add(mov_(1, 4), mov_(0, 5))
        if signed: f.add(div0s(6, 1))
        else: f.add(div0u())
        for _ in range(n): f.add(ROTCL | 0 << 8, div1(6, 1))
        f.add(ROTCL | 0 << 8)
        k = r.randrange(0, 4)
        if k == 1: f.add(mov_(0, 1))
        elif k == 2: f.add(xor_(1, 0))
        elif k == 3: f.add(cmpeq(7, 1), movt(0))
        return
    elif setup == 3:                       # the divisor a constant
        f.lit(2, r.choice([3, 7, 10, 100, 1000, 0x10000, 0x7FFFFFFF, 0x80000000, r.getrandbits(32) | 1]))
        f.add(mov_(0, 4))
        if signed: f.add(ROTCL | 0 << 8, CLRT, subc(1, 1), div0s(2, 1))
        else: f.add(movi(1, 0), div0u())
        for _ in range(n): f.add(ROTCL | 0 << 8, div1(2, 1))
        f.add(ROTCL | 0 << 8)
        return
    else:                                  # the divisor and the dividend narrowed
        f.add(extuw(0, 4) if r.random() < 0.5 else extub(0, 4), extub(2, 5))
        if signed: f.add(ROTCL | 0 << 8, CLRT, subc(1, 1), div0s(2, 1))
        else: f.add(movi(1, 0), div0u())
        for _ in range(n): f.add(ROTCL | 0 << 8, div1(2, 1))
        f.add(ROTCL | 0 << 8)
        return
    for _ in range(n): f.add(ROTCL | 0 << 8, div1(5, 1))
    f.add(ROTCL | 0 << 8)
    k = r.randrange(0, 5)
    if k == 1: f.add(not_(0, 0))
    elif k == 2: f.add(mov_(0, 1))
    elif k == 3: f.add(neg_(0, 0)) if signed else f.add(addi(0, 1))
    elif k == 4: f.add(SETT if False else 0x0008, 0x300E | 0 << 8 | 1 << 4)         # clrt ; addc r1,r0

# ------------------------------------------------------------------------------------------------------------------------------------ 69 cmov
def fam_cmov(r, f):
    width = r.choice([1, 2, 4, 4])
    pairs = [(0, 1), (1, 0), (0, -1), (-1, 0), (5, 6), (6, 5), (0, 2), (0, 0x80), (0, 0x8000), (0, 0x80000000), (-1, 1), (7, 7), (0x7F, 0x80), (0, 0xFF),
             (1, 0x100), (0x7FFF, 0x8000), (0xFFFF, 0), (r.getrandbits(16), r.getrandbits(16)), (r.getrandbits(32), r.getrandbits(32)), (0, 1 << r.randrange(0, 32))]
    c1, c2 = r.choice(pairs)
    shape = r.randrange(0, 6)
    f.lit(2, r.choice([0, 1, 0x7F, 0x80, 0xFF, 0x1000, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF, r.getrandbits(32)]))
    cond = r.randrange(0, 8)
    def emit_cond():
        if cond == 0: f.add(cmpeq(2, 4))
        elif cond == 1: f.add(cmphi(2, 4))
        elif cond == 2: f.add(cmpgt(2, 4))
        elif cond == 3: f.add(cmphs(2, 4))
        elif cond == 4: f.add(cmpge(2, 4))
        elif cond == 5: f.add(tstr(2, 4))
        elif cond == 6: f.add(cmppz(4))
        else: f.add(cmppl(4))
    def ld(reg, v):
        v &= M32
        if v < 0x80 or v >= 0xFFFFFF80: f.add(movi(reg, v))
        else: f.lit(reg, v)
    emit_cond()
    if shape == 0:                         # triangle: r0 = c1 ; if T: r0 = c2
        ld(0, c1); f.br('bf', 'k'); ld(0, c2); f.label('k')
    elif shape == 1:                       # diamond
        f.br('bt', 'a'); ld(0, c1); f.br('bra', 'k'); f.add(0x0009); f.label('a'); ld(0, c2); f.label('k')
    elif shape == 2:                       # the select of two registers
        f.add(mov_(0, 4), mov_(1, 5)); f.br('bf', 'k'); f.add(mov_(0, 1)); f.label('k')
    elif shape == 3:                       # arms that compute
        f.add(mov_(0, 4), mov_(1, 4)); f.br('bt', 'a'); f.add(addi(0, 1), neg_(0, 0)); f.br('bra', 'k'); f.add(0x0009); f.label('a'); f.add(addi(1, -1)); f.add(mov_(0, 1)); f.label('k')
    elif shape == 4:                       # one arm a constant, the other the input
        f.add(mov_(0, 4)); f.br('bf', 'k'); ld(0, c2); f.label('k')
    else:                                  # two conditions into one select
        f.add(movt(1)); ld(0, c1); cond2 = r.choice([cmpeq(5, 4), cmpgt(5, 4), cmphi(5, 4)]); f.add(cond2); f.br('bf', 'k'); f.add(tstr(1, 1)); f.br('bt', 'k'); ld(0, c2); f.label('k')
    if width == 1: f.add(r.choice([extub, extsb])(0, 0))
    elif width == 2: f.add(r.choice([extuw, extsw])(0, 0))
    if r.random() < 0.3: tail_common(r, f, 0, 3)

# ------------------------------------------------------------------------------------------------------------------------------------ 70 divdiv
def fam_divdiv(r, f):
    kind = r.randrange(0, 6)
    d1 = r.choice([3, 5, 7, 10, 100, 641, 1000, 65537, 0x10000, 0x7FFFFFFF, 0x80000001, 0xFFFFFFFF, 6, 12, 25, 60, 127])
    d2 = r.choice([3, 5, 7, 10, 100, 641, 1000, 65537, 0x10000, 0x20000, 0x7FFFFFFF, 0x80000001, 0xFFFFFFFF, 2, 4, 6, 255, 257])
    if kind == 0:                               # unsigned x / d1 / d2
        emit_udiv(r, f, d1, 4, 0, 2)
        f.add(mov_(1, 0)); emit_udiv(r, f, d2, 1, 0, 2)
    elif kind == 1:                             # signed
        e1 = r.choice([3, 5, 7, 10, 100, 641, 1000, 65537, -3, -7, -10, -100, -1000, 0x7FFFFFFF])
        e2 = r.choice([3, 5, 7, 10, 100, 641, 1000, 65537, -3, -7, -10, -100, -1000, 0x7FFFFFFF])
        emit_sdiv(r, f, e1, 4, 0, 2)
        f.add(mov_(1, 0)); emit_sdiv(r, f, e2, 1, 0, 2)
    elif kind == 2:                             # (x >> a) / d, unsigned
        f.add(mov_(1, 4)); shr_logical(f, 1, r.choice([1, 2, 3, 4, 8, 16])); emit_udiv(r, f, d1, 1, 0, 2)
    elif kind == 3:                             # x / 2^k / d: a signed division by a power of two (cmp/pz, add the bias, shar) then a magic one
        k = r.choice([1, 2, 3, 4, 8])
        f.add(mov_(1, 4), cmppz(1)); f.br('bt', 'p'); f.add(addi(1, (1 << k) - 1)) if (1 << k) - 1 < 128 else f.add(addi(1, 0)); f.label('p')
        shr_arith(f, 1, k)
        e = r.choice([3, 7, 10, 100, -3, -10])
        emit_sdiv(r, f, e, 1, 0, 2)
    elif kind == 4:                             # remainders: (x % d1) % d2 by multiply and subtract
        emit_udiv(r, f, d1, 4, 0, 2)
        f.lit(3, d1); f.add(mulL(3, 0), stsmacl(3), mov_(1, 4), sub__(3, 1))
        emit_udiv(r, f, max(d2, 3), 1, 0, 2)
        f.lit(3, max(d2, 3)); f.add(mulL(3, 0), stsmacl(3), sub__(3, 1), mov_(0, 1))
    else:                                       # x / d1 then the quotient shifted and divided again
        emit_udiv(r, f, d1, 4, 0, 2)
        f.add(mov_(1, 0)); shr_logical(f, 1, r.choice([1, 4, 8, 16])); emit_udiv(r, f, d2, 1, 0, 2)
    if r.random() < 0.25: tail_common(r, f, 0, 3)

# ------------------------------------------------------------------------------------------------------------------------------------ 71 varshift
def fam_varshift(r, f):
    # the accumulator is r1 (r0 holds the high word of a pair) and the result is handed back by the rts slot `mov r1,r0`, the compiled idiom for `return r1`: the
    # engine takes r0 for a return only then (A2-34), and a loop whose result is left in r0 by a `nop` slot is printed as `return;` by design
    form = r.randrange(0, 4)
    if form == 0:                               # rotate left by (r5 & 31), the amount 0 skipped
        f.add(mov_(1, 4), mov_(2, 5)); f.lit(3, 0x1F); f.add(and__(3, 2), tstr(2, 2)); f.br('bt', 'e')
        f.label('top'); f.add(ROTL | 1 << 8, dt(2)); f.br('bf', 'top'); f.label('e')
    elif form == 1:                             # a 64-bit shift left by (r6 & 63) of r4:r5, one bit at a time
        f.add(mov_(0, 4), mov_(1, 5), mov_(2, 6)); f.lit(3, 0x3F); f.add(and__(3, 2), tstr(2, 2)); f.br('bt', 'e')
        f.label('top'); f.add(SHLL | 1 << 8, ROTCL | 0 << 8, dt(2)); f.br('bf', 'top'); f.label('e')
        if r.random() < 0.5: f.add(mov_(1, 0))
    elif form == 2:                             # a right shift (logical or arithmetic) by a loop that counts down, the result tested
        f.add(mov_(1, 4), mov_(2, 5)); f.lit(3, 0x1F); f.add(and__(3, 2), tstr(2, 2)); f.br('bt', 'e')
        f.label('top'); f.add((SHLR if r.random() < 0.5 else SHAR) | 1 << 8, dt(2)); f.br('bf', 'top'); f.label('e')
        if r.random() < 0.4: f.add(tstr(1, 1), movt(1))
    else:                                       # shift by a variable amount through shifts of 8 and 1: (n >> 3) bytes then n & 7 bits
        f.add(mov_(1, 4), mov_(2, 5)); f.lit(3, 0x1F); f.add(and__(3, 2), mov_(3, 2)); shr_logical(f, 3, 3)
        f.add(tstr(3, 3)); f.br('bt', 'l1')
        f.label('l0'); f.add(SHLR8 | 1 << 8, dt(3)); f.br('bf', 'l0')
        f.label('l1'); f.add(mov_(3, 2)); f.add(movi(0, 7)); f.add(and__(0, 3), tstr(3, 3)); f.br('bt', 'e')
        f.label('l2'); f.add(SHLR | 1 << 8, dt(3)); f.br('bf', 'l2')
        f.label('e')
    f.rts_slot = mov_(0, 1)

# ------------------------------------------------------------------------------------------------------------------------------------ 73 signmod
def fam_signmod(r, f):
    # x % 2^k and x / 2^k for a signed x, the way the SH-2 compilers write them: the bias added to a negative x, masked or shifted, and for the remainder subtracted
    # (RuleSignMod2nOpt, RuleSignMod2nOpt2, RuleSignMod2Opt, RuleSignDiv2); the operand a byte, a word or a longword, every k from 1 to 30
    k = r.choice([1, 2, 3, 4, 5, 6, 7, 8, 9, 15, 16, 17, 23, 24, 30, r.randrange(1, 31)])
    pre = r.choice([0, 0, 1, 2, 3, 4])
    src = 4
    if pre == 1: f.add(extsb(1, 4)); src = 1
    elif pre == 2: f.add(extsw(1, 4)); src = 1
    elif pre == 3: f.add(extub(1, 4)); src = 1
    elif pre == 4: f.add(extuw(1, 4)); src = 1
    form = r.randrange(0, 5)
    bias = (1 << k) - 1
    def ld_bias(reg):
        if bias < 0x80: f.add(movi(reg, bias))
        else: f.lit(reg, bias)
    if form == 0:                      # x % 2^k with the branch: if x < 0: t = x + bias else t = x ; r = x - (t & ~bias)
        f.add(mov_(0, src), mov_(2, src), cmppz(2)); f.br('bt', 'p')
        ld_bias(3); f.add(add__(3, 2)); f.label('p')
        f.lit(3, (~bias) & M32); f.add(and__(3, 2), sub__(2, 0))
    elif form == 1:                    # x / 2^k with the branch
        f.add(mov_(0, src), cmppz(0)); f.br('bt', 'p')
        ld_bias(3); f.add(add__(3, 0)); f.label('p')
        shr_arith(f, 0, k)
    elif form == 2:                    # the branch-free bias: (x >> 31) >>> (32 - k)
        f.add(mov_(2, src)); shr_arith(f, 2, 31); shr_logical(f, 2, 32 - k)
        f.add(mov_(0, src), add__(0, 2)) if False else f.add(mov_(0, src), add__(2, 0))
        shr_arith(f, 0, k)
    elif form == 3:                    # x % 2^k branch-free
        f.add(mov_(2, src)); shr_arith(f, 2, 31); shr_logical(f, 2, 32 - k)
        f.add(mov_(3, src), add__(2, 3)); f.lit(2, (~bias) & M32); f.add(and__(2, 3), mov_(0, src), sub__(3, 0))
    else:                              # the remainder by the mask directly when x is known non-negative, after a test
        f.add(mov_(0, src)); f.lit(2, bias); f.add(and__(2, 0))
        f.add(cmppz(src)); f.br('bt', 'q'); f.add(tstr(0, 0)); f.br('bt', 'q'); f.lit(3, (~bias) & M32); f.add(or__(3, 0)); f.label('q')
    if r.random() < 0.3: tail_common(r, f, 0, 3)

# ------------------------------------------------------------------------------------------------------------------------------------ 74 deepsoup
def fam_deepsoup(r, f):
    # 15 to 45 steps over r0..r3 (r4..r7 are read-only inputs), the T flag produced and consumed in every way the SH-2 has (compares, tst, cmp/str, dt, shifts and
    # rotates through T, addc/subc/negc, addv/subv, div0u/div0s/div1, movt), the multiplies with their mach/macl reads, extensions and swaps, and forward
    # branches over one to three steps; the result is handed back by the rts slot `mov rX,r0`
    R = [0, 1, 2, 3]
    f.add(mov_(0, 4), mov_(1, 5), mov_(2, 6), mov_(3, 7))
    if r.random() < 0.3: f.add(0x0028)                               # clrmac
    if r.random() < 0.5: f.add(r.choice([0x0008, 0x0018]))             # clrt / sett
    n = r.randrange(15, 46)
    lab = 0
    pending = []                                                       # (label name, steps left)
    def src(): return r.choice(R + [4, 5, 6, 7])
    for _ in range(n):
        k = r.randrange(0, 44)
        a, b = r.choice(R), src()
        if k < 8: f.add(r.choice([add__, sub__, and__, or__, xor_, mov_, neg_, not_, swapb_, swapw_, xtrct, extub, extuw, extsb, extsw][r.randrange(0, 15)])(b, a)) if False else f.add(r.choice([0x300C, 0x3008, 0x2009, 0x200B, 0x200A, 0x6003, 0x600B, 0x6007, 0x6008, 0x6009, 0x200D, 0x600C, 0x600D, 0x600E, 0x600F]) | a << 8 | b << 4)
        elif k < 12: f.add(r.choice([SHLL, SHLR, SHAR, ROTL, ROTR, SHLL2, SHLR2, SHLL8, SHLR8, SHLL16, SHLR16]) | a << 8)
        elif k < 14: f.add(addi(a, r.choice([1, 2, -1, -2, 4, 0x7F, -0x80])))
        elif k < 15: f.add(movi(a, r.choice([0, 1, -1, 0x7F, -0x80, 0x55])))
        elif k < 17: f.lit(a, r.choice([0xFFFF, 0x10000, 0x80000000, 0x7FFFFFFF, 0xFF00FF00, r.getrandbits(32)]))
        elif k < 20: f.add(r.choice([cmpeq, cmphi, cmphs, cmpgt, cmpge, cmp_str, tstr])(b, a))
        elif k < 21: f.add(r.choice([cmppz, cmppl, dt])(a))
        elif k < 22: f.add(r.choice([0x8800 | r.choice([0, 1, 0x7F, 0x80, 0xFF]), 0xC800 | r.choice([1, 0x80, 0xFF, 0x0F])]))      # cmp/eq #imm,r0 ; tst #imm,r0
        elif k < 24: f.add(movt(a))
        elif k < 28: f.add(r.choice([addc, subc, addv, subv])(b, a))
        elif k < 30: f.add(negc(b, a))
        elif k < 33: f.add(r.choice([ROTCL, ROTCR, SHLL, SHLR]) | a << 8)
        elif k < 34: f.add(r.choice([0x0008, 0x0018]))
        elif k < 36: f.add(r.choice([div0u()]) if r.random() < 0.5 else div0s(src(), a))
        elif k < 38: f.add(div1(b, a))
        elif k < 40: f.add(r.choice([mulL, muluw, mulsw])(b, a), stsmacl(r.choice(R)))
        elif k < 41: f.add(r.choice([dmulu, dmuls])(b, a), stsmach(r.choice(R)), stsmacl(r.choice(R)))
        elif k < 42: f.add(0x401A | a << 8)                           # lds Rn,macl
        elif k < 43: f.add(0x400A | a << 8)                           # lds Rn,mach
        else:
            lab += 1
            nm = 'sk%d' % lab
            f.br(r.choice(['bt', 'bf']), nm)
            pending.append([nm, r.randrange(1, 4)])
            continue
        for p in pending: p[1] -= 1
        for p in [p for p in pending if p[1] <= 0]:
            f.label(p[0]); pending.remove(p)
    for p in pending: f.label(p[0])
    x, y = r.choice(R), r.choice(R)
    kind = r.randrange(0, 3)
    if kind == 1: f.add(r.choice([xor_, add__, or__])(y, x))
    f.rts_slot = mov_(0, x)


# ------------------------------------------------------------------------------------------------------------------------------------ 75 delaybr
def slot_word(r, regs=(0, 1, 2, 3)):
    """one instruction fit for a delay slot (no branch, no PC-relative load): arithmetic, moves, T producers and consumers, shifts, extensions, movt, dt"""
    a = r.choice(regs); b = r.choice(list(regs) + [4, 5, 6, 7])
    k = r.randrange(0, 14)
    if k == 0: return 0x300C | a << 8 | b << 4                 # add
    if k == 1: return 0x3008 | a << 8 | b << 4                 # sub
    if k == 2: return 0x6003 | a << 8 | b << 4                 # mov
    if k == 3: return 0x2009 | a << 8 | b << 4                 # and
    if k == 4: return 0x200A | a << 8 | b << 4                 # xor
    if k == 5: return r.choice([SHLL, SHLR, SHAR, ROTL, ROTR, ROTCL, ROTCR]) | a << 8
    if k == 6: return 0x7000 | a << 8 | r.choice([1, 2, 0xFF, 0xFE, 0x7F])      # add #imm
    if k == 7: return 0x0029 | a << 8                          # movt
    if k == 8: return r.choice([0x300E, 0x300A, 0x300F, 0x300B]) | a << 8 | b << 4   # addc subc addv subv
    if k == 9: return 0x4010 | a << 8                          # dt
    if k == 10: return r.choice([0x3000, 0x3006, 0x3003]) | a << 8 | b << 4           # cmp/eq cmp/hi cmp/ge (T after the branch decided)
    if k == 11: return r.choice([0x600C, 0x600E, 0x600D, 0x600F, 0x6008, 0x6009]) | a << 8 | b << 4
    if k == 12: return 0x600B | a << 8 | b << 4               # neg
    return 0x200D | a << 8 | b << 4                            # xtrct

def fam_delaybr(r, f):
    form = r.randrange(0, 6)
    if form == 0:         # counted loop closed by bf/s with a working slot; the slot's T is read after the loop
        f.add(mov_(0, 4), mov_(1, 5), movi(2, r.randrange(1, 9)))
        f.label('top')
        for _ in range(r.randrange(1, 3)): f.add(slot_word(r, (0, 1)))
        f.add(dt(2)); f.br('bfs', 'top'); f.add(slot_word(r, (0, 1)))
        if r.random() < 0.6: f.add(movt(3), xor_(3, 0))
    elif form == 1:       # forward bt/s over an add, the slot executes on both paths
        f.add(mov_(0, 4), mov_(1, 5), r.choice([cmpeq, cmphi, cmpgt, cmpge, cmphs])(1, 0))
        f.br('bts', 'done'); f.add(slot_word(r, (0, 1)))
        f.add(add__(1, 0), slot_word(r, (0,)) if r.random() < 0.5 else 0x0009)
        f.label('done')
        if r.random() < 0.5: f.add(movt(1), add__(1, 0))
    elif form == 2:       # bf/s forward, slot a T consumer
        f.add(mov_(0, 4), mov_(1, 5), mov_(2, 6), r.choice([cmpeq, cmphi, cmpgt])(1, 0))
        f.br('bfs', 'skip'); f.add(r.choice([addc(2, 0), subc(2, 0), negc(0, 0), ROTCL | 0 << 8, movt(0)]))
        f.add(add__(2, 0), xor_(1, 0))
        f.label('skip')
    elif form == 3:       # bra with a slot, a backward loop of two exits
        f.add(mov_(0, 4), mov_(1, 5), movi(2, r.randrange(1, 6)))
        f.br('bra', 'enter'); f.add(slot_word(r, (0, 1)))
        f.label('body'); f.add(slot_word(r, (0, 1)), slot_word(r, (0, 1)))
        f.label('enter'); f.add(dt(2)); f.br('bfs', 'body'); f.add(slot_word(r, (0, 1, 3)))
        f.add(add__(3, 0)) if r.random() < 0.5 else None
    elif form == 4:       # two chained delayed branches, the second taken only if the first was not
        f.add(mov_(0, 4), mov_(1, 5), cmpeq(1, 0))
        f.br('bts', 'a'); f.add(slot_word(r, (0, 1, 2)))
        f.add(cmpgt(1, 0)); f.br('bfs', 'b'); f.add(slot_word(r, (0, 1, 2)))
        f.add(slot_word(r, (0, 1)))
        f.label('a'); f.add(slot_word(r, (0, 1)))
        f.label('b'); f.add(add__(2, 0), add__(1, 0))
    else:                 # a loop with bt/s and the exit test on the value, slot decrementing the count
        f.add(mov_(0, 4), movi(2, 0), mov_(1, 5), r.choice([SHLR | 1 << 8, SHAR | 1 << 8]))
        f.label('top'); f.add(add__(1, 2), SHLL | 0 << 8, tstr(0, 0), r.choice([cmppz(0), cmppl(0), tstr(1, 1)]))
        f.br(r.choice(['bts', 'bfs']), 'out'); f.add(addi(2, 1))
        f.add(cmpgt(1, 2)); f.br('bfs', 'top'); f.add(slot_word(r, (0, 1)))
        f.label('out'); f.add(add__(2, 0))
    f.rts_slot = mov_(0, r.choice([0, 1, 2])) if False else 0x0009


# ------------------------------------------------------------------------------------------------------------------------------------ 76 condprop
def nt_op(r, a, b):
    """an instruction that leaves T alone: add, sub, and, or, xor, mov, neg, not, extensions, swaps, mul.l + sts macl"""
    k = r.randrange(0, 10)
    if k < 6: return [r.choice([0x300C, 0x3008, 0x2009, 0x200B, 0x200A, 0x6003]) | a << 8 | b << 4]
    if k == 6: return [r.choice([0x600B, 0x6007, 0x600C, 0x600D, 0x600E, 0x600F, 0x6008, 0x6009]) | a << 8 | b << 4]
    if k == 7: return [0x7000 | a << 8 | r.choice([1, 2, 3, 0xFF, 0xFE, 0x10])]
    if k == 8: return [mulL(b, a), stsmacl(a)]
    return [0xE000 | a << 8 | r.choice([0, 1, 5, 0x7F, 0x80, 0xFF])]

def fam_condprop(r, f):
    # one condition computed once and read by several branches with T-preserving code between them (the later branches are redundant), or recomputed with a
    # related constant; equality tests whose true arm uses the tested register (a constant there); chains of equality tests
    form = r.randrange(0, 5)
    f.add(mov_(0, 4), mov_(1, 5), mov_(2, 6))
    if form == 0:         # the same T read by two or three branches
        f.add(r.choice([cmpeq, cmphi, cmpgt, cmpge, cmphs, tstr])(1, 0))
        for i in range(r.randrange(2, 4)):
            f.br(r.choice(['bt', 'bf']), 'j%d' % i)
            for _ in range(r.randrange(1, 3)): f.add(*nt_op(r, r.choice([0, 2, 3]), r.choice([0, 1, 2, 4, 5])))
            f.label('j%d' % i)
            for _ in range(r.randrange(0, 2)): f.add(*nt_op(r, r.choice([0, 2, 3]), r.choice([0, 1, 2, 4, 5])))
    elif form == 1:       # x == c, then x used in the true arm
        c = r.choice([0, 1, 5, 0x7F, 0x80, 0xFF, 2, 0x10])
        f.add(0x8800 | (c & 0xFF)) if c < 0x80 or c == 0xFF else f.add(0x8800 | (c & 0xFF))
        f.br(r.choice(['bf', 'bt']), 'e')
        f.add(add__(0, 2), mov_(3, 0)); shl(f, 3, r.choice([1, 2, 8])); f.add(xor_(3, 2), extub(2, 2) if r.random() < 0.5 else 0x6003 | 2 << 8 | 2 << 4)
        f.label('e')
        f.add(add__(2, 0))
    elif form == 2:       # a chain of equality tests of the same register
        cs = r.sample([0, 1, 2, 3, 5, 7, 0x10, 0x40, 0x7F, 0x80], 3)
        for i, c in enumerate(cs):
            f.add(0x8800 | (c & 0xFF)); f.br('bf', 'n%d' % i)
            f.add(movi(3, (i + 1) * 11), add__(3, 2))
            f.br('bra', 'end'); f.add(0x0009)
            f.label('n%d' % i)
        f.add(mov_(3, 0)); f.add(add__(1, 3))
        f.label('end')
        f.add(mov_(2, 3))
    elif form == 3:       # a test, code that recomputes T from a nearby constant, a second test
        f.add(cmphi(1, 0)); f.br('bf', 'a'); f.add(*nt_op(r, 2, 0)); f.label('a')
        f.add(addi(1, 1), cmphs(1, 0)); f.br('bt', 'b'); f.add(*nt_op(r, 2, 1)); f.label('b')
        f.add(cmpgt(1, 0)); f.br('bf', 'c'); f.add(*nt_op(r, 2, 4)); f.label('c')
    else:                 # a loop whose exit test is repeated in its body
        f.add(movi(3, r.randrange(2, 7)))
        f.label('top'); f.add(dt(3), movt(1))
        f.br('bt', 'out'); f.add(*nt_op(r, 0, 3)); f.add(tstr(3, 3)); f.br('bt', 'out'); f.add(*nt_op(r, 2, 0))
        f.br('bra', 'top'); f.add(0x0009)
        f.label('out'); f.add(add__(1, 2))
    f.add(*nt_op(r, 0, 2)) if r.random() < 0.5 else f.add(xor_(2, 0))


# ------------------------------------------------------------------------------------------------------------------------------------ 77 multiword
def fam_multiword(r, f):
    # a 96- or 128-bit value (r5..r7 or r4..r7, high word first) copied to r1..r3 or r0..r3: a 32-bit constant added through the carries, the value added to itself,
    # negated, a rotated copy of its words subtracted, the borrow of that read as a compare, shifted one bit right or left through T; the words combined to one result
    nw = r.choice([3, 4])
    srcs = [4, 5, 6, 7][4 - nw:]
    dst = [0, 1, 2, 3][4 - nw:]
    for d_, s_ in zip(dst, srcs): f.add(mov_(d_, s_))
    op = r.randrange(0, 7)
    rot = srcs[1:] + srcs[:1]
    if op == 0:             # += a 32-bit constant, the carry through the other words
        f.lit(7, r.choice([1, 0xFFFFFFFF, 0x80000000, 0x7FFFFFFF, r.getrandbits(32)])); f.add(movi(6, 0), 0x0008, addc(7, dst[-1]))
        for d_ in reversed(dst[:-1]): f.add(addc(6, d_))
    elif op == 1:           # the value added to itself
        f.add(0x0008)
        for d_ in reversed(dst): f.add(addc(d_, d_))
    elif op == 2:           # negated
        f.add(0x0008)
        for d_ in reversed(dst): f.add(negc(d_, d_))
    elif op == 3 or op == 4:   # minus a rotated copy of its own words; the borrow read for the compare
        f.add(0x0008)
        for d_, s_ in zip(reversed(dst), reversed(rot)): f.add(subc(s_, d_))
        if op == 4:
            f.add(movt(7))
            f.add(mov_(0, 7))
            return
    elif op == 5:           # shifted right one bit through T (arithmetic or logical on the top word)
        f.add((SHAR if r.random() < 0.4 else SHLR) | dst[0] << 8)
        for d_ in dst[1:]: f.add(ROTCR | d_ << 8)
    else:                   # shifted left one bit through T
        f.add(SHLL | dst[-1] << 8)
        for d_ in reversed(dst[:-1]): f.add(ROTCL | d_ << 8)
    k = r.randrange(0, 4)
    if k == 0: f.add(mov_(0, dst[0])) if dst[0] != 0 else None
    elif k == 1:
        for d_ in dst:
            if d_ != 0: f.add(xor_(d_, 0)) if 0 not in dst else f.add(xor_(d_, dst[0]))
    elif k == 2:
        for d_ in dst[1:]: f.add(add__(d_, dst[0]))
        if dst[0] != 0: f.add(mov_(0, dst[0]))
    else: f.add(mov_(0, dst[-1]))

# ------------------------------------------------------------------------------------------------------------------------------------ 72 mix
MIXFAMS = None

def fam_mix(r, f):
    global MIXFAMS
    if MIXFAMS is None:
        MIXFAMS = [fam_shmul, fam_muldi, fam_shift64, fam_cmpstr, fam_bitops, fam_divterm, fam_div1len, fam_cmov, fam_divdiv, fam_varshift, fam_signmod,
                   d13.fam_lerp, d13.fam_mulhsum, d13.fam_mulshift, d13.fam_negshift, d13.fam_shdiv, d13.fam_narrow]
    nb = r.choice([2, 2, 3])
    for b in range(nb):
        sub = Fn(r)
        sub.d_hint = None
        r.choice(MIXFAMS)(r, sub)
        # unique labels per block
        for c in sub.w:
            if isinstance(c, tuple) and c[0] == 'label': f.w.append(('label', c[1] + '_b%d' % b))
            elif isinstance(c, tuple) and c[0] == 'br': f.w.append(('br', c[1], c[2] + '_b%d' % b))
            else: f.w.append(c)
        if b != nb - 1:
            f.add(mov_(r.choice([4, 4, 5]), 0))

FAMS14 = {62: fam_shmul, 63: fam_muldi, 64: fam_shift64, 65: fam_cmpstr, 66: fam_bitops, 67: fam_divterm, 68: fam_div1len, 69: fam_cmov, 70: fam_divdiv,
          71: fam_varshift, 72: fam_mix, 73: fam_signmod, 74: fam_deepsoup, 75: fam_delaybr, 76: fam_condprop, 77: fam_multiword}

def build(seed, fam=None, want_fn=False):
    r = random.Random(seed * 104729 + 14)
    f = Fn(r)
    f.d_hint = None
    fm = FAMS14[fam] if fam is not None else FAMS14[r.choice(sorted(FAMS14))]
    fm(r, f)
    f.add(0x000B, getattr(f, 'rts_slot', 0x0009))
    if want_fn: return f
    return finish(f)

def lits_of14(f):
    out = []
    for c in f.w:
        if isinstance(c, tuple) and c[0] == 'lit': out.append(c[2] & M32)
    return out

def extras(first, count, fam):
    vals = set()
    for s in range(first, first + count):
        f = build(s, fam, want_fn=True)
        for v in lits_of14(f):
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
