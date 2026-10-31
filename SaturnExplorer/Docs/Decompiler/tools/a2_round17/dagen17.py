#!/usr/bin/env python3
# Area 2 round 17 instrument. Register-only functions for the printed-C differential on narrow compares (patch 0071, A2-104) and memory functions for the
# printed-C memory mode, drawn the way dagen16.py draws them (loads dagen16.py, so families 0..99 are still there):
#  100 ncmp     a value cut to 8 or 16 bits by up to three nested extensions (exts.b, exts.w, extu.b, extu.w, with an add, a shift or a swap between), compared
#               with a constant of either sign on the right or on the left of the compare (cmp/eq, cmp/hs, cmp/hi, cmp/ge, cmp/gt with the registers either way,
#               cmp/eq #imm, cmp/pz, cmp/pl), read as movt, as a branch around an assignment, or as a difference
#  101 ncmpx    the same against another narrow value or an expression: two cut values of different width and sign, a cut value and a sum, a cut value against a
#               cut constant sum
#  102 nloop    a loop whose counter or accumulator is cut to a byte or word and compared with a constant at the loop test or inside the body
#  103 nsel     a cut value compared and then selected on (two compares of the same value with constants of opposite signs, summed)
#  104 memmix   (memory mode, SE_GENERATED_PRINTEDC_BE=1) one record read and written at mixed widths by overlapping accesses: a word stored and a byte read back, a half-word
#               over a byte pair (the machine is big-endian and the host is not: the big-endian reading, A2-110)
#  105 memchain (memory mode, BE) post-increment and pre-decrement chains that change width where the alignment allows
#  106 memalias (memory mode, BE) a store through r4 at one width and a load through r5 at another (r5 equal to r4, a word apart or unrelated)
#  107 regaddr  addresses of hardware registers as plain numbers in sums, masks, compares, products, shifts, returned (patch 0073)
#  108 nexpr    a narrow named variable against a narrow negated, complemented, masked, shifted or offset expression, every compare, both orders (A2-113)
#  109 ncmp4    family 100 with the source always r4, for SE_GENERATED_PRINTEDC_SWEEP16=1 (r4 = every value 0..65535)
#  110 memloopmix (memory mode, BE) loops that change width on the same bytes: bytes assembled into a word, a word taken apart, half-words summed, a byte copy read as a word
#  111 memcmp   (memory mode, width 1 or 2) a byte or half-word loaded and compared with a constant of either sign on either side
# usage: dagen17.py <first seed> <count> [family 100..111]   -> one function per seed, ";" joined (SE_GENERATED_PRINTEDC_WORDS)
#        dagen17.py show <seed> <family>
import importlib.util, os, random, sys

HERE17 = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("dagen16", os.path.join(HERE17, "..", "a2_round16", "dagen16.py"))
d16 = importlib.util.module_from_spec(spec)
spec.loader.exec_module(d16)
globals().update({k: v for k, v in vars(d16).items() if not k.startswith('__')})
FAMS17 = {}

CUT = [extsb, extsw, extub, extuw]
# constants a cut value is compared with: the limits of the cut, both signs, and a few outside it
C8 = [0, 1, 2, 0x7E, 0x7F, 0x80, 0x81, 0xFE, 0xFF, 0x100, 0x101, 0xFFFFFF00, 0xFFFFFF7F, 0xFFFFFF80, 0xFFFFFF81, 0xFFFFFFFE, 0xFFFFFFFF, 0x7FFFFFFF, 0x80000000, 0x1FF]
C16 = [0, 1, 0x7FFE, 0x7FFF, 0x8000, 0x8001, 0xFFFE, 0xFFFF, 0x10000, 0x10001, 0xFFFF0000, 0xFFFF7FFF, 0xFFFF8000, 0xFFFF8001, 0xFFFFFFFE, 0xFFFFFFFF, 0x7FFFFFFF, 0x80000000, 0x1FFFF, 0xFF, 0x80, 0xFFFFFF80]


def consts(r, k):
    return r.choice(C8 if k in (extsb, extub) else C16)


