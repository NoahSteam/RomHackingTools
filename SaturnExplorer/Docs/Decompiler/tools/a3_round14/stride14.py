#!/usr/bin/env python3
"""stride14.py > stride.src : A3-119, a pointer walked down over a frame by a register stride whose source the value-set analysis cannot read.
Frame 0x200 (adds of 127), the sentinel word (5) at sp+0x40, p = sp+0x1c0 (+r4 when unbounded), three iterations of `p = p -/+ r5 ; *p = r1` with the
stride 0x80; the third store lands on the sentinel, which is returned. Sources of the stride: a constant copy (mov #), a pool halfword, a pool long, a
reload from the frame, a global, an operation on a constant, a parameter (the sign is unknown: A3-9's convention)."""
def adds(r, n, k):
    o = []
    while n > 0:
        d = min(n, 127); o.append('add #%d,%s' % (k * d, r)); n -= d
    return o
def fx(name, setup, op, unb, G='0x0600F180'):
    F, base, sent = 0x200, 0x1c0, 0x40
    L = adds('r15', F, -1) + ['mov r15,r2'] + adds('r2', base, 1) + ['mov r15,r3'] + adds('r3', sent, 1) + ['mov #5,r0', 'mov.l r0,@r3']
    if unb: L += ['add r4,r2']
    L += setup + ['mov #3,r1', 'L:', op, 'mov.l r1,@r2', 'dt r1', 'bf L', 'mov.l @r3,r0'] + adds('r15', F, 1) + ['rts', 'nop']
    return ['name ' + name, 'asm 06009000'] + L + ['endasm', 'long 0600F180 FFFFFF80', 'args 0 0 0 0', 'end', '']
src = {
    'imm-copy': (['mov #-128,r5'], 'add'),
    'pool-halfword': (['mov.w =0xFF80,r5'], 'add'),
    'pool-long': (['mov.l =0xFFFFFF80,r5'], 'add'),
    'frame-reload': (['mov #-128,r5', 'mov.l r5,@(0,r15)', 'mov.l @(0,r15),r5'], 'add'),
    'global-load': (['mov.l =0x0600F180,r5', 'mov.l @r5,r5'], 'add'),
    'shll-of-pool': (['mov.w =0xFFF0,r5', 'shll2 r5'], 'add'),
    'neg-of-pool': (['mov.w =0x0080,r5', 'neg r5,r5'], 'add'),
    'sub-pool-positive': (['mov.w =0x0080,r5'], 'sub'),
    'sub-global-positive': (['mov.l =0x0600F184,r5', 'mov.l @r5,r5'], 'sub'),
    'sub-imm-positive': (['mov #127,r5'], 'sub'),
}
out = []
for k, (setup, op) in src.items():
    for unb in (False, True):
        out += fx('R14-14 stride=%s op=%s unbounded=%d' % (k, op, unb), setup, '%s r5,r2' % op, unb)
print('\n'.join(out))
