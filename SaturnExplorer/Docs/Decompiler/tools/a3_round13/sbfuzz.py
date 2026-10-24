#!/usr/bin/env python3
"""sbfuzz.py seed count > out.src : the storesBelow record (area 3 round 13). A lent writer g (06009100) is handed p = r15 + 8 of its caller in r4, makes q = p - 4k
in a chosen way, round-trips it through a chosen channel, and writes 0x55 through it. The caller P (06009000) has locals 1..4 at r15+0..12, calls g with r4 = p and
returns the local at r15+4*j. A text that returns the stored constant while the run returns 0x55 is stale (sbsum.py)."""
import random, sys
seed = int(sys.argv[1]); n = int(sys.argv[2]); rnd = random.Random(seed)
G1 = '0x0600F300'
def below(k):
    f = rnd.choice(['add', 'sub_reg', 'neg_add', 'launder_add', 'predec', 'mask_after'])
    if f == 'add': return f, ['add #-%d,r4' % (4 * k)]
    if f == 'sub_reg': return f, ['mov #%d,r1' % (4 * k), 'sub r1,r4']
    if f == 'neg_add': return f, ['mov #%d,r1' % (4 * k), 'neg r1,r1', 'add r1,r4']
    if f == 'launder_add': return f, ['swap.w r4,r4', 'swap.w r4,r4', 'add #-%d,r4' % (4 * k)]
    if f == 'predec': return f, ['mov #0x55,r2'] + ['mov.l r2,@-r4'] * k + ['add #4,r4']
    return f, ['add #-%d,r4' % (4 * k), 'mov #-4,r1', 'and r1,r4']
def channel():
    c = rnd.choice(['direct', 'slot_exact', 'slot_swap', 'slot_mask', 'global', 'global_via_slot', 'push_pop', 'r9'])
    if c == 'direct': return c, []
    if c == 'slot_exact': return c, ['add #-4,r15', 'mov.l r4,@r15', 'mov.l @r15,r4', 'add #4,r15']
    if c == 'slot_swap': return c, ['add #-4,r15', 'mov.l r4,@r15', 'mov r15,r2', 'swap.w r2,r2', 'swap.w r2,r2', 'mov.l @r2,r4', 'add #4,r15']
    if c == 'slot_mask': return c, ['add #-4,r15', 'mov.l r4,@r15', 'mov r15,r2', 'mov #-4,r3', 'and r3,r2', 'mov.l @r2,r4', 'add #4,r15']
    if c == 'global': return c, ['mov.l =%s,r2' % G1, 'mov.l r4,@r2', 'mov.l @r2,r4']
    if c == 'global_via_slot': return c, ['add #-4,r15', 'mov.l r4,@r15', 'mov.l =%s,r2' % G1, 'mov.l r15,@r2', 'mov.l @r2,r2', 'mov.l @r2,r4', 'add #4,r15']
    if c == 'push_pop': return c, ['mov.l r4,@-r15', 'mov.l @r15+,r4']
    return c, ['mov r4,r9', 'mov r9,r4']
out = []
for i in range(n):
    k = rnd.choice([1, 2]); j = rnd.choice([0, 1])
    bk, B = below(k); ck, C = channel()
    store = [] if bk == 'predec' else ['mov #0x55,r2', 'mov.l r2,@r4']
    g = B + C + store + ['rts', 'nop']
    # g's stored words: k words below p = r15+8: words at r15+4 (k=1) or r15+0, r15+4 (k=2); j chooses the local read
    P = ['.word 0x4F22', 'add #-16,r15', 'mov #1,r0', 'mov.l r0,@(0,r15)', 'mov #2,r0', 'mov.l r0,@(4,r15)', 'mov #3,r0', 'mov.l r0,@(8,r15)', 'mov #4,r0', 'mov.l r0,@(12,r15)',
         'mov r15,r4', 'add #8,r4', 'mov.l =0x06009100,r1', 'jsr @r1', 'nop', 'mov.l @(%d,r15),r0' % (4 * j), 'add #16,r15', '.word 0x4F26', 'rts', 'nop']
    out.append('name SB seed=%d i=%d k=%d j=%d below=%s channel=%s' % (seed, i, k, j, bk, ck))
    out += ['asm 06009000'] + P + ['endasm', 'asm 06009100'] + g + ['endasm', 'lend 06009100', 'quiet', 'args 0 0 0 0', 'end']
print('\n'.join(out))
