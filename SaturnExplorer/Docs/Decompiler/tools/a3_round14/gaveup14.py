#!/usr/bin/env python3
"""gaveup14.py N... > gaveup.src : the escape walk's bound (patch 0061, maxMembers = 8,192) with the escape made by an unlent callee. The caller makes N copies
of the frame address (`mov r15,r1 ; add #16,r1 ; mov.l r6,@r1`: two members each), then hands `r15+16` to an unlent g that keeps it in a global, reloads it,
hands it to an unlent walker w that writes 0x55 downward over the sentinel at sp+0, and returns the sentinel. When the walk gives up on the first query there
is no floor yet (ScopeLocal::setFrameFloor) and `low` is 0, which `gatherInternal` takes for a parameter reference: no alias. Code at 0x06020000."""
import sys
def fx(n, variant):
    L = ['.word 0x4F22', 'add #-32,r15', 'mov #5,r0', 'mov.l r0,@r15', 'mov #7,r6']
    for _ in range(n): L += ['mov r15,r1', 'add #16,r1', 'mov.l r6,@r1']
    L += ['mov r15,r4', 'add #16,r4', 'mov.l =0x0600F100,r5' if variant == 'g' else 'mov #0,r5']
    if variant == 'g':
        L += ['mov.l =0x06030000,r1', 'jsr @r1', 'nop', 'mov.l =0x0600F100,r4', 'mov.l @r4,r4']
    L += ['mov.l =0x06030080,r1', 'jsr @r1', 'nop', 'mov.l @r15,r0', 'add #32,r15', '.word 0x4F26', 'rts', 'nop']
    g = ['mov.l r4,@r5', 'rts', 'nop']
    w = ['mov #0x55,r2', 'mov #6,r3', 'WL:', 'mov.l r2,@-r4', 'dt r3', 'bf WL', 'rts', 'nop']
    o = ['name R14-19 gaveup N=%d variant=%s' % (n, variant), 'asm 06020000'] + L + ['endasm']
    if variant == 'g': o += ['asm 06030000'] + g + ['endasm']
    o += ['asm 06030080'] + w + ['endasm', 'entry 06020000', 'args 0 0 0 0', 'end', '']
    return o
o = []
for a in sys.argv[1:]:
    o += fx(int(a), 'g')
    o += fx(int(a), 'direct')
print('\n'.join(o))
