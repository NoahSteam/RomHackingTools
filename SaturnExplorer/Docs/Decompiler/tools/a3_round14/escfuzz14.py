#!/usr/bin/env python3
"""escfuzz14.py seed count > out.src : random escape fixtures (area 3 round 14: escfuzz.py with more launderings -- MACH/MACL, mac.l, dmulu.l, mul.l, GBR, VBR, xtrct, the T bit, addc, rotc -- more mediator registers and more walks).

Each fixture is a caller P (06009000) with a frame of locals initialised to distinct small values, a frame address p = r15 + T made and laundered, a
MEDIATOR that keeps p somewhere (a global, a slot of the frame, a register the model keeps, r0, an object in ordinary RAM, or nothing), and a WALKER that
writes 0x55 downward (or by a negative displacement, or by a negative r0 index) from the kept pointer. P then returns one local. The walker's reach
decides whether the text's return value is right; the driver's RUN line is the truth (run.sh / sum.py read it; escsum.py flags a text whose last
constant return differs, or whose oracle rejects).  Callees are lent or left unlent per fixture."""
import random, sys, os

seed = int(sys.argv[1]); n = int(sys.argv[2])
rnd = random.Random(seed)
G1, G2, OBJ = '0x0600F100', '0x0600F140', '0x0600F200'
A_G, A_W, A_H = '0x06009100', '0x06009180', '0x06009200'

LAB = [0]
def lab():
    LAB[0] += 1
    return 'LB%d' % LAB[0]

def launder_kind(rnd_choice=None):
    return rnd.choice(['none', 'none', 'none', 'mask', 'swap2', 'spill', 'global',
                       'macl', 'mach', 'mulone', 'dmulone', 'macmem', 'gbr', 'vbr', 'xtrct2', 'tbit', 'addc0', 'rotc', 'subc0', 'negc2'])

def launder_lines(k, reg, t1, t2):
    L = []
    if k == 'mask': L += ['mov #-4,%s' % t1, 'and %s,%s' % (t1, reg)]
    elif k == 'swap2': L += ['swap.w %s,%s' % (reg, reg), 'swap.w %s,%s' % (reg, reg)]
    elif k == 'spill': L += ['mov.l %s,@-r15' % reg, 'mov.l @r15+,%s' % reg]
    elif k == 'global': L += ['mov.l =%s,%s' % (G2, t1), 'mov.l %s,@%s' % (reg, t1), 'mov.l @%s,%s' % (t1, reg)]
    elif k == 'macl': L += ['lds %s,macl' % reg, 'sts macl,%s' % reg]
    elif k == 'mach': L += ['lds %s,mach' % reg, 'sts mach,%s' % reg]
    elif k == 'mulone': L += ['mov #1,%s' % t1, 'mul.l %s,%s' % (t1, reg), 'sts macl,%s' % reg]
    elif k == 'dmulone': L += ['clrmac', 'mov #1,%s' % t1, 'dmulu.l %s,%s' % (reg, t1), 'sts macl,%s' % reg]
    elif k == 'macmem':
        n = int(t2[1:]); m = int(t1[1:])
        L += ['add #-8,r15', 'mov.l %s,@(0,r15)' % reg, 'mov #1,%s' % t1, 'mov.l %s,@(4,r15)' % t1, 'mov r15,%s' % t1, 'mov r15,%s' % t2, 'add #4,%s' % t2,
              'clrmac', '.word 0x%04X' % (0x000F | (n << 8) | (m << 4)), 'sts macl,%s' % reg, 'add #8,r15']
    elif k == 'gbr': L += ['stc gbr,%s' % t1, 'ldc %s,gbr' % reg, 'stc gbr,%s' % reg, 'ldc %s,gbr' % t1]
    elif k == 'vbr': L += ['stc vbr,%s' % t1, 'ldc %s,vbr' % reg, 'stc vbr,%s' % reg, 'ldc %s,vbr' % t1]
    elif k == 'xtrct2': L += ['xtrct %s,%s' % (reg, reg), 'xtrct %s,%s' % (reg, reg)]
    elif k == 'tbit':
        l = lab()
        L += ['mov #0,%s' % t1, 'mov #32,%s' % t2, l + ':', 'shll %s' % reg, 'rotcl %s' % t1, 'dt %s' % t2, 'bf ' + l, 'mov %s,%s' % (t1, reg)]
    elif k == 'addc0': L += ['clrt', 'mov #0,%s' % t1, 'addc %s,%s' % (t1, reg)]
    elif k == 'subc0': L += ['clrt', 'mov #0,%s' % t1, 'subc %s,%s' % (t1, reg)]
    elif k == 'rotc': L += ['rotcl %s' % reg, 'rotcr %s' % reg]
    elif k == 'negc2': L += ['mov #0,%s' % t1, 'clrt', 'negc %s,%s' % (reg, t1), 'clrt', 'negc %s,%s' % (t1, reg)]
    return L