def cutchain(r, f, dst, src):
    """dst = src cut by 1 to 3 nested extensions (an add, a shift or a swap between them sometimes); returns the last cut"""
    f.add(mov_(dst, src))
    last = r.choice(CUT)
    n = r.choice([1, 1, 2, 2, 3])
    for i in range(n):
        last = r.choice(CUT)
        if i > 0 and r.random() < 0.4:
            m = r.randrange(0, 5)
            if m == 0: f.add(0x7000 | dst << 8 | r.choice([1, 0x7F, 0x80, 0xFF, 0xFE]))
            elif m == 1: f.add(r.choice([0x4008, 0x4009, 0x4018, 0x4019, 0x4000, 0x4001, 0x4021]) | dst << 8)
            elif m == 2: f.add(swapb_(dst, dst))
            elif m == 3: f.add(neg_(dst, dst))
            else: f.add(not_(dst, dst))
        f.add(last(dst, dst))
    return last


def cmpkinds():
    return [cmpeq, cmphi, cmphs, cmpgt, cmpge]


def usecmp(r, g, f):
    """read T as movt (maybe negated), a branch around an assignment, or a difference; result in r0"""
    c = r.randrange(0, 3)
    if c == 0:
        f.add(movt(0))
        if r.random() < 0.3: f.add(neg_(0, 0))
    elif c == 1:
        l = g.lab('Z'); f.add(mov_(0, 6))
        f.br(r.choice(['bt', 'bf']), l); f.add(mov_(0, 7)); f.label(l)
    else:
        f.add(movt(0), mov_(2, 4), add__(2, 0)) if False else f.add(movt(0), add__(4, 0))


def fam_ncmp(r):
    g = Gen(r); f = g.f
    nt = r.choice([1, 1, 1, 2])
    for i in range(nt):
        src = r.choice([4, 5, 6, 7])
        last = cutchain(r, f, 1, src)
        form = r.randrange(0, 8)
        c = consts(r, last)
        if form < 3:        # value op const: cmp/X rconst,rvalue
            f.lit(3, c); f.add(r.choice(cmpkinds())(3, 1))
        elif form < 6:      # const op value
            f.lit(3, c); f.add(r.choice(cmpkinds())(1, 3))
        elif form == 6:     # cmp/eq #imm,r0
            f.add(mov_(0, 1), 0x8800 | r.choice([0, 1, 0x7F, 0x80, 0xFF, 0xFE]))
        else:
            f.add(r.choice([cmppz, cmppl])(1))
        if i + 1 < nt:
            f.add(movt(2) if i == 0 else movt(3))
            if i == 0: f.add(mov_(8, 2))
            else: f.add(add__(8, 3), mov_(8, 3))
    usecmp(r, g, f)
    if nt == 2: f.add(add__(8, 0))
    f.rts_slot = NOP
    return f


def fam_ncmpx(r):
    g = Gen(r); f = g.f
    a = cutchain(r, f, 1, r.choice([4, 5]))
    b = cutchain(r, f, 2, r.choice([6, 7]))
    form = r.randrange(0, 6)
    if form == 0:      # two cut values
        f.add(r.choice(cmpkinds())(2, 1))
    elif form == 1:    # cut value against a sum of the other and a constant
        f.add(0x7000 | 2 << 8 | r.choice([1, 0x7F, 0x80, 0xFF, 0xFE]), r.choice(cmpkinds())(2, 1))
    elif form == 2:    # cut value against the sum of both, cut again
        f.add(add__(1, 2), r.choice(CUT)(2, 2), r.choice(cmpkinds())(2, 1))
    elif form == 3:    # cut difference against a constant
        f.add(sub__(2, 1), r.choice(CUT)(1, 1)); f.lit(3, consts(r, a)); f.add(r.choice(cmpkinds())(r.choice([1, 3]), r.choice([3, 1])) if False else r.choice(cmpkinds())(3, 1))
    elif form == 4:    # cut product low half against a constant
        f.add(mulL(1, 2), stsmacl(1), r.choice(CUT)(1, 1)); f.lit(3, consts(r, a)); f.add(r.choice(cmpkinds())(1, 3))
    else:              # a cut value ANDed with a mask then compared with the cut of a constant sum
        f.add(0xC900 | r.choice([0x7F, 0x80, 0xFF, 0x3F]) if False else and__(2, 1), r.choice(CUT)(1, 1)); f.lit(3, consts(r, a)); f.add(r.choice(cmpkinds())(3, 1))
    usecmp(r, g, f)
    f.rts_slot = NOP
    return f


