#!/usr/bin/env python3
"""cap14.py N... > cap.src : A3-122 candidates. S18 (a frame address plus an unbounded index, walked down by `mov.l r1,@-r2` in a counted loop) with N extra
pointer-derived stores (`mov.l r1,@(4,r2)`) in the loop body before the walking store, so the loop's join has more than 4,096 (varnode, offset) descendants:
AliasChecker::stepsDown stops following at 4,096 and answers false. The body is split in segments of 1,000 stores with a trampoline between them (bf/bra reach 256/4,094 bytes); the code sits at 0x06020000."""
import sys
def fx(n, unb=True):
    seg = 1000
    segs = []
    left = n
    while left > 0:
        segs.append(min(seg, left)); left -= segs[-1]
    L = ['add #-24,r15', 'mov r15,r2', 'add #16,r2', 'mov #5,r0', 'mov.l r0,@r15']
    if unb: L += ['add r4,r2']
    L += ['mov #4,r1', 'L:']
    prev = 'L'
    for k, c in enumerate(segs):
        L += ['mov.l r1,@(4,r2)'] * c
        if k < len(segs) - 1:
            L += ['bra OVER%d' % k, 'nop', 'M%d:' % k, 'bra %s' % prev, 'nop', 'OVER%d:' % k]
            prev = 'M%d' % k
    L += ['mov.l r1,@-r2', 'dt r1', 'bt EXIT', 'nop', 'bra %s' % prev, 'nop', 'EXIT:', 'mov.l @r15,r0', 'add #24,r15', 'rts', 'nop']
    return ['name R14-18 N=%d extra stores in the loop body, unbounded=%d' % (n, unb), 'asm 06020000'] + L + ['endasm', 'entry 06020000', 'args 0 0 0 0', 'end', '']
o = []
for a in sys.argv[1:]:
    o += fx(int(a))
    if a.startswith('4') and int(a) > 4095: o += fx(int(a), False)
print('\n'.join(o))