def launder(reg, tmp):
    k = launder_kind()
    return k, launder_lines(k, reg, tmp, 'r5' if tmp != 'r5' else 'r6')

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
    elif kind == 'subreg':
        L += ['mov #4,r5', 'mov #%d,%s' % (steps, cnt), 'WL:', 'sub r5,%s' % q, 'mov.l %s,@%s' % (val, q), 'dt %s' % cnt, 'bf WL']
    elif kind == 'addreg':
        L += ['mov #-4,r5', 'mov #%d,%s' % (steps, cnt), 'WL:', 'add r5,%s' % q, 'mov.l %s,@%s' % (val, q), 'dt %s' % cnt, 'bf WL']
    elif kind == 'idxdown':
        L += ['mov #0,r5', 'mov #%d,%s' % (steps, cnt), 'WL:', 'add #-4,r5', 'mov r5,r0', 'mov.l %s,@(r0,%s)' % (val, q), 'dt %s' % cnt, 'bf WL']
    elif kind == 'subidx':
        L += ['mov #0,r5', 'mov #%d,%s' % (steps, cnt), 'WL:', 'add #4,r5', 'mov %s,r0' % q, 'sub r5,r0', 'mov.l %s,@r0' % val, 'dt %s' % cnt, 'bf WL']
    elif kind == 'gbrrmw':
        # GBR-based read-modify-write: and.b/or.b/xor.b #imm,@(r0,gbr) at q - 4*steps (the top byte of the word)
        op = rnd.choice([0xCF40, 0xCE40])
        L += ['stc gbr,r5', 'ldc %s,gbr' % q, 'mov #%d,r0' % (-4 * steps), '.word 0x%04X' % op, 'ldc r5,gbr']
    elif kind == 'gbrmov':
        # mov.l r0,@(disp,gbr) with gbr = q - 4*steps
        L += ['stc gbr,r5', 'add #%d,%s' % (-4 * steps, q), 'ldc %s,gbr' % q, 'mov %s,r0' % val, 'mov.l r0,@(0,gbr)', 'ldc r5,gbr']
    elif kind == 'tas':
        L += ['add #%d,%s' % (-4 * steps, q), 'tas.b @%s' % q]
    elif kind == 'movbw':
        L += ['mov #%d,%s' % (steps, cnt), 'WL:', 'mov.b %s,@-%s' % (val, q), 'mov.b %s,@-%s' % (val, q), 'mov.b %s,@-%s' % (val, q), 'mov.b %s,@-%s' % (val, q), 'dt %s' % cnt, 'bf WL']
    elif kind == 'unroll':
        for i in range(steps): L += ['mov.l %s,@-%s' % (val, q)]
    return L

