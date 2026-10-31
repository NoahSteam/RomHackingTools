#!/usr/bin/env python3
"""hbfuzz14.py seed count > out.src : hand-back fixtures (area 3 round 14), hbfuzz.py with a 12-byte frame in g, more ways to obtain the pointer (the
round 13 five, each then laundered by MACH/MACL, mul.l, mac.l, GBR, VBR, xtrct, the T bit, addc, rotc), more channels (halves, bytes, two hops, a published
slot address) and more writes (tas.b, a read-modify-write byte). g (06009100) makes a pointer to its caller's outgoing word (its entry r15, word 0 or 1),
sends it through a channel and writes through it; P (06009000) writes 5 and 6 to its words, calls g and returns word w. A stale text returns 5."""
import random, sys
seed = int(sys.argv[1]); n = int(sys.argv[2]); rnd = random.Random(seed)
G1 = '0x0600F300'; GF = 12
LAB = [0]
def lab():
    LAB[0] += 1
    return 'HB%d' % LAB[0]
def lau(k, reg, t1, t2):
    L = []
    if k == 'macl': L += ['lds %s,macl' % reg, 'sts macl,%s' % reg]
    elif k == 'mach': L += ['lds %s,mach' % reg, 'sts mach,%s' % reg]
    elif k == 'mulone': L += ['mov #1,%s' % t1, 'mul.l %s,%s' % (t1, reg), 'sts macl,%s' % reg]
    elif k == 'dmulone': L += ['clrmac', 'mov #1,%s' % t1, 'dmulu.l %s,%s' % (reg, t1), 'sts macl,%s' % reg]
    elif k == 'macmem':
        a = int(t1[1:]); b = int(t2[1:])
        L += ['add #-8,r15', 'mov.l %s,@(0,r15)' % reg, 'mov #1,%s' % t1, 'mov.l %s,@(4,r15)' % t1, 'mov r15,%s' % t1, 'mov r15,%s' % t2, 'add #4,%s' % t2,
              'clrmac', '.word 0x%04X' % (0x000F | (b << 8) | (a << 4)), 'sts macl,%s' % reg, 'add #8,r15']
    elif k == 'gbr': L += ['stc gbr,%s' % t1, 'ldc %s,gbr' % reg, 'stc gbr,%s' % reg, 'ldc %s,gbr' % t1]
    elif k == 'vbr': L += ['stc vbr,%s' % t1, 'ldc %s,vbr' % reg, 'stc vbr,%s' % reg, 'ldc %s,vbr' % t1]
    elif k == 'xtrct2': L += ['xtrct %s,%s' % (reg, reg), 'xtrct %s,%s' % (reg, reg)]
    elif k == 'tbit':
        l = lab()
        L += ['mov #0,%s' % t1, 'mov #32,%s' % t2, l + ':', 'shll %s' % reg, 'rotcl %s' % t1, 'dt %s' % t2, 'bf ' + l, 'mov %s,%s' % (t1, reg)]
    elif k == 'addc0': L += ['clrt', 'mov #0,%s' % t1, 'addc %s,%s' % (t1, reg)]
    elif k == 'rotc': L += ['rotcl %s' % reg, 'rotcr %s' % reg]
    return L
LK = ['none', 'none', 'macl', 'mach', 'mulone', 'dmulone', 'macmem', 'gbr', 'vbr', 'xtrct2', 'tbit', 'addc0', 'rotc']
def obtain(w):   # pointer to the entry-r15 word w in r3 (g has taken GF bytes: entry r15 = r15 + GF)
    k = rnd.choice(['exact', 'exact_add', 'laundered_mask', 'laundered_swap', 'from_pr_slot_math', 'via_r14'])
    off = GF + 4 * w
    if k == 'exact': L = ['mov r15,r3', 'add #%d,r3' % off]
    elif k == 'exact_add': L = ['mov r15,r3', 'add #%d,r3' % GF, 'add #%d,r3' % (4 * w)]
    elif k == 'laundered_mask': L = ['mov r15,r3', 'add #%d,r3' % off, 'mov #-4,r4', 'and r4,r3']
    elif k == 'laundered_swap': L = ['mov r15,r3', 'add #%d,r3' % off, 'swap.w r3,r3', 'swap.w r3,r3']
    elif k == 'via_r14': L = ['mov r15,r14', 'add #%d,r14' % off, 'mov r14,r3']
    else: L = ['mov r15,r3', 'mov #%d,r4' % off, 'add r4,r3']
    lk = rnd.choice(LK)
    return k + '+' + lk, L + lau(lk, 'r3', 'r4', 'r2')