def fam_nloop(r):
    g = Gen(r); f = g.f
    n = r.randrange(2, 6)
    f.add(movi(8, 0), mov_(1, 4), movi(9, n))
    cut = r.choice(CUT)
    top = g.lab('T'); f.label(top)
    f.add(add__(5, 1) if r.random() < 0.5 else 0x7100 | r.choice([1, 0x7F, 0x80, 0xFF, 0xFE]))
    f.add(cut(2, 1))
    inner = r.random() < 0.4
    if inner:      # a compare in the body: count the values over a constant
        f.lit(3, consts(r, cut)); f.add(r.choice(cmpkinds())(3, 2))
        l = g.lab('Y'); f.br(r.choice(['bt', 'bf']), l); f.add(0x7800 | 0x01); f.label(l)    # r8 += 1
    f.add(0x7800 | 0x01) if not inner else None
    # fuel, then the exit test: the cut counter against a constant
    out = g.lab('O'); f.add(dt(9)); f.br('bt', out)
    if r.random() < 0.5:
        f.add(mov_(0, 2)); f.add(0x8800 | r.choice([0, 1, 0x7F, 0x80, 0xFF, 0xFE]))
        f.br('bf', top)
    else:
        f.lit(3, consts(r, cut)); f.add(r.choice([cmpeq, cmphi, cmpgt])(3, 2)); f.br(r.choice(['bt', 'bf']), top)
    f.label(out); f.add(mov_(0, 8), add__(1, 0))
    f.rts_slot = NOP
    return f


def fam_nsel(r):
    g = Gen(r); f = g.f
    last = cutchain(r, f, 1, r.choice([4, 5, 6, 7]))
    ks = []
    f.lit(3, consts(r, last)); f.add(r.choice(cmpkinds())(3, 1), movt(2))
    f.lit(3, consts(r, last)); f.add(r.choice(cmpkinds())(1, 3), movt(0))
    f.add(r.choice([add__, sub__, xor_, or__, and__])(2, 0))
    if r.random() < 0.4:
        l = g.lab('Z'); f.br(r.choice(['bt', 'bf']), l); f.add(neg_(0, 0)); f.label(l)
    f.rts_slot = NOP
    return f


FAMS17.update({100: fam_ncmp, 101: fam_ncmpx, 102: fam_nloop, 103: fam_nsel})


# register addresses of the machine (SH-2 on-chip, SCU, VDP1/2, SMPC, SCSP, the cache-through mirrors), as plain numbers: patch 0073 (ActionConstantPtr) takes
# a number for the address of a hardware register only where the data flow uses it as one
REGADDR = [0xFFFFFE92, 0xFFFFFE10, 0xFFFFFE11, 0xFFFFFE00, 0xFFFFFEFF, 0xFFFFFF00, 0xFFFFFF8C, 0xFFFFFF62, 0xFFFFFF80, 0xFFFFFFB0, 0xFFFFFFF8, 0xFFFFFFFC, 0xFFFFFFFE,
           0xFFFFFFFF, 0xFFFFFE88, 0xFFFFFE4C, 0xFFFFFF40, 0xFFFFFFA0, 0xFFFFFFE0, 0xFFFFFFE4, 0xFFFFFFF0, 0xFFFFFFC0,
           0x25FE0000, 0x25FE0020, 0x25FE00A0, 0x25FE00B0, 0x25FE00A4, 0x25F80000, 0x25F80004, 0x25F8000E, 0x25D00000, 0x25D00010, 0x25D0001E, 0x25B00400, 0x25B00000,
           0x20100061, 0x20100063, 0x2010001F, 0x20100031, 0x20100075, 0x2010007D, 0x25890000, 0x05FE00A0, 0x05F80000, 0x45FE0000, 0x00FE0000, 0xFFFF8000, 0xFFFF0000]