out = []
for i in range(n):
    F = rnd.choice([16, 24, 32, 40])
    T = rnd.choice([t for t in range(0, F + 1, 4)])
    wkind = rnd.choice(['loop', 'loop', 'disp', 'idx', 'unroll', 'up', 'subreg', 'addreg', 'idxdown', 'subidx', 'gbrrmw', 'gbrmov', 'tas', 'movbw'])
    wkind = os.environ.get('ESC_WKIND', wkind)
    if wkind == 'up' and T >= F: T = F - 4
    R = rnd.choice(range(0, F, 4))
    steps = rnd.choice([1, 1, 2, 3, 4, 6])
    med = rnd.choice(['glob', 'glob', 'slot', 'reg', 'ret', 'obj', 'relay', 'none', 'stackarg', 'own', 'ownslot', 'ownmir'])
    med = os.environ.get('ESC_MED', med)
    wk = rnd.choice(['caller', 'lent_w', 'unlent_w', 'arg_lent', 'arg_unlent'])
    wk = os.environ.get('ESC_WK', wk)
    g_lent = rnd.random() < 0.7
    w_lent = wk in ('lent_w', 'arg_lent')
    lk, LL = launder('r4', 'r6')
    if 'ESC_LK' in os.environ:
        lk = os.environ['ESC_LK']; LL = launder_lines(lk, 'r4', 'r6', 'r5')
    rK = rnd.choice(['r1', 'r9', 'r10', 'r11', 'r3', 'mach', 'macl', 'gbr', 'vbr', 'r8', 'r12', 'r13', 'r14'])
    rewrite = rnd.random() < 0.3
    wlk = launder_kind() if rnd.random() < 0.5 else 'none'
    wlk = os.environ.get('ESC_WLK', wlk)
    extra = [r for r in (5, 6, 7) if rnd.random() < 0.4]
    if med in ('slot', 'obj'): extra = [r for r in extra if r != 5]
    S = rnd.choice([s for s in range(0, F, 4) if s != T]) if med == 'slot' else None
    if S is not None and R == S: R = (S + 4) % F
    name = 'EF seed=%d i=%d F=%d T=%d R=%d steps=%d med=%s wk=%s wkind=%s g_lent=%d lk=%s wlk=%s rK=%s rewrite=%d extra=%s' % (
        seed, i, F, T, R, steps, med, wk, wkind, g_lent, lk, wlk, rK, rewrite, ''.join(str(r) for r in extra) or '-')
    P = ['.word 0x4F22', 'add #-%d,r15' % F]
    for off in range(0, F, 4):
        P += ['mov #%d,r0' % (off // 4 + 1), 'mov.l r0,@(%d,r15)' % off]
    P += ['mov r15,r4', 'add #%d,r4' % T] if T else ['mov r15,r4']
    P += LL
    P += ['mov #%d,r%d' % (30 + r, r) for r in extra]
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
        if rK in ('mach', 'macl'):
            g = ['lds r4,%s' % rK, 'rts', 'nop']; load_q = lambda q: ['sts %s,%s' % (rK, q)]
        elif rK in ('gbr', 'vbr'):
            g = ['ldc r4,%s' % rK, 'rts', 'nop']; load_q = lambda q: ['stc %s,%s' % (rK, q)]
        else:
            g = ['mov r4,%s' % rK, 'rts', 'nop']; load_q = lambda q: ['mov %s,%s' % (rK, q)] if q != rK else []
        blocks.append((A_G, g))
        if g_lent: lend.append(A_G)
        P += ['mov.l =%s,r1' % A_G, 'jsr @r1', 'nop'] if rK != 'r1' else ['mov.l =%s,r2' % A_G, 'jsr @r2', 'nop']
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
    elif med == 'own':
        P += ['mov.l =%s,r1' % G1, 'mov.l r4,@r1']
        load_q = lambda q: ['mov.l =%s,%s' % (G1, q), 'mov.l @%s,%s' % (q, q)]
    elif med == 'ownmir':
        P += ['mov.l =%s,r1' % G1, 'mov.l r4,@r1']
        load_q = lambda q: ['mov.l =0x2600F100,%s' % q, 'mov.l @%s,%s' % (q, q)]
    elif med == 'ownslot':
        S = rnd.choice([s2 for s2 in range(0, F, 4) if s2 != T])
        if R == S: R = (S + 4) % F
        P += ['mov.l r4,@(%d,r15)' % S]
        load_q = lambda q: ['mov.l @(%d,r15),%s' % (S, q)]
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
        P += load_q('r6') + launder_lines(wlk, 'r6', 'r3', 'r5') + walk('r6', steps, wkind, 'r7', 'r2')
    elif wk in ('lent_w', 'unlent_w'):
        w = load_q('r6') if False else None
        # the callee reads the pointer from where the mediator kept it (global/object), or is handed it in r4 when the mediator is register-only
        if med in ('glob', 'relay', 'obj', 'stackarg', 'own', 'ownmir'):
            body = load_q('r1') + launder_lines(wlk, 'r1', 'r5', 'r6') + walk('r1', steps, wkind, 'r3', 'r2') + ['rts', 'nop']
            blocks.append((A_W, body))
            if w_lent: lend.append(A_W)
            P += ['mov.l =%s,r1' % A_W, 'jsr @r1', 'nop']
        else:
            body = launder_lines(wlk, 'r4', 'r5', 'r6') + walk('r4', steps, wkind, 'r3', 'r2') + ['rts', 'nop']
            blocks.append((A_W, body))
            if w_lent: lend.append(A_W)
            P += load_q('r4') + ['mov.l =%s,r1' % A_W, 'jsr @r1', 'nop']
    else:  # arg_lent / arg_unlent: the walker is handed the kept pointer in r4
        body = launder_lines(wlk, 'r4', 'r5', 'r6') + walk('r4', steps, wkind, 'r3', 'r2') + ['rts', 'nop']
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
