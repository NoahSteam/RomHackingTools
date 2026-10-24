#!/usr/bin/env python3
"""hbfuzz.py seed count > out.src : hand-back fixtures (area 3 round 13). A lent-able callee g (06009100) obtains a pointer to its caller's outgoing word
(its own entry r15, word 0 or 1) in a chosen way, round-trips it through a chosen channel, and writes 7 through it; the caller P (06009000) writes 5 to
its outgoing word(s), calls g and returns the word. Run: 7 (or 8 for word 1). A text that returns 5 is stale; g lent while it writes the caller's frame
is a wrong lent claim (hbsum.py reads both)."""
import random, sys
seed = int(sys.argv[1]); n = int(sys.argv[2]); rnd = random.Random(seed)
G1 = '0x0600F300'
def obtain(w):   # pointer to the entry-r15 word w in r3 (g has pushed 4 bytes: slot L at r15+0, entry r15 = r15+4)
    k = rnd.choice(['exact', 'exact_add', 'laundered_mask', 'laundered_swap', 'from_pr_slot_math'])
    off = 4 + 4 * w
    if k == 'exact': return k, ['mov r15,r3', 'add #%d,r3' % off]
    if k == 'exact_add': return k, ['mov r15,r3', 'add #4,r3', 'add #%d,r3' % (4 * w)] if w else ['mov r15,r3', 'add #4,r3']
    if k == 'laundered_mask': return k, ['mov r15,r3', 'add #%d,r3' % off, 'mov #-4,r4', 'and r4,r3']
    if k == 'laundered_swap': return k, ['mov r15,r3', 'add #%d,r3' % off, 'swap.w r3,r3', 'swap.w r3,r3']
    return k, ['mov r15,r3', 'mov #%d,r4' % off, 'add r4,r3']
def channel():
    k = rnd.choice(['direct', 'slot_exact', 'slot_swap', 'slot_mask', 'slot_r0', 'global', 'global_via_slot', 'reg_r9', 'push_pop', 'gbr', 'mach', 'macl', 'helper'])
    if k == 'direct': return k, []
    if k == 'slot_exact': return k, ['mov.l r3,@r15', 'mov.l @r15,r3']
    if k == 'slot_swap': return k, ['mov.l r3,@r15', 'mov r15,r2', 'swap.w r2,r2', 'swap.w r2,r2', 'mov.l @r2,r3']
    if k == 'slot_mask': return k, ['mov.l r3,@r15', 'mov r15,r2', 'mov #-4,r4', 'and r4,r2', 'mov.l @r2,r3']
    if k == 'slot_r0': return k, ['mov.l r3,@r15', 'mov #0,r0', 'mov.l @(r0,r15),r3']
    if k == 'global': return k, ['mov.l =%s,r2' % G1, 'mov.l r3,@r2', 'mov.l @r2,r3']
    if k == 'global_via_slot': return k, ['mov.l r3,@r15', 'mov.l =%s,r2' % G1, 'mov.l r15,@r2', 'mov.l @r2,r2', 'mov.l @r2,r3']
    if k == 'reg_r9': return k, ['mov r3,r9', 'mov r9,r3']
    if k == 'gbr': return k, ['stc gbr,r5', 'ldc r3,gbr', 'stc gbr,r3', 'ldc r5,gbr']
    if k == 'mach': return k, ['lds r3,mach', 'sts mach,r3']
    if k == 'macl': return k, ['lds r3,macl', 'sts macl,r3']
    if k == 'helper': return k, ['mov r3,r4', 'mov.l =0x06009180,r1', '.word 0x4F22', 'jsr @r1', 'nop', '.word 0x4F26', 'mov r0,r3']
    return k, ['mov.l r3,@-r15', 'mov.l @r15+,r3']
def write(w):
    k = rnd.choice(['mov', 'movb_hi', 'predec', 'tas', 'two'])
    if k == 'mov': return k, ['mov #7,r0', 'mov.l r0,@r3']
    if k == 'movb_hi': return k, ['mov #7,r0', 'mov.b r0,@(3,r3)'] if False else ['mov #7,r0', 'add #3,r3', 'mov.b r0,@r3']
    if k == 'predec': return k, ['add #4,r3', 'mov #7,r0', 'mov.l r0,@-r3']
    if k == 'tas': return k, ['mov #7,r0', 'mov.l r0,@r3']
    return k, ['mov #7,r0', 'mov.l r0,@r3', 'mov.l r0,@r3']
out = []
for i in range(n):
    w = rnd.choice([0, 0, 1])
    ok, O = obtain(w); ck, C = channel(); wk, W = write(w)
    g = ['add #-4,r15'] + O + C + W + ['add #4,r15', 'rts', 'nop']
    # P writes the words; the expected value is 7 in word w for 'mov'-like writes (the byte store changes only the low byte: 7 over 5 -> 0x05000007? keep mov only for exact value)
    P = ['.word 0x4F22', 'add #-16,r15', 'add #-8,r15', 'mov #5,r0', 'mov.l r0,@r15', 'mov #6,r0', 'mov.l r0,@(4,r15)',
         'mov.l =0x06009100,r1', 'jsr @r1', 'nop', 'mov.l @(%d,r15),r0' % (4 * w), 'add #24,r15', '.word 0x4F26', 'rts', 'nop']
    out.append('name HB seed=%d i=%d w=%d obtain=%s channel=%s write=%s' % (seed, i, w, ok, ck, wk))
    out += ['asm 06009000'] + P + ['endasm', 'asm 06009100'] + g + ['endasm', 'asm 06009180', 'mov r4,r0', 'rts', 'nop', 'endasm', 'lend 06009180 06009100', 'quiet', 'args 0 0 0 0', 'end']
print('\n'.join(out))