def fam_regaddr(r):
    """a register's address as a plain number: sums, differences, masks, compares, products, shifts, a return of the number itself"""
    g = Gen(r); f = g.f
    f.add(mov_(1, r.choice([4, 5, 6, 7])))
    for _ in range(r.choice([1, 2, 2, 3])):
        a = r.choice(REGADDR)
        if r.random() < 0.15: a = (a + r.choice([-4, -1, 1, 2, 4, 0x10, 0x100])) & 0xFFFFFFFF
        f.lit(2, a)
        k = r.randrange(0, 14)
        if k == 0: f.add(add__(2, 1))
        elif k == 1: f.add(sub__(2, 1))
        elif k == 2: f.add(sub__(1, 2), mov_(1, 2))
        elif k == 3: f.add(and__(2, 1))
        elif k == 4: f.add(or__(2, 1))
        elif k == 5: f.add(xor_(2, 1))
        elif k == 6: f.add(mulL(2, 1), stsmacl(1))
        elif k == 7: f.add(r.choice([SHLL, SHLR, SHAR, 0x4008, 0x4009, 0x4018, 0x4019, 0x4028, 0x4029]) | 2 << 8, add__(2, 1))
        elif k == 8: f.add(r.choice([cmpeq, cmphi, cmphs, cmpgt, cmpge])(2, 1), movt(3), add__(3, 1))
        elif k == 9: f.add(neg_(2, 2), add__(2, 1))
        elif k == 10: f.add(r.choice([extsb, extsw, extub, extuw])(2, 2), add__(2, 1))
        elif k == 11: f.add(mov_(1, 2))                   # the number itself
        elif k == 12: f.add(r.choice([cmpeq, cmphi, cmphs, cmpgt, cmpge])(2, 1)); l = g.lab('Z'); f.br(r.choice(['bt', 'bf']), l); f.lit(1, r.choice(REGADDR)); f.label(l)
        else: f.add(not_(2, 2), and__(2, 1))
    f.add(mov_(0, 1))
    f.rts_slot = NOP
    return f


FAMS17[107] = fam_regaddr


def fam_nexpr(r):
    """A2-113's shape: a narrow named variable (a cut of a parameter, used again below so that it stays a variable) against a narrow expression (a cut of a negated,
    complemented, masked, shifted or offset value, the compare reduced to the cut's width by the engine), every compare and both orders"""
    g = Gen(r); f = g.f
    w = r.choice(['b', 'w'])
    ca = r.choice([extsb, extub] if w == 'b' else [extsw, extuw])
    cb = r.choice([extsb, extub] if w == 'b' else [extsw, extuw])
    f.add(mov_(1, r.choice([4, 5])), ca(1, 1))
    f.add(mov_(2, r.choice([6, 7])))
    k = r.randrange(0, 9)
    if k == 0: f.add(mov_(0, 2), andi(r.choice([0xFF, 0x7F, 0x0F])), mov_(2, 0), neg_(2, 2))
    elif k == 1: f.add(extub(2, 2), neg_(2, 2))
    elif k == 2: f.add(extuw(2, 2), neg_(2, 2))
    elif k == 3: f.add(extsb(2, 2), neg_(2, 2))
    elif k == 4: f.add(extub(2, 2), not_(2, 2))
    elif k == 5: f.add(extub(2, 2), 0x7200 | r.choice([1, 0x7F, 0x80, 0xFF, 0xFE]))
    elif k == 6: f.add(extuw(2, 2), r.choice([0x4008, 0x4009, 0x4018, 0x4019]) | 2 << 8)
    elif k == 7: f.add(extsw(2, 2), r.choice([0x4001, 0x4021, 0x4000]) | 2 << 8)
    else: f.add(mov_(0, 2), andi(r.choice([0xFF, 0x7F])), mov_(2, 0), mulL(2, 2), stsmacl(2))
    f.add(cb(2, 2))
    kind = r.choice(cmpkinds())
    f.add(kind(2, 1) if r.random() < 0.5 else kind(1, 2))
    usecmp(r, g, f)
    if r.random() < 0.5: f.add(add__(1, 0))          # the variable is used again
    f.rts_slot = NOP
    return f