def channel():
    k = rnd.choice(['direct', 'slot_exact', 'slot_swap', 'slot_mask', 'slot_r0', 'global', 'global_via_slot', 'reg_r9', 'push_pop', 'gbr', 'mach', 'macl', 'helper',
                    'half', 'bytes', 'two_hop', 'pub_slot', 'slot_far', 'two_slots'])
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
    if k == 'half': return k, ['mov r15,r2', 'add #2,r2', 'mov.w r3,@r2', 'swap.w r3,r4', 'mov.w r4,@r15', 'mov.l @r15,r3']
    if k == 'bytes':
        return k, ['mov r15,r2', 'add #3,r2', 'mov r3,r4', 'mov.b r4,@r2', 'shlr8 r4', 'add #-1,r2', 'mov.b r4,@r2', 'shlr8 r4', 'add #-1,r2', 'mov.b r4,@r2',
                   'shlr8 r4', 'add #-1,r2', 'mov.b r4,@r2', 'mov.l @r15,r3']
    if k == 'two_hop': return k, ['mov.l r3,@r15', 'mov r15,r2', 'mov.l r2,@(4,r15)', 'mov.l @(4,r15),r2', 'mov.l @r2,r3']
    if k == 'pub_slot': return k, ['mov.l r3,@r15', 'mov.l =%s,r2' % G1, 'mov r15,r4', 'mov.l r4,@r2', 'mov.l @r2,r2', 'mov.l @r2,r3']
    if k == 'slot_far': return k, ['mov.l r3,@(8,r15)', 'mov.l =%s,r2' % G1, 'mov r15,r4', 'add #8,r4', 'mov.l r4,@r2', 'mov.l @r2,r2', 'mov.l @r2,r3']
    return k, ['mov.l r3,@(4,r15)', 'mov.l r3,@(8,r15)', 'mov.l @(8,r15),r3']
def write(w):
    k = rnd.choice(['mov', 'movb_hi', 'predec', 'tas', 'two', 'rmw', 'movw'])
    if k == 'mov': return k, ['mov #7,r0', 'mov.l r0,@r3']
    if k == 'movb_hi': return k, ['mov #7,r0', 'add #3,r3', 'mov.b r0,@r3']
    if k == 'predec': return k, ['add #4,r3', 'mov #7,r0', 'mov.l r0,@-r3']
    if k == 'tas': return k, ['tas.b @r3']
    if k == 'rmw': return k, ['mov.b @r3,r0', 'or #0x20,r0', 'mov.b r0,@r3']
    if k == 'movw': return k, ['mov #7,r0', 'add #2,r3', 'mov.w r0,@r3']
    return k, ['mov #7,r0', 'mov.l r0,@r3', 'mov.l r0,@r3']
out = []
for i in range(n):
    w = rnd.choice([0, 0, 1])
    ok, O = obtain(w); ck, C = channel(); wk, W = write(w)
    g = ['add #-%d,r15' % GF] + O + C + W + ['add #%d,r15' % GF, 'rts', 'nop']
    P = ['.word 0x4F22', 'add #-16,r15', 'add #-8,r15', 'mov #5,r0', 'mov.l r0,@r15', 'mov #6,r0', 'mov.l r0,@(4,r15)',
         'mov.l =0x06009100,r1', 'jsr @r1', 'nop', 'mov.l @(%d,r15),r0' % (4 * w), 'add #24,r15', '.word 0x4F26', 'rts', 'nop']
    out.append('name HB seed=%d i=%d w=%d obtain=%s channel=%s write=%s' % (seed, i, w, ok, ck, wk))
    out += ['asm 06009000'] + P + ['endasm', 'asm 06009100'] + g + ['endasm', 'asm 06009180', 'mov r4,r0', 'rts', 'nop', 'endasm', 'lend 06009180 06009100', 'quiet', 'args 0 0 0 0', 'end']
print('\n'.join(out))
