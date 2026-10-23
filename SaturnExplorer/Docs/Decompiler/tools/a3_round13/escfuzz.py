#!/usr/bin/env python3
"""escfuzz.py seed count > out.src : random escape fixtures (area 3 round 13).

Each fixture is a caller P (06009000) with a frame of locals initialised to distinct small values, a frame address p = r15 + T made and laundered, a
MEDIATOR that keeps p somewhere (a global, a slot of the frame, a register the model keeps, r0, an object in ordinary RAM, or nothing), and a WALKER that
writes 0x55 downward (or by a negative displacement, or by a negative r0 index) from the kept pointer. P then returns one local. The walker's reach
decides whether the text's return value is right; the driver's RUN line is the truth (run.sh / sum.py read it; escsum.py flags a text whose last
constant return differs, or whose oracle rejects).  Callees are lent or left unlent per fixture."""
import random, sys

seed = int(sys.argv[1]); n = int(sys.argv[2])
rnd = random.Random(seed)
G1, G2, OBJ = '0x0600F100', '0x0600F140', '0x0600F200'
A_G, A_W, A_H = '0x06009100', '0x06009180', '0x06009200'

def launder(reg, tmp):
    k = rnd.choice(['none', 'none', 'none', 'mask', 'swap2', 'spill', 'global'])
    L = []
    if k == 'mask': L += ['mov #-4,%s' % tmp, 'and %s,%s' % (tmp, reg)]
    elif k == 'swap2': L += ['swap.w %s,%s' % (reg, reg), 'swap.w %s,%s' % (reg, reg)]
    elif k == 'spill': L += ['mov.l %s,@-r15' % reg, 'mov.l @r15+,%s' % reg]
    elif k == 'global': L += ['mov.l =%s,%s' % (G2, tmp), 'mov.l %s,@%s' % (reg, tmp), 'mov.l @%s,%s' % (tmp, reg)]
    return k, L

def walk(q, steps, kind, cnt='r3', val='r2'):
    """lines that store 0x55 below pointer register q"""
    L = ['mov #0x55,%s' % val]
    if kind == 'loop':
        L += ['mov #%d,%s' % (steps, cnt), 'WL:', 'mov.l %s,@-%s' % (val, q), 'dt %s' % cnt, 'bf WL']
    elif kind == 'disp':
        L += ['add #%d,%s' % (-4 * steps, q), 'mov.l %s,@%s' % (val, q)]
    elif kind == 'idx':
        L += ['mov #%d,r0' % (-4 * steps), 'mov.l %s,@(r0,%s)' % (val, q)]
    elif kind == 'up':
        L += ['mov.l %s,@%s' % (val, q)]
    elif kind == 'unroll':
        for i in range(steps): L += ['mov.l %s,@-%s' % (val, q)]
    return L

