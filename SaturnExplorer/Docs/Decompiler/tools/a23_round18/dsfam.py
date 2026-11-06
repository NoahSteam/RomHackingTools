#!/usr/bin/env python3
"""dsfam.py START COUNT -> ';'-joined register-only functions with a PC-relative load or mova in the delay slot of bt/s, bf/s, bra or braf
(patch 0050's shapes, which the register-only grammar does not draw). A pool of distinct longs follows the code, so every literal the slot can
read on either path is a distinct known value. Deterministic per index."""
import sys, random
BASE = 0x06009000
CMPS = [0x3457, 0x3453, 0x3456, 0x3452, 0x3450, 0x2458, 0x4415, 0x4411]   # cmp/gt r5,r4; cmp/ge; cmp/hi; cmp/hs; cmp/eq; tst r5,r4; cmp/pl r4; cmp/pz r4
OPS = [0x316C, 0x317C, 0x217A, 0x216A, 0x4100, 0x4101, 0x6117, 0x611B, 0x216B, 0x2169, 0x7105, 0x71F0, 0x4108, 0x6119, 0x3168]
#      add r6,r1; add r7,r1; xor r7,r1; xor r6,r1; shll; shlr; not; neg; or r6,r1; and r6,r1; add #5; add #-16; shll2; swap.w; sub r6,r1
def build(i):
    R = random.Random(i * 7919 + 13)
    kind = R.choice(['bt/s', 'bf/s', 'bra', 'braf'])
    slot = R.choice(['movl', 'movw', 'mova'])
    code = []   # list of words or ('lbl', name) placeholders resolved later
    pre = [R.choice(CMPS)] if kind in ('bt/s', 'bf/s') else []
    if R.random() < 0.5: pre = [0xE1A5] + pre   # mov #-91,r1 (r1 defined on the not-taken path even if the slot is mova)
    nt = [R.choice(OPS) for _ in range(R.randrange(0, 3))]
    tk = [R.choice(OPS) for _ in range(R.randrange(0, 3))]
    gap = [R.choice(OPS) for _ in range(R.randrange(0, 3))] if kind in ('bra', 'braf') else []
    mv = [0x6103] if slot == 'mova' else []   # mov r0,r1
    # layout: pre ; [mov #x,r2 for braf] ; branch ; slot ; (cond: nt ; bra E ; nop) or (uncond: gap words skipped) ; L: mv tk ; E: mov r1,r0 ; rts ; nop ; pool
    w = list(pre)
    if kind == 'braf': w.append(None)     # mov #disp,r2 filled below
    br = len(w); w.append(None); w.append(None)   # branch, slot
    if kind in ('bt/s', 'bf/s'):
        w += mv + nt; braE = len(w); w += [None, 0x0009]
    else:
        w += gap
    L = len(w); w += mv + tk
    E = len(w); w += [0x6013, 0x000B, 0x0009]
    if len(w) & 1: w.append(0x0009)
    pool = len(w); n = 96
    for k in range(n): w += [0x1000 + k * 0x13 + (i % 7), 0x2000 + k * 0x29]
    addr = lambda idx: BASE + 2 * idx
    tgt = addr(L)
    if kind == 'bt/s': w[br] = 0x8D00 | ((L - br - 2) & 0xFF)
    elif kind == 'bf/s': w[br] = 0x8F00 | ((L - br - 2) & 0xFF)
    elif kind == 'bra': w[br] = 0xA000 | ((L - br - 2) & 0xFFF)
    else:
        w[br] = 0x0223; w[br - 1] = 0xE200 | ((2 * (L - br - 2)) & 0xFF)   # braf r2, r2 = L - (braf + 4)
    if kind in ('bt/s', 'bf/s'): w[braE] = 0xA000 | ((E - braE - 2) & 0xFFF)
    slotaddr = addr(br + 1)
    bases = [(tgt + 2)]
    if kind in ('bt/s', 'bf/s'): bases.append(slotaddr + 4)
    lo, hi = addr(pool), addr(pool) + 4 * n
    if slot == 'movw':
        ds = [d for d in range(256) if all(lo <= b + 2 * d < hi - 2 for b in bases)]
        d = R.choice(ds); w[br + 1] = 0x9100 | d
    else:
        ds = [d for d in range(256) if all(lo <= (b & ~3) + 4 * d < hi - 4 for b in bases)]
        d = R.choice(ds); w[br + 1] = (0xD100 if slot == 'movl' else 0xC700) | d
    return ' '.join('%04X' % x for x in w)
s, n = int(sys.argv[1]), int(sys.argv[2])
print(';'.join(build(i) for i in range(s, s + n)))
