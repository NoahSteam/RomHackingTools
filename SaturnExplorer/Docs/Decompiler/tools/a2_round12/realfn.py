#!/usr/bin/env python3
# Area 2 round 12 instrument: the register-only functions of a game image, as words for the printed-C differential
# (SE_GENERATED_PRINTEDC_WORDS): compiler-written arithmetic, branches and loops, with no load, store, call or control-register access.
# usage: realfn.py <hwram image> <entries file (one hex address per line, or file names of SE_GAME_OUT)> [max length in words]  -> the words, ';' joined
import sys, os
img = open(sys.argv[1], 'rb').read()
BASE = 0x06000000
entries = []
for line in open(sys.argv[2]):
    t = line.strip().split('.')[0]
    if t: entries.append(int(t, 16))
maxw = int(sys.argv[3]) if len(sys.argv) > 3 else 120
def w(a): o = a - BASE; return (img[o] << 8) | img[o + 1] if 0 <= o < len(img) - 1 else None
def sx8(v): return v - 256 if v & 0x80 else v
def sx12(v): return v - 4096 if v & 0x800 else v
def classify(op):
    """None if the instruction is not register-only; else (kind, data)"""
    n, m, d = (op >> 8) & 15, (op >> 4) & 15, op & 15
    top = op >> 12
    if op == 0x0009: return ('ok',)
    if op == 0x000B: return ('rts',)
    if op in (0x0008, 0x0018, 0x0028, 0x0029 | 0, ): return ('ok',)
    if top == 0 and d == 9 and m == 2: return ('ok',)                      # movt
    if top == 0 and (op & 0xF0FF) in (0x000A, 0x001A, 0x0029): return ('ok',)   # sts mach/macl, movt
    if top == 0 and d == 7: return ('ok',)                                  # mul.l
    if top == 2 and d in (8, 9, 0xA, 0xB, 0xC, 0xD, 0xE, 0xF): return ('ok',)
    if top == 3 and d in (0, 2, 3, 4, 5, 6, 7, 8, 0xA, 0xB, 0xC, 0xD, 0xE, 0xF): return ('ok',)
    if top == 4:
        low = op & 0xFF
        if low in (0x00, 0x01, 0x04, 0x05, 0x08, 0x09, 0x0A + 0x0, 0x10, 0x11, 0x15, 0x18, 0x19, 0x20, 0x21, 0x24, 0x25, 0x28, 0x29): return ('ok',)
        return None
    if top == 6 and d in (3, 7, 8, 9, 0xA, 0xB, 0xC, 0xD, 0xE, 0xF): return ('ok',)
    if top == 7 or top == 0xE: return ('ok',)
    if top == 8:
        sub = n
        if sub in (8,): return ('ok',)                                      # cmp/eq #imm,r0
        if sub in (9, 0xB, 0xD, 0xF): return ('br', sx8(op & 0xFF), sub in (0xD, 0xF))
        return None
    if top == 9 or top == 0xD: return ('pc', top)
    if top == 0xA: return ('bra', sx12(op & 0xFFF))
    if top == 0xC:
        if n in (8, 9, 0xA, 0xB): return ('ok',)                            # tst/and/xor/or #imm,r0
        if n == 7: return ('mova', op & 0xFF)
        return None
    return None
out = []
for e in entries:
    if e & 1: continue
    pos = e; insts = []; ok = True; end = None; targets = []; pcrefs = []
    limit = e + 2 * maxw
    # walk linearly; a delayed branch has a slot that must be 'ok'
    while pos < limit:
        op = w(pos)
        if op is None: ok = False; break
        c = classify(op)
        if c is None: ok = False; break
        insts.append((pos, op, c))
        if c[0] == 'rts':
            sl = w(pos + 2); cs = classify(sl) if sl is not None else None
            if cs is None or cs[0] not in ('ok',): ok = False; break
            insts.append((pos + 2, sl, cs)); pos += 4
            # more code only if a branch target lies beyond
            far = [t for t in targets if t >= pos]
            if not far: end = pos; break
            continue
        if c[0] == 'br': targets.append(pos + 4 + 2 * c[1]);
        if c[0] == 'bra': targets.append(pos + 4 + 2 * c[1])
        if c[0] == 'pc': pcrefs.append(pos)
        pos += 2
    if not ok or end is None: continue
    # delay slots of s-branches/bra must be ok instructions (the walk already classified them)
    if any(t < e or t > end for t in targets): continue
    # pool: pc-relative loads
    pool_end = end
    for p in pcrefs:
        op = w(p); d = op & 0xFF
        a = (p + 4 + 2 * d) if (op >> 12) == 9 else (((p + 4) & ~3) + 4 * d)
        pool_end = max(pool_end, a + (2 if (op >> 12) == 9 else 4))
    if pool_end - e > 2 * (maxw + 40): continue
    if any(classify(op)[0] == 'mova' for _, op, _ in insts if classify(op)): continue
    words = [w(a) for a in range(e, pool_end, 2)]
    if e & 2: words = [0x0009] + words
    out.append("%04X" % 0 if False else " ".join("%04X" % x for x in words))
print(";".join(out))
sys.stderr.write("%d of %d entries are register-only functions\n" % (len(out), len(entries)))
