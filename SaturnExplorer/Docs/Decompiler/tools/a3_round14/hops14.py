#!/usr/bin/env python3
"""hops14.py N... > hops.src : R14-3, a walk down whose store pointer is N constant-add hops (add #-1,r2) from the loop's join, two iterations, a byte store;
the frame is 2N+8 bytes with the sentinel word at sp+0, p0 = sp + 2N + r4 (unbounded). LoadGuard::pointerSteppedDown follows 64 hops."""
import sys
def adds(r, n, k):
    out = []
    while n > 0:
        d = min(n, 127); out.append('add #%d,%s' % (k * d, r)); n -= d
    return out
def fx(n, unb=True):
    F = 2 * n + 8
    L = adds('r15', F, -1) + ['mov r15,r2'] + adds('r2', 2 * n, 1) + ['mov #5,r0', 'mov.l r0,@r15']
    if unb: L += ['add r4,r2']
    L += ['mov #2,r1', 'L:'] + ['add #-1,r2'] * n + ['mov.b r1,@r2', 'dt r1', 'bf L', 'mov.l @r15,r0'] + adds('r15', F, 1) + ['rts', 'nop']
    return ['name R14-3 hops=%d unbounded=%d' % (n, unb), 'asm 06009000'] + L + ['endasm', 'args 0 0 0 0', 'end', '']
o = []
for a in sys.argv[1:]: o += fx(int(a))
print('\n'.join(o))