FAMS17[108] = fam_nexpr


# ------------------------------------------------------------------------------------------------------------------------------------ 104..106 memory, mixed widths
# For the memory mode with the big-endian reading (SE_GENERATED_PRINTEDC_BE=1, SE_GENERATED_PRINTEDC_PTR=4): accesses of different widths to the same bytes. r4 and r5 point
# into the arena (r5 is r4, 4k bytes either side of it, r4 + 8 or a second area), r8 is the working pointer. Every access is aligned for its width (the offset of the
# working pointer is tracked, so a post-increment or pre-decrement chain changes width only where the alignment allows).
LD = {1: 0x6000, 2: 0x6001, 4: 0x6002}            # mov.X @Rm,Rn
LDI = {1: 0x6004, 2: 0x6005, 4: 0x6006}           # mov.X @Rm+,Rn
ST = {1: 0x2000, 2: 0x2001, 4: 0x2002}            # mov.X Rm,@Rn
STD = {1: 0x2004, 2: 0x2005, 4: 0x2006}           # mov.X Rm,@-Rn
DREGS = [1, 2, 3, 6, 7]


class MixMem:
    def __init__(self, r):
        self.r = r; self.g = Gen(r); self.f = self.g.f
        self.f.add(mov_(1, 6), mov_(2, 7), mov_(3, 6))
        self.res = []

    def point(self, base, off):
        self.f.add(mov_(8, base)); 
        while off:
            step = min(off, 127) if off > 0 else max(off, -128)
            self.f.add(0x7800 | (step & 0xFF)); off -= step

    def load(self, base, off, w, dst):
        self.point(base, off); self.f.add(LD[w] | dst << 8 | 8 << 4)

    def store(self, base, off, w, src):
        self.point(base, off); self.f.add(ST[w] | 8 << 8 | src << 4)

    def acc(self):
        r = self.r
        w = r.choice([1, 2, 4, 4, 2]); off = r.randrange(0, 32 // w) * w
        return r.choice([4, 4, 5]), off, w

    def finish(self):
        r = self.r; f = self.f
        parts = r.sample(DREGS[:3], r.randrange(1, 4))
        f.add(mov_(0, parts[0]))
        for p in parts[1:]: f.add(r.choice([add__, xor_, sub__])(p, 0))
        f.rts_slot = NOP


def fam_memmix(r):
    m = MixMem(r); f = m.f
    for _ in range(r.randrange(4, 11)):
        b, off, w = m.acc()
        if r.random() < 0.5: m.store(b, off, w, r.choice(DREGS))
        else:
            m.load(b, off, w, r.choice([1, 2, 3]))
            if r.random() < 0.3: f.add(op2(r, r.choice([1, 2, 3]), r.choice([1, 2, 3, 6, 7]))[0])
    m.finish()
    return f


def fam_memchain(r):
    m = MixMem(r); f = m.f
    off = r.choice([0, 4, 8, 12, 16]) 
    m.point(4 if r.random() < 0.7 else 5, off)
    for _ in range(r.randrange(2, 5)):
        up = r.random() < 0.6
        w = r.choice([1, 2, 4])
        # bring the pointer to an alignment of w
        while off % w:
            if up: f.add(LDI[1] | 3 << 8 | 8 << 4); off += 1
            else: f.add(STD[1] | 8 << 8 | 7 << 4); off -= 1
        for _ in range(r.randrange(1, 5)):
            if (off + (0 if up else -w)) % w or off - (0 if up else w) < 0 or off + (w if up else 0) > 60: break
            if up:
                f.add(LDI[w] | r.choice([1, 2, 3]) << 8 | 8 << 4); off += w
            else:
                f.add(STD[w] | 8 << 8 | r.choice([1, 2, 3, 6, 7]) << 4); off -= w
        if r.random() < 0.4: f.add(ST[r.choice([1, 2, 4])] | 4 << 8 | r.choice([1, 2, 3]) << 4) if False else f.add(op2(r, r.choice([1, 2, 3]), r.choice([1, 2, 3, 6, 7]))[0])
    m.finish()
    return f


def fam_memalias(r):
    m = MixMem(r); f = m.f
    for _ in range(r.randrange(3, 8)):
        wa = r.choice([1, 2, 4]); wb = r.choice([1, 2, 4])
        off = r.randrange(0, 8) * 4
        a = 4; b = 5
        # a store through one pointer at one width, a load through the other at another; the two pointers are the same, a word apart, or unrelated
        sa = r.choice(DREGS)
        oa = off + r.randrange(0, 4 // wa) * wa
        m.store(a, oa, wa, sa)
        ob = off + r.randrange(0, 4 // wb) * wb
        m.load(b, ob, wb, r.choice([1, 2, 3]))
        if r.random() < 0.4: f.add(op2(r, r.choice([1, 2, 3]), r.choice([1, 2, 3, 6, 7]))[0])
    m.finish()
    return f


FAMS17.update({104: fam_memmix, 105: fam_memchain, 106: fam_memalias})


def fam_ncmp4(r):
    """100 and 103 with the source always r4, for the 16-bit sweep (SE_GENERATED_PRINTEDC_SWEEP16=1: r4 = every value 0..65535, so a constant at the edge of the cut is met)"""
    class R4(random.Random):
        pass
    g = Gen(r); f = g.f
    nt = r.choice([1, 1, 2])
    for i in range(nt):
        last = cutchain(r, f, 1, 4)
        form = r.randrange(0, 8)
        c = consts(r, last)
        if form < 3: f.lit(3, c); f.add(r.choice(cmpkinds())(3, 1))
        elif form < 6: f.lit(3, c); f.add(r.choice(cmpkinds())(1, 3))
        elif form == 6: f.add(mov_(0, 1), 0x8800 | r.choice([0, 1, 0x7F, 0x80, 0xFF, 0xFE]))
        else: f.add(r.choice([cmppz, cmppl])(1))
        if i + 1 < nt:
            f.add(movt(2) if i == 0 else movt(3))
            if i == 0: f.add(mov_(8, 2))
            else: f.add(add__(8, 3), mov_(8, 3))
    usecmp(r, g, f)
    if nt == 2: f.add(add__(8, 0))
    f.rts_slot = NOP
    return f


FAMS17[109] = fam_ncmp4


def fam_memloopmix(r):
    """loops whose body changes width on the same bytes: four bytes assembled into a word, a word taken apart into bytes, half-words summed into a word, a byte fill read back
    as words (the code of a game's memcpy, a checksum and a record unpack), over r4 and r5 (the harness's pointers: equal, a neighbour, another area), r6 and r7 data"""
    m = MixMem(r); f = m.f
    n = r.randrange(2, 5)
    f.add(movi(9, n), movi(10, 0), mov_(8, 4), mov_(11, 5))
    top = m.g.lab('T'); f.label(top)
    form = r.randrange(0, 6)
    if form == 0:       # r10 = r10 << 8 | *r8++ (four times), the word stored at r11
        for _ in range(4): f.add(0x4018 | 10 << 8, LDI[1] | 2 << 8 | 8 << 4, extub(2, 2), or__(2, 10))
        f.add(ST[4] | 11 << 8 | 10 << 4, 0x7B04)
    elif form == 1:     # a word taken apart into four bytes at r11
        f.add(LDI[4] | 2 << 8 | 8 << 4)
        for k in range(4):
            f.add(ST[1] | 11 << 8 | 2 << 4, 0x7B01, 0x4019 | 2 << 8)
        f.add(add__(2, 10))
    elif form == 2:     # half-words summed (sign-extending loads) into r10, the sum stored as a word
        f.add(LDI[2] | 2 << 8 | 8 << 4, add__(2, 10), LDI[2] | 2 << 8 | 8 << 4, add__(2, 10), ST[4] | 11 << 8 | 10 << 4, 0x7B04)
    elif form == 3:     # bytes copied to r11, then the word at the start of the copy read and added
        for _ in range(4): f.add(LDI[1] | 2 << 8 | 8 << 4, ST[1] | 11 << 8 | 2 << 4, 0x7B01)
        f.add(0x7BFC, LD[4] | 3 << 8 | 11 << 4, add__(3, 10), 0x7B04)
    elif form == 4:     # r6 stored as bytes, half-words and a word into r11, then read back as a word and a half-word
        f.add(ST[1] | 11 << 8 | 6 << 4, 0x7B01, ST[1] | 11 << 8 | 7 << 4, 0x7B01, ST[2] | 11 << 8 | 6 << 4, 0x7B02, 0x7BFC, LD[4] | 2 << 8 | 11 << 4, add__(2, 10), 0x7B04)
    else:               # a word read, its two halves stored swapped as half-words, then read as bytes
        f.add(LDI[4] | 2 << 8 | 8 << 4, 0x6200 | 2 << 4 | 9 | 0, ST[2] | 11 << 8 | 2 << 4, 0x7B02, 0x4029 | 2 << 8, ST[2] | 11 << 8 | 2 << 4, 0x7B02, 0x7BFC, LD[1] | 3 << 8 | 11 << 4, add__(3, 10), 0x7B04)
    f.add(dt(9)); f.br('bf', top)
    f.add(mov_(0, 10))
    f.rts_slot = NOP
    return f


FAMS17[110] = fam_memloopmix


def fam_memcmp(r):
    """memory mode, one width per function (DAGEN16_W 1 or 2, else 2): a byte or half-word loaded through r4 or r5 (sign-extended by the load, or cut by extu), compared with a constant of
    either sign on either side, stored back or counted; what `if (*p == 0xff)` and `if (*(short *)p < -1)` are"""
    m = MixMem(r); f = m.f
    w = W16 if W16 in (1, 2) else 2
    for _ in range(r.randrange(1, 4)):
        base = r.choice([4, 4, 5]); off = r.randrange(0, 32 // w) * w
        m.load(base, off, w, 1)
        k = r.randrange(0, 4)
        if k == 0: f.add((extub if w == 1 else extuw)(1, 1))
        elif k == 1: f.add((extsb if w == 1 else extsw)(1, 1))
        c = r.choice(C8 if w == 1 else C16)
        f.lit(3, c)
        kind = r.choice(cmpkinds())
        f.add(kind(3, 1) if r.random() < 0.5 else kind(1, 3))
        f.add(movt(2), add__(2, 6) if False else add__(2, 7))
        if r.random() < 0.4:
            m.store(base, off, w, 1 if w == 1 else 0) if False else None
    f.add(mov_(0, 7))
    f.rts_slot = NOP
    return f


FAMS17[111] = fam_memcmp


def build17(seed, fam, want_fn=False):
    r = random.Random(seed * 104729 + 17)
    f = FAMS17[fam](r)
    f.add(0x000B, getattr(f, 'rts_slot', NOP))
    if want_fn: return f
    return finish(f)


def build(seed, fam=None, want_fn=False):
    if fam is not None and fam in FAMS17: return build17(seed, fam, want_fn)
    return d16.build(seed, fam, want_fn)


if __name__ == "__main__":
    if sys.argv[1] == 'show':
        f = build(int(sys.argv[2]), int(sys.argv[3]), want_fn=True)
        for c in f.w: print(c if isinstance(c, tuple) else "%04X" % c)
    else:
        first, count = int(sys.argv[1]), int(sys.argv[2])
        fam = int(sys.argv[3]) if len(sys.argv) > 3 else None
        out = [g for g in (build(s, fam) for s in range(first, first + count)) if g]
        print(";".join(out))
