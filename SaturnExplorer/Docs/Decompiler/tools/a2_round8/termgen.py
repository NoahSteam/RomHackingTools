#!/usr/bin/env python3
# Area 2 round 8 instrument: sums of scaled terms over shared variables (RuleCollectTerms' input) read through a mask: the A2-47 / A2-52 family.
# usage: termgen.py <first seed> <count>; same use as maskgen.py. 21 of 6,000 seeds print a wrong value on the tree of round 8 (A2-52).
"""Sums of scaled terms over shared variables (RuleCollectTerms' input) with a mask or an extension after: x*c1 + y*c2 + x*c3 + k, (T*A + (1-T)*B)
variants with two booleans, then a mask that the low bits of the sum decide. One function per seed; ';' joined."""
import random, sys
def build(seed):
    r = random.Random(seed * 31 + 7)
    w = []
    def lit(reg, v): w.append(('lit', reg, v))
    nterm = r.randrange(2, 5)
    # variables: r4, r5, r6 (args) and booleans movt'ed into r1/r2 (0/1)
    vars_ = []
    for k in range(r.randrange(0, 3)):
        n, m = r.choice([(4, 5), (4, 6), (5, 6), (6, 7)])
        w.append(r.choice([0x3000, 0x3007, 0x3006, 0x3002, 0x3003, 0x2008]) | n << 8 | m << 4)
        reg = 1 + k
        w.append(0x0029 | reg << 8)
        vars_.append(reg)
    vars_ += [4, 5, 6]
    # r0 accumulates; r3 is the scratch
    lit(0, r.choice([0, 0, 0x06009008, 0x06009000, 4, 8, 0x10, 0x100, 0xFFFFFFFC]))
    for t in range(nterm):
        v = r.choice(vars_)
        c = r.choice([1, 2, 3, 4, 5, 6, 8, 12, 0x10, 0x12, 0x14, 0x1C, 0x24, 0x48, 0xB8, 0xFFFFFFFF, 0xFFFFFFFC, 0xFFFFFFFE, 0xFFFFFFFA, 0x6009008, 0x600900E, 0x600900A])
        how = r.randrange(0, 3)
        if how == 0 and c < 0x100 and c in (2, 4, 8, 0x10):   # shift form
            w.append(0x6003 | 3 << 8 | v << 4)
            sh = {2: [0x4000], 4: [0x4008], 8: [0x4008, 0x4000], 0x10: [0x4008, 0x4008]}[c]
            for s in sh: w.append(s | 3 << 8)
        else:
            lit(3, c)
            w.append(0x0007 | 3 << 8 | v << 4)      # mul.l rv,r3
            w.append(0x001A | 3 << 8)               # sts macl,r3
        w.append((0x300C if r.random() < 0.7 else 0x3008) | 0 << 8 | 3 << 4)   # add/sub r3,r0
    tail = r.randrange(0, 8)
    if tail == 0: lit(3, r.choice([0xFFFFFFFC, 0xFFFFFFF8, 0xFFFFFFF0, 0xFFFFFFFE, 0xFFFFFF00, 0xFFFFFFC0])); w.append(0x2039)
    elif tail == 1: w.append(0xC9FC)
    elif tail == 2: w += [0x4009, 0x4008]
    elif tail == 3: w.append(0x600D)
    elif tail == 4: w.append(0x600C)
    elif tail == 5: w += [0x4009, 0x4009, 0x4008, 0x4008]
    elif tail == 6: w += [0xC903, 0x4000]
    else: w += [0x4019, 0x4018]
    w += [0x000B, 0x0009]
    n = len(w); pool_at = (n * 2 + 3) & ~3
    words = []; pool = []
    for i, c in enumerate(w):
        if isinstance(c, tuple):
            _, reg, val = c
            if val not in pool: pool.append(val)
            d = (pool_at + 4 * pool.index(val) - (((i * 2) + 4) & ~3)) // 4
            if d < 0 or d > 255: return None
            words.append(0xD000 | reg << 8 | d)
        else: words.append(c)
    while len(words) * 2 < pool_at: words.append(0x0009)
    for v in pool: words += [v >> 16, v & 0xFFFF]
    return " ".join("%04X" % x for x in words)
if __name__ == "__main__":
    first, count = int(sys.argv[1]), int(sys.argv[2])
    print(";".join(g for g in (build(s) for s in range(first, first + count)) if g))