out = []
for i in range(n):
    F = rnd.choice([16, 24, 32, 40])
    T = rnd.choice([t for t in range(0, F + 1, 4)])
    R = rnd.choice(range(0, F, 4))
    steps = rnd.choice([1, 1, 2, 3, 4, 6])
    med = rnd.choice(['glob', 'glob', 'slot', 'reg', 'ret', 'obj', 'relay', 'none', 'stackarg'])
    wk = rnd.choice(['caller', 'lent_w', 'unlent_w', 'arg_lent', 'arg_unlent'])
    wkind = rnd.choice(['loop', 'loop', 'disp', 'idx', 'unroll', 'up'])
    g_lent = rnd.random() < 0.7
    w_lent = wk in ('lent_w', 'arg_lent')
    lk, LL = launder('r4', 'r1')
    rK = rnd.choice(['r1', 'r9', 'r10', 'r11', 'r3'])
    rewrite = rnd.random() < 0.3
    S = rnd.choice([s for s in range(0, F, 4) if s != T]) if med == 'slot' else None
    if S is not None and R == S: R = (S + 4) % F
    name = 'EF seed=%d i=%d F=%d T=%d R=%d steps=%d med=%s wk=%s wkind=%s g_lent=%d lk=%s rK=%s rewrite=%d' % (
        seed, i, F, T, R, steps, med, wk, wkind, g_lent, lk, rK, rewrite)
    P = ['.word 0x4F22', 'add #-%d,r15' % F]
    for off in range(0, F, 4):
        P += ['mov #%d,r0' % (off // 4 + 1), 'mov.l r0,@(%d,r15)' % off]
    P += ['mov r15,r4', 'add #%d,r4' % T] if T else ['mov r15,r4']
    P += LL
    blocks = []
    lend = []
    # --- mediator
    if med in ('glob', 'relay'):
        g = ['mov.l =%s,r1' % G1, 'mov.l r4,@r1', 'rts', 'nop']
        blocks.append((A_G, g));
        if g_lent: lend.append(A_G)
        P += ['mov.l =%s,r1' % A_G, 'jsr @r1', 'nop']
        if med == 'relay':
            h = ['mov.l =%s,r1' % G1, 'mov.l @r1,r2', 'mov.l =%s,r1' % G2, 'mov.l r2,@r1', 'rts', 'nop']
            blocks.append((A_H, h))
            if g_lent: lend.append(A_H)
            P += ['mov.l =%s,r1' % A_H, 'jsr @r1', 'nop']
            src = G2
        else:
            src = G1
        load_q = lambda q: ['mov.l =%s,%s' % (src, q), 'mov.l @%s,%s' % (q, q)]
    elif med == 'slot':
        g = ['mov.l r4,@r5', 'rts', 'nop']
        blocks.append((A_G, g))
        if g_lent: lend.append(A_G)
        P += ['mov r15,r5', 'add #%d,r5' % S] if S else ['mov r15,r5']
        P += ['mov.l =%s,r1' % A_G, 'jsr @r1', 'nop']
        load_q = lambda q: ['mov.l @(%d,r15),%s' % (S, q)] if S < 64 else ['mov.l @r15,%s' % q]
    elif med == 'reg':
        g = ['mov r4,%s' % rK, 'rts', 'nop']
        blocks.append((A_G, g))
        if g_lent: lend.append(A_G)
        P += ['mov.l =%s,r1' % A_G, 'jsr @r1', 'nop'] if rK != 'r1' else ['mov.l =%s,r2' % A_G, 'jsr @r2', 'nop']
        load_q = lambda q: ['mov %s,%s' % (rK, q)] if q != rK else []
    elif med == 'ret':
        g = ['mov r4,r0', 'rts', 'nop']
        blocks.append((A_G, g))
        if g_lent: lend.append(A_G)
        P += ['mov.l =%s,r1' % A_G, 'jsr @r1', 'nop']
        load_q = lambda q: ['mov r0,%s' % q]
    elif med == 'obj':
        g = ['mov.l r4,@(4,r5)', 'rts', 'nop']
        blocks.append((A_G, g))
        if g_lent: lend.append(A_G)
        P += ['mov.l =%s,r5' % OBJ, 'mov.l =%s,r1' % A_G, 'jsr @r1', 'nop']
        load_q = lambda q: ['mov.l =%s,%s' % (OBJ, q), 'mov.l @(4,%s),%s' % (q, q)]
    elif med == 'stackarg':
        # the address goes out as the fifth argument (outgoing word at the call's r15)
        g = ['mov.l @r15,r1', 'mov.l =%s,r2' % G1, 'mov.l r1,@r2', 'rts', 'nop']
        blocks.append((A_G, g))
        if g_lent: lend.append(A_G)
        P += ['add #-4,r15', 'mov.l r4,@r15', 'mov.l =%s,r1' % A_G, 'jsr @r1', 'nop', 'add #4,r15']
        load_q = lambda q: ['mov.l =%s,%s' % (G1, q), 'mov.l @%s,%s' % (q, q)]
        # (the frame offsets above are those of P after the add #4; the push is below them)
    else:  # none: the walker is handed p directly
        load_q = lambda q: ['mov r15,%s' % q, 'add #%d,%s' % (T, q)] if T else ['mov r15,%s' % q]
    if rewrite:
        P += ['mov #%d,r0' % 99, 'mov.l r0,@(%d,r15)' % R]
    # --- walker
    if wk == 'caller':
        P += load_q('r6') + walk('r6', steps, wkind, 'r7', 'r2')
    elif wk in ('lent_w', 'unlent_w'):
        w = load_q('r6') if False else None
        # the callee reads the pointer from where the mediator kept it (global/object), or is handed it in r4 when the mediator is register-only
        if med in ('glob', 'relay', 'obj', 'stackarg'):
            body = load_q('r1') + walk('r1', steps, wkind, 'r3', 'r2') + ['rts', 'nop']
            blocks.append((A_W, body))
            if w_lent: lend.append(A_W)
            P += ['mov.l =%s,r1' % A_W, 'jsr @r1', 'nop']
        else:
            body = walk('r4', steps, wkind, 'r3', 'r2') + ['rts', 'nop']
            blocks.append((A_W, body))
            if w_lent: lend.append(A_W)
            P += load_q('r4') + ['mov.l =%s,r1' % A_W, 'jsr @r1', 'nop']
    else:  # arg_lent / arg_unlent: the walker is handed the kept pointer in r4
        body = walk('r4', steps, wkind, 'r3', 'r2') + ['rts', 'nop']
        blocks.append((A_W, body))
        if w_lent: lend.append(A_W)
        P += load_q('r4') + ['mov.l =%s,r1' % A_W, 'jsr @r1', 'nop']
    P += ['mov.l @(%d,r15),r0' % R, 'add #%d,r15' % F, '.word 0x4F26', 'rts', 'nop']
    # unroll with a counter register needs none; 'loop' uses r3/r7 as hidden counters set inside the walker's own code
    out.append('name ' + name)
    out.append('asm 06009000'); out += P; out.append('endasm')
    for a, b in blocks:
        out.append('asm ' + a[2:].upper().rjust(8, '0') if False else 'asm %08X' % int(a, 16)); out += b; out.append('endasm')
    if lend: out.append('lend ' + ' '.join('%08X' % int(a, 16) for a in lend))
    out += ['quiet', 'args 0 0 0 0', 'end']
print('\n'.join(out))
