#!/usr/bin/env python3
# Area 2 round 8 instrument: templates T * A + (1 - T) * B (A2-47): 18 of 400 print a wrong value with patch 0046 reverted, none with it.
# usage: tmplgen.py <first seed> <count>; same use as maskgen.py.
"""Templates of the A2-47 family: a value built from a boolean T as (T * A + (1 - T) * B) or a select, whose low bits are known to be
clear in one shape and not in the rewritten one, then a mask/shift/extend that reads them. One function per line; ';' joined."""
import random, sys

def build(seed):
    r = random.Random(seed)
    w = []; lits = []
    def lit(reg, v):
        w.append(('lit', reg, v))
    # 1. T
    a, b = r.choice([(4, 5), (4, 6), (5, 6), (4, 7)]), None
    n, m = a
    k = r.randrange(0, 6)
    w.append([0x4015 | n << 8, 0x3007 | n << 8 | m << 4, 0x3006 | n << 8 | m << 4, 0x3000 | n << 8 | m << 4, 0x2008 | n << 8 | m << 4, 0x4011 | n << 8][k])
    w.append(0x0029 | 1 << 8)   # movt r1
    # 2. constants
    Bc = r.choice([0x06009008, 0x06009000, 0x0600900C, 0x06009010, 0x08, 0x20, 0x100, 0x0, 0x4, 0x1000, 0x06009018, 0x06009020, 0x06008FFC])
    delta = r.choice([2, 6, 1, 3, 4, 8, 10, 12, 14, 0x12, 0x16, 0x1A, 0x1E, 0x32, 0x102, 0x7FFE, 0x10002, 0xFFFFFFFE, 0xFFFFFFFA, 0xFFFFFFF2])
    Ac = (Bc + delta) & 0xFFFFFFFF
    form = r.randrange(0, 5)
    if form == 0:      # T*A + (1-T)*B
        lit(2, Ac); lit(3, Bc)
        w.append(0x0217)            # mul.l r1,r2
        w.append(0x061A)            # sts macl,r6
        w.append(0xE401)            # mov #1,r4
        w.append(0x3418)            # sub r1,r4
        w.append(0x0347)            # mul.l r4,r3
        w.append(0x051A)            # sts macl,r5
        w.append(0x365C)            # add r5,r6
    elif form == 1:    # B + T * delta
        lit(2, delta); lit(6, Bc)
        w.append(0x0217); w.append(0x051A)   # mul.l r1,r2 ; sts macl,r5
        w.append(0x365C)
    elif form == 2:    # select via neg/and: (-T & delta) + B
        lit(2, delta); lit(6, Bc)
        w.append(0x611B)            # neg r1,r1
        w.append(0x2129)            # and r2,r1
        w.append(0x3C1C if False else 0x361C)   # add r1,r6
    elif form == 3:    # T*A computed by shift/add: ((T << k) + B) with k
        kk = r.choice([1, 2, 3])
        w.append(0x4000 | 1 << 8) if kk >= 1 else None
        for _ in range(kk - 1): w.append(0x4000 | 1 << 8)
        lit(6, Bc)
        w.append(0x361C)
    else:              # (A - B) * T - the other order: B - T*delta
        lit(2, delta); lit(6, Bc)
        w.append(0x0217); w.append(0x051A)
        w.append(0x3658)            # sub r5,r6
    # 3. mask or shift reading the low bits of r6
    tail = r.randrange(0, 9)
    w.append(0x6063)                # mov r6,r0  (some shapes keep r6)
    if tail == 0:
        lit(7, r.choice([0xFFFFFFFC, 0xFFFFFFF8, 0xFFFFFFF0, 0xFFFFFFFE, 0xFFFFFF00])); w.append(0x2079 if False else 0x2679)   # and r7,r6
        w.append(0x6063)
    elif tail == 1:
        w.append(0xC9FC); 
    elif tail == 2:
        w.append(0x4009); w.append(0x4008)   # shlr2 ; shll2
    elif tail == 3:
        w.append(0x600D)                     # extu.w r0,r0
    elif tail == 4:
        w.append(0x600C)                     # extu.b r0,r0
    elif tail == 5:
        w.append(0x4019); w.append(0x4018)   # shlr8 ; shll8
    elif tail == 6:
        w.append(0xC903)                     # and #3,r0
    elif tail == 7:
        w.append(0x4009); w.append(0x4009); w.append(0x4008); w.append(0x4008)
    else:
        lit(7, r.choice([0x3C, 0xFC, 0xFFC, 0xFFF0])); w.append(0x2679); w.append(0x6063)
    post = r.randrange(0, 4)
    if post == 1: w.append(0x7001)           # add #1,r0
    elif post == 2: w.append(0x4000)         # shll r0
    elif post == 3: w.append(0x4001)         # shlr r0
    w.append(0x000B); w.append(0x0009)
    n = len(w)
    pool_at = (n * 2 + 3) & ~3
    words = []; pool = []
    for i, c in enumerate(w):
        if isinstance(c, tuple):
            _, reg, val = c
            if val not in pool: pool.append(val)
            d = (pool_at + 4 * pool.index(val) - (((i * 2) + 4) & ~3)) // 4
            if d < 0 or d > 255: return None
            words.append(0xD000 | reg << 8 | d)
        else:
            words.append(c)
    while len(words) * 2 < pool_at: words.append(0x0009)
    for v in pool: words += [v >> 16, v & 0xFFFF]
    return " ".join("%04X" % x for x in words)
if __name__ == "__main__":
    first, count = int(sys.argv[1]), int(sys.argv[2])
    print(";".join(g for g in (build(s) for s in range(first, first + count)) if g))
