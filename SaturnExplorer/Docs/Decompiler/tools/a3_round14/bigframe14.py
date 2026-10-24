#!/usr/bin/env python3
"""bigframe14.py > big.src : R14-4, walks down from a frame address (plus an unbounded index) over a frame larger than the 0xffff the guards reach.
A fixture is (frame, base offset B, sentinel offset S, stride, unbounded): p = sp + B (+ r4); the loop steps p down by the stride (register, sub) three
times and stores 3, 2, 1; the sentinel word at sp+S holds 5 and is returned. B - 3*stride = S makes the third store land on it."""
import sys
def fx(name, frame, B, S, stride, unbounded, op='sub'):
    L = ['mov.l =%d,r1' % frame, 'sub r1,r15', 'mov.l =%d,r3' % B, 'mov r15,r2', 'add r3,r2', 'mov.l =%d,r3' % S, 'add r15,r3', 'mov #5,r0', 'mov.l r0,@r3']
    if unbounded: L += ['add r4,r2']
    L += ['mov.l =%d,r5' % (stride if op == 'sub' else (-stride) & 0xFFFFFFFF), 'mov #3,r1', 'L:', '%s r5,r2' % ('sub' if op == 'sub' else 'add'), 'mov.l r1,@r2', 'dt r1', 'bf L',
          'mov.l @r3,r0', 'mov.l =%d,r1' % frame, 'add r1,r15', 'rts', 'nop']
    return ['name ' + name, 'asm 06009000'] + L + ['endasm', 'args 0 0 0 0', 'end', '']
o = []
o += fx('R14-4a frame 0x1A000 B=0x19000 S=0x1000 stride 0x8000 unbounded sub', 0x1A000, 0x19000, 0x1000, 0x8000, True)
o += fx('R14-4b same, constant base', 0x1A000, 0x19000, 0x1000, 0x8000, False)
o += fx('R14-4c frame 0x30000 B=0x2C000 S=0x4000 stride 0x8000 unbounded (0x28000 below)', 0x30000, 0x1C000, 0x4000, 0x8000, True)
o += fx('R14-4d same with add of -stride', 0x30000, 0x1C000, 0x4000, 0x8000, True, 'add')
o += fx('R14-4e same constant base add', 0x30000, 0x1C000, 0x4000, 0x8000, False, 'add')
o += fx('R14-4f control: small frame 0x400 B=0x300 S=0x100 stride 0x80 unbounded', 0x400, 0x280, 0x100, 0x80, True)
print('\n'.join(o))
