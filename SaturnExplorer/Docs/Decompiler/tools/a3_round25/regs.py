#!/usr/bin/env python3
"""Area 3 round 25: the register sweep of A3-192's mark, printed as fixtures.src text (appended there once; kept to say how they were made).
For R in r0-r3, r8, r10, r12-r14: a local of 5, its address in R (saved by the caller when R is r8-r14), `bsr F1`, then 7 stored through R
(vA to vC) or `bsr F2` that stores 7 through R (vD, vE); every run with r4 != 0 returns 7.
  vA  lent F1 sets R on one return          vB  lent F1 never touches R          vC  F1 not lent (reads rX on entry), sets R on one return
  vD  vA's F1, then F2 (not lent: reads R) stores 7 through R                 vE  vC's F1, then the same F2
usage: regs.py > more.src"""
REGS = ['r0', 'r1', 'r2', 'r3', 'r8', 'r10', 'r12', 'r13', 'r14']
def fixture(R, v):
    T = 'r3' if R == 'r2' else 'r2'
    saved = R in ('r8', 'r10', 'r12', 'r13', 'r14')
    unlentReg = 'r1' if R == 'r3' else 'r3'
    L = ['name %s%s' % (v, R), '# A3-192 register sweep: %s, %s' % (R, v), 'ret 1,0,0' if v in ('vD', 'vE') else 'ret 1,0', '.word 0x4F22']
    if saved: L.append('mov.l %s,@-r15' % R)
    L += ['add #-8,r15', 'mov #5,%s' % T, 'mov.l %s,@r15' % T, 'mov r15,%s' % R, 'bsr F1', 'nop']
    if v in ('vD', 'vE'): L += ['bsr F2', 'nop']
    else: L += ['mov #7,%s' % T, 'mov.l %s,@%s' % (T, R)]
    L += ['mov.l @r15,r0', 'add #8,r15']
    if saved: L.append('mov.l @r15+,%s' % R)
    L += ['.word 0x4F26', 'rts', 'nop', 'fn', 'F1:']
    if v == 'vB': L += ['tst r4,r4', 'bf K1', 'mov #0,%s' % ('r3' if R != 'r3' else 'r2'), 'rts', 'nop', 'K1:', 'rts', 'nop']
    else:
        L += ['tst %s,%s' % ((unlentReg, unlentReg) if v in ('vC', 'vE') else ('r4', 'r4'))]
        if v in ('vC', 'vE'): L += ['bt K1', 'tst r4,r4']
        L += ['bf K1', 'mov #0,%s' % R, 'rts', 'nop', 'K1:', 'rts', 'nop']
    if v in ('vD', 'vE'):
        T2 = 'r3' if R == 'r2' else 'r2'
        L += ['fn', 'F2:', 'mov #7,%s' % T2, 'rts', 'mov.l %s,@%s' % (T2, R)]
    return L
for R in REGS:
    for v in ('vA', 'vB', 'vC', 'vD', 'vE'):
        print('\n'.join(fixture(R, v)))
