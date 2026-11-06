#!/usr/bin/env python3
"""ordfam.py START COUNT -> ';'-joined functions: a cut value (one or two extensions of r4/r5) compared, ordered or equal, either way round,
with a 32-bit constant at an edge; result movt, or the A2-111 shape (minus a second unsigned compare). Deterministic by index."""
import sys, itertools, asm
SRC = ['r4', 'r5']
EXT = [['exts.b'], ['exts.b', 'exts.w'], ['exts.b', 'extu.w'], ['extu.b', 'exts.w'], ['exts.w'], ['extu.w'], ['extu.b'],
       ['exts.w', 'exts.b'], ['extu.w', 'exts.b']]
CON = [0xFFFFFFFF, 0xFFFF, 0xFF, 0x80, 0x7F, 0xFF80, 0xFFFFFF80, 0x8000, 0xFFFF8000, 0x7FFF, 0, 1, 0xFFFFFFFE, 0xFFFE, 0xFF7F, 0xFFFF7FFF,
       0x81, 0xFFFFFF7F]
OPS = ['cmp/eq', 'cmp/hs', 'cmp/hi', 'cmp/ge', 'cmp/gt']
TAIL = ['movt', 'sub80', 'sub7fff', 'bt']
def fn(src, ext, c, op, swap, tail):
    L = ['mov %s,r1' % src] + ['%s r1,r1' % e for e in ext] + ['mov.l =%d,r3' % c]
    L.append('%s %s' % (op, 'r1,r3' if swap else 'r3,r1'))
    if tail == 'movt':
        L += ['.word 0x0029', 'rts', 'nop']
    elif tail == 'bt':
        L += ['bt T', 'mov #5,r0', 'rts', 'nop', 'T:', 'rts', 'mov #9,r0']
    else:
        c2 = 0x80 if tail == 'sub80' else 0x7FFF
        L += ['.word 0x0229', 'mov.l =%d,r3' % c2, 'cmp/hi r1,r3', '.word 0x0029', 'sub r2,r0', 'rts', 'nop']
    return ' '.join('%04X' % w for w in asm.assemble(L))
allc = list(itertools.product(SRC, EXT, CON, OPS, [0, 1], TAIL))
s, n = int(sys.argv[1]), int(sys.argv[2])
out = []
for i in range(s, min(s + n, len(allc))):
    a = allc[i]
    if a[3] == 'cmp/eq' and a[4]: continue
    out.append(fn(*a))
print(';'.join(out))
if len(sys.argv) > 3: print(len(allc), file=sys.stderr)
