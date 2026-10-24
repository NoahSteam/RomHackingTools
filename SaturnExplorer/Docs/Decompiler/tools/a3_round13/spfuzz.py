#!/usr/bin/env python3
"""spfuzz.py seed count > out.src : the spare (A3-93) fixtures of area 3 round 13. f (06009100) pushes r14, makes the address of that saved word in several
ways, round-trips it through a channel (the same as hbfuzz's), and hands it to a lent writer h (06009180: mov.l 7,@r4) which overwrites the saved r14;
f pops r14. The top function P (06009000) sets r14 = 0x1234 (a pool literal), calls f and returns r14 in r0. Run: 7. A text that returns 0x1234 (or
an entry-r14 name) is stale; f lent with r14 in its preserved set is the wrong claim (spsum.py)."""
import random, sys
seed = int(sys.argv[1]); n = int(sys.argv[2]); rnd = random.Random(seed)
G1 = '0x0600F300'
def obtain():
    k = rnd.choice(['exact', 'laundered_mask', 'laundered_swap', 'index_sum'])
    if k == 'exact': return k, ['mov r15,r4']
    if k == 'laundered_mask': return k, ['mov r15,r4', 'mov #-4,r2', 'and r2,r4']
    if k == 'laundered_swap': return k, ['mov r15,r4', 'swap.w r4,r4', 'swap.w r4,r4']
    return k, ['mov r15,r4', 'mov #0,r2', 'add r2,r4']
def channel():
    k = rnd.choice(['direct', 'slot_exact', 'slot_swap', 'slot_mask', 'global', 'global_via_slot', 'reg_r9', 'push_pop'])
    if k == 'direct': return k, []
    # a spare local below the saved word: r15 - 4
    if k == 'slot_exact': return k, ['add #-4,r15', 'mov.l r4,@r15', 'mov.l @r15,r4', 'add #4,r15']
    if k == 'slot_swap': return k, ['add #-4,r15', 'mov.l r4,@r15', 'mov r15,r2', 'swap.w r2,r2', 'swap.w r2,r2', 'mov.l @r2,r4', 'add #4,r15']
    if k == 'slot_mask': return k, ['add #-4,r15', 'mov.l r4,@r15', 'mov r15,r2', 'mov #-4,r3', 'and r3,r2', 'mov.l @r2,r4', 'add #4,r15']
    if k == 'global': return k, ['mov.l =%s,r2' % G1, 'mov.l r4,@r2', 'mov.l @r2,r4']
    if k == 'global_via_slot': return k, ['add #-4,r15', 'mov.l r4,@r15', 'mov.l =%s,r2' % G1, 'mov.l r15,@r2', 'mov.l @r2,r2', 'mov.l @r2,r4', 'add #4,r15']
    if k == 'reg_r9': return k, ['mov r4,r9', 'mov r9,r4']
    return k, ['mov.l r4,@-r15', 'mov.l @r15+,r4']
out = []
for i in range(n):
    ok, O = obtain(); ck, C = channel()
    f = ['.word 0x4F22', 'mov.l r14,@-r15'] + O + C + ['mov.l =0x06009180,r1', 'jsr @r1', 'nop', 'mov.l @r15+,r14', '.word 0x4F26', 'rts', 'nop']
    P = ['.word 0x4F22', 'mov.l =0x1234,r14', 'mov.l =0x06009100,r1', 'jsr @r1', 'nop', 'mov r14,r0', '.word 0x4F26', 'rts', 'nop']
    h = ['mov #7,r0', 'mov.l r0,@r4', 'rts', 'nop']
    out.append('name SP seed=%d i=%d obtain=%s channel=%s' % (seed, i, ok, ck))
    out += ['asm 06009000'] + P + ['endasm', 'asm 06009100'] + f + ['endasm', 'asm 06009180'] + h + ['endasm', 'lend 06009180 06009100', 'quiet', 'args 0 0 0 0', 'end']
print('\n'.join(out))
