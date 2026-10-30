#!/usr/bin/env python3
"""frame15.py SEED COUNT > file.src : frame-struct fixtures beyond the frame124 stream.
A struct S (16 bytes) in a 64-byte frame; &S reached through a chain of L pointer slots (L = 1..3); N = 1..24 stores of
byte/halfword/word width at any legal offset through the reloaded pointer (or direct); 1..3 reads of any width/offset;
optional hand-off of &S to a lent callee that reads S[0..], or to an unlent one. Knobs via env: F15_LEVELS, F15_NST, F15_CALL."""
import sys, random, os
seed = int(sys.argv[1]); count = int(sys.argv[2])
rnd = random.Random(seed)
def pick(env, lo, hi):
    v = os.environ.get(env)
    if v: return int(v)
    return rnd.randint(lo, hi)
out = []
for k in range(count):
    L = pick('F15_LEVELS', 1, 3)
    N = pick('F15_NST', 1, 24)
    call = os.environ.get('F15_CALL') or rnd.choice(['none', 'none', 'lent', 'unlent'])
    fillmode = rnd.choice((os.environ.get('F15_FILL') or 'words,bytes,halves,none,save,save').split(','))
    sOff = rnd.choice([0, 4, 8, 12, 16])
    base = 32          # pointer slots at 32.. (frame 48 bytes)
    F = 48
    slots = [base + 4*i for i in range(L)]   # slot i holds: i==0 -> &S ; else &slot[i-1]
    asm = []
    A = asm.append
    A('.word 0x4F22')
    A('add #-%d,r15' % F)
    # set pointer chain
    A('mov r15,r1'); A('add #%d,r1' % sOff); A('mov.l r1,@(%d,r15)' % slots[0])
    for i in range(1, L):
        A('mov r15,r1'); A('add #%d,r1' % slots[i-1]); A('mov.l r1,@(%d,r15)' % slots[i])
    def reload(reg):
        A('mov.l @(%d,r15),%s' % (slots[L-1], reg))
        for i in range(L-1):
            A('mov.l @%s,%s' % (reg, reg))
    def addr(reg, off, via):
        # reg holds the address of S+off
        if via == 'ptr':
            reload(reg)
        else:
            A('mov r15,%s' % reg); A('add #%d,%s' % (sOff, reg))
        if off:
            A('add #%d,%s' % (off, reg))
    def store(width, off, srcreg, via):
        addr('r1', off, via)
        A('mov.%s %s,@r1' % ({1:'b',2:'w',4:'l'}[width], srcreg))
    # fill
    if fillmode == 'words':
        A('mov #-1,r2'); A('mov #0,r3')
        store(4, 0, 'r2', 'ptr'); store(4, 4, 'r3', 'ptr'); store(4, 8, 'r2', 'ptr'); store(4, 12, 'r3', 'ptr')
    elif fillmode == 'bytes':
        for o in rnd.sample(range(16), 16):
            A('mov #%d,r2' % rnd.randint(1, 120)); store(1, o, 'r2', 'ptr')
    elif fillmode == 'save':
        # the entry values of callee-saved registers stored whole into S (words), or one word only
        regs = rnd.sample(['r8', 'r9', 'r10', 'r11', 'r12', 'r13', 'r14'], 4)
        for wi in range(4):
            if rnd.random() < 0.85:
                store(4, 4*wi, regs[wi], rnd.choice(['direct', 'direct', 'ptr']))
            else:
                A('mov #-1,r2'); store(4, 4*wi, 'r2', 'ptr')
    elif fillmode == 'halves':
        for o in rnd.sample(range(0, 16, 2), 8):
            A('mov #%d,r2' % rnd.randint(1, 120)); store(2, o, 'r2', 'ptr')
    for i in range(N):
        w = rnd.choice([1, 1, 1, 2, 2, 4])
        off = rnd.randrange(0, 16, w)
        src = rnd.choice(['r4', 'r5', 'r6', 'r7'])
        via = rnd.choice(['ptr', 'ptr', 'ptr', 'direct'])
        if rnd.random() < 0.15:
            A('mov #%d,r2' % rnd.randint(-120, 120)); src = 'r2'
        store(w, off, src, via)
    if call != 'none':
        reload('r4')
        if call == 'lent':
            A('mov.l =0x06009100,r1')
        else:
            A('mov.l =0x06009180,r1')
        A('jsr @r1'); A('nop')
    A('mov #0,r0')
    for i in range(rnd.randint(1, 3)):
        w = rnd.choice([1, 1, 2, 4, 4])
        off = rnd.randrange(0, 16, w)
        via = rnd.choice(['ptr', 'ptr', 'direct'])
        addr('r1', off, via)
        A('mov.%s @r1,r3' % {1:'b',2:'w',4:'l'}[w])
        if w == 1: A('extu.b r3,r3')
        if w == 2: A('extu.w r3,r3')
        A(rnd.choice(['add', 'xor', 'add']) + ' r3,r0')
    A('add #%d,r15' % F)
    A('.word 0x4F26')
    A('rts'); A('nop')
    out.append('name F15.%d.%d L%d N%d %s fill=%s s=%d' % (seed, k, L, N, call, fillmode, sOff))
    if call == 'lent': out.append('lend 06009100')
    out.append('asm 06009000'); out.extend(asm); out.append('endasm')
    if call == 'lent':
        out.append('asm 06009100'); out.append('mov.l @r4,r0'); out.append('rts'); out.append('nop'); out.append('endasm')
    if call == 'unlent':
        out.append('asm 06009180'); out.append('mov.l @(4,r4),r0'); out.append('rts'); out.append('nop'); out.append('endasm')
    out.append('args 0x11223344 0x55 0x6677 0x80000001'.replace('0x', ''))
    out.append('args 7fffffff 80 1 ffffffff')
    out.append('end'); out.append('')
print('\n'.join(out))
