#!/usr/bin/env python3
"""bigframe.py N > big.src : a function of N copies of the frame address (`mov r15,r1 ; add #16,r1 ; mov.l r6,@r1`), then the frame address swapped twice and walked
down over the sentinel at r15 + 0. Patch 0061's walk gives up past 8,192 members (everything escapes); N = 6,500 (39 KB, 19,500 instructions) decompiles right
(`return xStack_20`), N = 9,000 exceeds the flow's instruction limit ('Flow exceeded maximum allowable instructions') before the bound is reached.
The code sits at 0x06020000 (the driver's vectors occupy 0x06009800)."""
import sys
n = int(sys.argv[1])
L = ['add #-32,r15', 'mov #5,r0', 'mov.l r0,@r15', 'mov #7,r6']
for _ in range(n):
    L += ['mov r15,r1', 'add #16,r1', 'mov.l r6,@r1']
L += ['mov r15,r2', 'add #16,r2', 'swap.w r2,r2', 'swap.w r2,r2', 'mov #4,r1', 'L:', 'mov.l r1,@-r2', 'dt r1', 'bf L', 'mov.l @r15,r0', 'add #32,r15', 'rts', 'nop']
print('\n'.join(['name bigframe %d' % n, 'asm 06020000'] + L + ['endasm', 'entry 06020000', 'args 0 0 0 0', 'end']))
