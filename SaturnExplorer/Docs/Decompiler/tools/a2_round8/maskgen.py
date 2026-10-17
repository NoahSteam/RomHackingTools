#!/usr/bin/env python3
# Area 2 round 8 instrument: random register-only functions aimed at the mask-reading rules (booleans from movt/neg, constants, extends, shifts).
# usage: maskgen.py <first seed> <count> | SE_GENERATED_PRINTEDC=only SE_GENERATED_PRINTEDC_WORDS="$(...)" SaturnExplorerSh2GeneratedTests <scratch>
"""Random register-only SH-2 functions aimed at the mask-reading rules: boolean (T) selects scaled by constants, masks and
shifts of sums, extends, swaps. Output: one function per line as hex words, ';'-joined (SE_GENERATED_PRINTEDC_WORDS)."""
import random, sys

CONSTS = [0, 1, 2, 3, 4, 6, 8, 12, 0xF, 0x10, 0x1C, 0x20, 0x7F, 0x80, 0xFF, 0x100, 0xFFF, 0x1000, 0xFFFC, 0xFFFF, 0x10000, 0xFFFFFFFC, 0xFFFFFFF8,
          0xFFFFFF00, 0xFFFFFFF0, 0xFFFF0000, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF, 0x06009008, 0x0600900E, 0x06009018, 0x0600901C, 0x00FF00FF,
          0x03030303, 0x12345678, 0x00000006, 0x0000000E, 0xFFFFFFFE, 0x55555555]

class Fn:
    def __init__(self, rnd):
        self.r = rnd
        self.w = []         # words; ('lit', reg, value) placeholders for pool loads
        self.lits = []      # values
    def reg(self, hi=7):
        return self.r.randrange(0, hi + 1)
    def tmp(self):
        return self.r.randrange(0, 4)
    def add(self, x):
        self.w.append(x)
    def lit(self, reg, value=None):
        if value is None:
            value = self.r.choice(CONSTS)
        self.w.append(('lit', reg, value))

def gen(seed):
    rnd = random.Random(seed)
    f = Fn(rnd)
    r = rnd
    if r.random() < 0.8:
        f.w += [0x6043, 0x6153, 0x6263, 0x6373]
    n = r.randrange(5, 17)
    def T():
        k = r.randrange(0, 9)
        a, b = r.randrange(0, 8), r.randrange(0, 8)
        if k == 0: f.add(0x3000 | a << 8 | b << 4)              # cmp/eq
        elif k == 1: f.add(0x3003 | a << 8 | b << 4)            # cmp/ge
        elif k == 2: f.add(0x3007 | a << 8 | b << 4)            # cmp/gt
        elif k == 3: f.add(0x3006 | a << 8 | b << 4)            # cmp/hi
        elif k == 4: f.add(0x3002 | a << 8 | b << 4)            # cmp/hs
        elif k == 5: f.add(0x4015 | a << 8)                     # cmp/pl
        elif k == 6: f.add(0x4011 | a << 8)                     # cmp/pz
        elif k == 7: f.add(0x2008 | a << 8 | b << 4)            # tst
        else: f.add(0x200C | a << 8 | b << 4)                   # cmp/str
    def shifts(rn):
        k = r.choice([0x4000, 0x4001, 0x4008, 0x4009, 0x4018, 0x4019, 0x4028, 0x4029, 0x4021, 0x4004, 0x4005, 0x4024, 0x4025])
        f.add(k | rn << 8)
    for _ in range(n):
        k = r.randrange(0, 34)
        d = f.tmp(); s = r.randrange(0, 8)
        if k < 4:      # a T select: movt d ; (neg d) ; (and/mul with constants)
            T()
            f.add(0x0029 | d << 8)                              # movt d
            if r.random() < 0.6: f.add(0x600B | d << 8 | d << 4)  # neg d,d
        elif k < 8:    # scale a 0/1 or 0/-1 by a constant: mov.l const,e ; mulu/and/shll
            if r.random() < 0.5:
                e = (d + 1) % 4
                f.lit(e)
                f.add(0x2009 | d << 8 | e << 4)                 # and e,d
            else:
                for _ in range(r.randrange(1, 4)): shifts(d)
        elif k < 12:   # add a constant or a register
            if r.random() < 0.5:
                e = (d + r.randrange(1, 4)) % 4
                f.lit(e); f.add(0x300C | d << 8 | e << 4)       # add e,d
            else:
                f.add(0x300C | d << 8 | s << 4)                 # add s,d
        elif k < 14:   # sub / neg / not
            f.add(r.choice([0x3008, 0x600B, 0x6007]) | d << 8 | s << 4)
        elif k < 18:   # and with a constant (the redundancy rules): via a literal register, or and #imm,r0
            if r.random() < 0.5:
                e = (d + r.randrange(1, 4)) % 4
                f.lit(e, r.choice([0xFFFFFFFC, 0xFFFFFFF8, 0xFFFFFF00, 0xFFFFFFF0, 0xFFFF, 0xFF, 0xFFF0, 0xFFFFFFFE, 0x1C, 0x7F, 0x3, 0xFC]))
                f.add(0x2009 | d << 8 | e << 4)
            else:
                f.add(0xC900 | r.choice([0xFC, 0xF8, 0xF0, 0xFF, 0x0F, 0x03, 0x7F, 0x80, 0x1F, 0xFE]))   # and #imm,r0
        elif k < 20:   # or / xor with something
            f.add(r.choice([0x200B, 0x200A]) | d << 8 | s << 4)
        elif k < 23:   # extends / swaps
            f.add(r.choice([0x600C, 0x600D, 0x600E, 0x600F, 0x6008, 0x6009, 0x200D]) | d << 8 | s << 4)
        elif k < 26:   # shifts
            shifts(d)
        elif k < 28:   # mul.l / mulu.w / muls.w + sts macl
            f.add(r.choice([0x200E, 0x200F, 0x0007]) | d << 8 | s << 4)
            f.add(0x001A | d << 8)                               # sts macl,d
        elif k < 30:   # a constant on its own
            f.lit(d)
        elif k < 32:   # mov
            f.add(0x6003 | d << 8 | s << 4)
        else:          # dt / addc style
            f.add(0x4010 | d << 8)
    # return: rts ; mov rX,r0
    x = r.randrange(0, 8)
    f.add(0x000B); f.add(0x6003 | x << 4)
    # lay out: pool after the code, 4-aligned (function base is 4-aligned)
    code = f.w
    n = len(code)
    pool_at = (n * 2 + 3) & ~3
    words = []
    pool = []
    for i, c in enumerate(code):
        if isinstance(c, tuple):
            _, reg, val = c
            if val not in pool: pool.append(val)
            idx = pool.index(val)
            a = i * 2
            p = pool_at + 4 * idx
            d = (p - ((a + 4) & ~3)) // 4
            if d < 0 or d > 255:
                return None
            words.append(0xD000 | reg << 8 | d)
        else:
            words.append(c)
    while len(words) * 2 < pool_at: words.append(0x0009)
    for v in pool: words += [v >> 16, v & 0xFFFF]
    return " ".join("%04X" % x for x in words)

if __name__ == "__main__":
    first, count = int(sys.argv[1]), int(sys.argv[2])
    out = []
    for s in range(first, first + count):
        g = gen(s)
        if g: out.append(g)
    print(";".join(out))
