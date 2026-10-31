#!/usr/bin/env python3
"""frame16.py SEED COUNT > file.src : frame attack fixtures beyond frame15.py (area 3 round 16).

What frame15 drew: one struct S behind 1 to 3 pointer slots, stores at legal offsets, one call. What this draws more:

  chain   4 to 6 pointer slots (the FrameEscape walk past four LOADs of frame slots), mixed widths, odd absolute offsets
  global  the address of S (or of a slot, or of a save slot) published in a global and read back (A3-141), alone or behind slots, with a
          callee that reads or writes through the global's pointer
  save    pushes of r8..r14 (the callee-saved registers' entry values) with a pointer chain aimed at a push slot: the slot is overwritten
          through the alias, the epilogue pops it, the function returns the register (the lent `preserved` mask is checked against the run)
  inter   pushes interleaved with frame stores and a second allocation: the frame is built in several steps
  r14     an -O0 frame: r14 = r15 after the allocation, every local through r14, the epilogue `mov r14,r15`
  loop    a loop-carried pointer: reloaded from its slot each trip, stored through, stepped, written back
  index   a store through a reloaded pointer plus a non-constant index (`mov.x rN,@(r0,rP)`), the index masked into S
  spill   -O0 spills of the pointer parameters (r4 stored to a slot, reloaded, stored through) beside a frame pointer slot, joined by a branch
  out     a pointer that is a callee's output (r0 = r4 + k) kept in a slot, reloaded and stored through
  multi   several narrow readers of one wide frame word (A3-139/A3-140's class: byte and halfword pieces combined by xor/sub/and/or)

Every fixture is a function at 06009000 (and its callees at 0600A000/0600A080); the driver prints PROTO (preserved mask), the text, LEDGER and the
ORACLE of each run; args lines perturb r8..r14 so a hidden register that the text reads shows. Env: F16_MODE (one mode), F16_LEVELS, F16_NST,
F16_CALL (none, lent, unlent, writer, global), F16_NOCALL=1 (call-free), F16_PC=1 (also no hidden register read; the printed-C differential gives the in_rN and unaff_rN the emulator's entry values, so it does not need this)."""
import sys, random, os
seed = int(sys.argv[1]); count = int(sys.argv[2])
rnd = random.Random(seed)
G = 0x0600F000
MODES = ['chain', 'chain', 'global', 'save', 'save', 'inter', 'r14', 'loop', 'index', 'spill', 'out', 'multi']
SAVEREGS = ['r8', 'r9', 'r10', 'r11', 'r12', 'r13', 'r14']

def pick(env, lo, hi):
    v = os.environ.get(env)
    return int(v) if v else rnd.randint(lo, hi)

class Fn:
    def __init__(self):
        self.asm = []
        self.sp = 0                 # r15 relative to the origin (r15 after `sts.l pr,@-r15`)
        self.base = 'r15'           # the register locals are reached through
        self.basey = None           # y of r14 when it is the base
        self.lab = 0
    def A(self, s): self.asm.append(s)
    def newlab(self):
        self.lab += 1
        return 'L%d' % self.lab
    def disp(self, y):
        return y - (self.sp if self.base == 'r15' else self.basey)
    def addr(self, reg, y):
        """reg = address of frame coordinate y"""
        d = self.disp(y)
        self.A('mov %s,%s' % (self.base, reg))
        while d > 127: self.A('add #127,%s' % reg); d -= 127
        while d < -128: self.A('add #-128,%s' % reg); d += 128
        if d: self.A('add #%d,%s' % (d, reg))
    def lw(self, reg, y):        # reg = word at frame y (direct, @(disp,base) when it fits)
        d = self.disp(y)
        if 0 <= d <= 60 and d % 4 == 0: self.A('mov.l @(%d,%s),%s' % (d, self.base, reg))
        else:
            self.addr(reg, y); self.A('mov.l @%s,%s' % (reg, reg))
    def sw(self, reg, y, tmp='r1'):
        d = self.disp(y)
        if 0 <= d <= 60 and d % 4 == 0: self.A('mov.l %s,@(%d,%s)' % (reg, d, self.base))
        else:
            self.addr(tmp, y); self.A('mov.l %s,@%s' % (reg, tmp))

def build(k):
    mode = os.environ.get('F16_MODE') or rnd.choice(MODES)
    f = Fn(); A = f.A
    L = pick('F16_LEVELS', 4, 6) if mode == 'chain' else pick('F16_LEVELS', 1, 4)
    N = pick('F16_NST', 2, 22)
    call = os.environ.get('F16_CALL') or rnd.choice(['none', 'none', 'none', 'lent', 'unlent', 'writer', 'global'])
    if os.environ.get('F16_NOCALL') or mode in ('r14', 'loop', 'index', 'multi', 'inter') and not os.environ.get('F16_CALL') and rnd.random() < 0.7:
        call = 'none'
    if mode == 'global' and call == 'none' and rnd.random() < 0.5: call = 'global'
    if mode == 'out': call = 'out'
    if call == 'writer' and (mode in ('save', 'inter')): call = 'lent'     # the writer writes S+8: the target must be S (checked below once the target is known)
    if os.environ.get('F16_NOCALL'): call = 'none'
    # ---- the frame: pushes P1, allocation A1 holding S and the slots, optional pushes P2 and allocation A2
    npush1 = rnd.randint(0, 4) if mode in ('save', 'inter', 'r14', 'spill') else rnd.choice([0, 0, 1, 2])
    pool = SAVEREGS[:]
    rnd.shuffle(pool)
    push1 = pool[:npush1]
    if mode == 'r14' and 'r14' not in push1: push1 = ['r14'] + push1
    push2 = []
    if mode == 'inter':
        push2 = [r for r in pool[npush1:npush1 + rnd.randint(1, 3)] if r != 'r14']
    F = 64
    sOff = rnd.choice([0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 12, 13, 16, 20])      # S's offset in the allocation: odd and half-word offsets too
    slotBase = 32 if sOff + 16 <= 32 else 40
    if slotBase + 4 * 6 > F: F = 80
    assert slotBase + 4 * 6 <= F
    # y coordinates (the origin is r15 after the pr push): the pushes P1 first
    y = 0
    saveY = {}
    for r in push1:
        y -= 4; saveY[r] = y
    yA1 = y - F                      # the allocation's lowest byte
    yS = yA1 + sOff
    ySlot = [yA1 + slotBase + 4 * i for i in range(6)]
    y = yA1
    for r in push2:
        y -= 4; saveY[r] = y
    A('.word 0x4F22')                # sts.l pr,@-r15
    for r in push1:
        A('mov.l %s,@-r15' % r); f.sp -= 4
    A('add #-%d,r15' % F if F <= 128 else 'add #-128,r15'); f.sp -= F
    if F > 128: raise SystemExit('frame too large')
    if mode == 'r14':
        A('mov r15,r14'); f.base = 'r14'; f.basey = f.sp
    # ---- the pointer chain
    target = 'S'
    tgtY = yS
    PC = bool(os.environ.get('F16_PC'))     # the printed-C differential has no hidden registers: no save-slot target, no read of one
    if mode == 'save' and push1 and rnd.random() < 0.8 and not PC:
        target = 'save'; tgtY = saveY[rnd.choice(push1)]
    if mode == 'save' and push1 and rnd.random() < 0.4:
        target = 'S'; tgtY = yS
    if mode == 'inter' and (push1 or push2) and rnd.random() < 0.4 and not PC:
        target = 'save'; tgtY = saveY[rnd.choice(push1 + push2)]
    viaG = (mode == 'global') or (mode == 'chain' and rnd.random() < 0.25) or call == 'global'
    gAt = rnd.choice(['pre', 'mid', 'post', 'slot'])
    def publish_global(yaddr, reg='r1'):
        f.addr(reg, yaddr); A('mov.l =0x%08X,r2' % G); A('mov.l %s,@r2' % reg)
    def init_chain():
        f.addr('r1', tgtY); f.sw('r1', ySlot[0])
        for i in range(1, L):
            f.addr('r1', ySlot[i - 1]); f.sw('r1', ySlot[i])
        if viaG:
            # the address the chain ends in is published in a global and read back through it
            if gAt in ('pre', 'mid'):
                f.addr('r1', tgtY); A('mov.l =0x%08X,r2' % G); A('mov.l r1,@r2')
            else:
                f.addr('r1', tgtY); A('mov.l =0x%08X,r2' % G); A('mov.l r1,@r2')
                A('mov.l @r2,r1'); f.sw('r1', ySlot[0])          # ...and the slot holds what the global gave back
    def reload(reg):
        if viaG and gAt in ('pre', 'mid', 'post') and rnd.random() < 0.6:
            A('mov.l =0x%08X,%s' % (G, reg)); A('mov.l @%s,%s' % (reg, reg))
            return
        f.lw(reg, ySlot[L - 1])
        for i in range(L - 1):
            A('mov.l @%s,%s' % (reg, reg))
    def paddr(reg, off, via):
        if via == 'ptr':
            reload(reg)
        else:
            f.addr(reg, tgtY)
        if off:
            A('add #%d,%s' % (off, reg))
    SZ = {1: 'b', 2: 'w', 4: 'l'}
    wide = 16 if target == 'S' else 4
    def legal(w):
        # an offset into the target's 16 (or 4) bytes, aligned for the width in absolute terms (the frame is word aligned)
        while True:
            off = rnd.randrange(0, wide - w + 1)
            if (tgtY + off) % w == 0: return off
    def store(w, off, src, via):
        paddr('r1', off, via)
        A('mov.%s %s,@r1' % (SZ[w], src))
    srcs = ['r4', 'r5', 'r6', 'r7']
    if (mode in ('save', 'inter', 'r14', 'spill') or rnd.random() < 0.3) and not PC: srcs += SAVEREGS[:4] * 2
    def src_pick():
        if rnd.random() < 0.15:
            A('mov #%d,r2' % rnd.randint(-120, 120)); return 'r2'
        return rnd.choice(srcs)
    # ---- the body
    init_chain()
    # S is filled first (most fixtures): a read of bytes nothing wrote is no oracle for a call-free fixture (the harness's frame holds other garbage)
    if target == 'S' and rnd.random() < 0.85:
        fm = rnd.choice(['words', 'words', 'bytes', 'halves']) if tgtY % 4 == 0 else (rnd.choice(['halves', 'bytes']) if tgtY % 2 == 0 else 'bytes')
        for o in rnd.sample(range(0, 16, 4 if fm == 'words' else (2 if fm == 'halves' else 1)), 4 if fm == 'words' else (8 if fm == 'halves' else 16)):
            if fm == 'words': A('mov #%d,r2' % rnd.randint(-100, 100)); store(4, o, 'r2' if rnd.random() < 0.5 else rnd.choice(['r4', 'r5', 'r6', 'r7']), rnd.choice(['ptr', 'direct']))
            elif fm == 'halves': A('mov #%d,r2' % rnd.randint(-100, 100)); store(2, o, 'r2', rnd.choice(['ptr', 'direct']))
            else: A('mov #%d,r2' % rnd.randint(-100, 100)); store(1, o, 'r2', rnd.choice(['ptr', 'direct']))
    if mode == 'spill':
        # -O0: r4 (a pointer parameter) spilled to a slot, reloaded and stored through; the frame pointer slot beside it
        f.sw('r4', ySlot[5]); f.sw('r5', ySlot[4])
        lb1, lb2 = f.newlab(), f.newlab()
        A('tst r6,r6'); A('bt %s' % lb1)
        f.addr('r1', yS); f.sw('r1', ySlot[3]); A('bra %s' % lb2); A('nop')
        A('%s:' % lb1); f.lw('r1', ySlot[5]); f.sw('r1', ySlot[3])      # the slot holds &S or the parameter r4
        A('%s:' % lb2)
        f.lw('r1', ySlot[3]); A('mov.l r5,@r1')       # store through the joined pointer (r4 may be anything: keep the run's r4 a RAM address below)
    if mode == 'loop':
        n = rnd.randint(2, 4); w = rnd.choice([1, 2, 4])
        if (tgtY) % w: w = 1
        step = rnd.choice([w, w, 2 * w]) if w > 1 else rnd.choice([1, 2, 3, 4])
        if step * (n - 1) + w > 16: step = w
        lp = f.newlab()
        A('mov #%d,r3' % n)
        A('%s:' % lp)
        reload('r1'); A('mov.%s r5,@r1' % SZ[w]); A('add #%d,r1' % step)
        f.sw('r1', ySlot[0])                          # the pointer stepped and written back to the innermost slot
        A('add r6,r5'); A('dt r3'); A('bf %s' % lp)
        f.addr('r1', tgtY); f.sw('r1', ySlot[0])      # the pointer is &S again after the loop
    p2idx = rnd.randint(0, N - 1)
    for i in range(N):
        w = rnd.choice([1, 1, 1, 2, 2, 4, 4])
        off = legal(w)
        via = rnd.choice(['ptr', 'ptr', 'ptr', 'direct'])
        if mode == 'index' and rnd.random() < 0.4 and target == 'S' and tgtY % 4 == 0:
            m = {1: 15, 2: 14, 4: 12}[w]
            reload('r1')
            A('mov %s,r0' % rnd.choice(['r4', 'r5', 'r6', 'r7'])); A('and #%d,r0' % m)
            A('mov.%s %s,@(r0,r1)' % (SZ[w], src_pick()))
            continue
        if mode == 'inter' and push2 and i == p2idx and not getattr(f, 'p2done', False):
            f.p2done = True
            for r in push2:
                A('mov.l %s,@-r15' % r); f.sp -= 4
        store(w, off, src_pick(), via)
    if mode == 'inter' and push2 and not getattr(f, 'p2done', False):
        for r in push2:
            A('mov.l %s,@-r15' % r); f.sp -= 4
        f.p2done = True
    # a callee-saved register used as a scratch (its entry value is saved above): written, then stored
    cand = [r for r in push1 + push2 if not (mode == 'r14' and r == 'r14')]
    if rnd.random() < 0.3 and cand and target == 'S' and tgtY % 4 == 0 and not PC:
        r = rnd.choice(cand)
        A('mov r5,%s' % r); store(4, rnd.randrange(0, 13, 4), r, 'ptr')
    # ---- the call
    if call in ('lent', 'unlent', 'writer', 'global', 'out') and tgtY % 4:
        call = 'none'                 # the callees read and write words at the pointer: it must be word aligned
    if call in ('lent', 'unlent', 'writer', 'global', 'out'):
        if call == 'global':
            A('mov.l =0x%08X,r1' % G); A('mov.l @r1,r4')
        else:
            reload('r4')
        if call == 'writer' and target != 'S': call = 'lent'
        callee = {'lent': 0x0600A000, 'writer': 0x0600A000, 'global': 0x0600A000, 'unlent': 0x0600A080, 'out': 0x0600A000}[call]
        A('mov.l =0x%08X,r1' % callee); A('jsr @r1'); A('nop')
        if call == 'out':
            # r0 = the callee's output: a pointer into the frame (r4 + 4 or r4 itself) kept in a slot, reloaded, stored through
            f.sw('r0', ySlot[2]); f.lw('r1', ySlot[2]); A('mov.l r5,@r1')
            f.lw('r1', ySlot[2]); A('mov.b r6,@r1')
    # ---- the reads
    A('mov #0,r0')
    nread = rnd.randint(1, 3)
    for i in range(nread):
        w = rnd.choice([1, 2, 4, 4])
        off = legal(w) if target == 'S' else 0
        via = rnd.choice(['ptr', 'direct', 'direct'])
        if mode == 'multi' and target == 'S':
            # several narrow readers of one wide word, combined
            wo = rnd.randrange(0, 13, 4) if tgtY % 4 == 0 else 0
            pieces = rnd.sample([(1, 0), (1, 1), (1, 2), (1, 3), (2, 0), (2, 2), (4, 0)], rnd.randint(2, 4))
            A('mov #0,r3')
            for (pw, po) in pieces:
                if (tgtY + wo + po) % pw: continue
                paddr('r1', wo + po, rnd.choice(['direct', 'ptr']))
                A('mov.%s @r1,r2' % SZ[pw])
                if pw == 1 and rnd.random() < 0.5: A('extu.b r2,r2')
                if pw == 2 and rnd.random() < 0.5: A('extu.w r2,r2')
                if rnd.random() < 0.3: A(rnd.choice(['shll8 r2', 'shll2 r2', 'shlr8 r2', 'shll16 r2']))
                A(rnd.choice(['xor', 'xor', 'sub', 'add', 'and', 'or']) + ' r2,r3')
            A(rnd.choice(['add', 'xor']) + ' r3,r0')
            continue
        paddr('r1', off, via)
        A('mov.%s @r1,r3' % SZ[w])
        if w == 1 and rnd.random() < 0.5: A('extu.b r3,r3')
        if w == 2 and rnd.random() < 0.5: A('extu.w r3,r3')
        A(rnd.choice(['add', 'xor', 'add', 'sub']) + ' r3,r0')
    # ---- the epilogue
    if mode == 'r14':
        A('mov r14,r15'); f.sp = f.basey
    else:
        for r in reversed(push2):
            pass
    for r in reversed(push2):
        A('mov.l @r15+,%s' % r); f.sp += 4
    if mode == 'r14':
        pass
    if f.sp != -len(push1) * 4 - F:
        # only the pushes of P2 were popped; A1 is still held (r14 mode restored r15 above)
        pass
    if mode != 'r14' or True:
        cur = f.sp
        want = -len(push1) * 4
        A('add #%d,r15' % (want - cur)) if want - cur else None
        f.sp = want
    for r in reversed(push1):
        A('mov.l @r15+,%s' % r); f.sp += 4
    # the register the function "returns" besides r0: a restored or a hidden callee-saved one
    if rnd.random() < 0.5 and not PC:
        r = rnd.choice(push1 + push2 + ['r12', 'r13'])
        A(rnd.choice(['add', 'xor']) + ' %s,r0' % r)
    A('.word 0x4F26')                # lds.l @r15+,pr
    A('rts'); A('nop')
    return mode, L, N, call, target, viaG, sOff, push1, push2, f.asm

def main():
    out = []
    for k in range(count):
        LAB = None
        mode, L, N, call, target, viaG, sOff, push1, push2, asm = build(k)
        nm = 'F16.%d.%d %s L%d N%d %s tgt=%s g=%d s=%d p=%s/%s' % (seed, k, mode, L, N, call, target, int(viaG), sOff, ''.join(r[1:] + ',' for r in push1), ''.join(r[1:] + ',' for r in push2))
        out.append('name ' + nm)
        if call in ('lent', 'writer', 'global', 'out'): out.append('lend 0600A000')
        out.append('asm 06009000'); out.extend(asm); out.append('endasm')
        if call in ('lent', 'global'):
            out.append('asm 0600A000')
            if call == 'global': out.extend(['mov.l =0x%08X,r1' % G, 'mov.l @r1,r1', 'mov.l @(4,r1),r0', 'rts', 'nop'])
            else: out.extend(['mov.l @r4,r0', 'rts', 'nop'])
            out.append('endasm')
        if call == 'writer':
            out.append('asm 0600A000'); out.extend(['mov.l r5,@(8,r4)', 'mov.l @(4,r4),r0', 'rts', 'nop']); out.append('endasm')
        if call == 'out':
            out.append('asm 0600A000'); out.extend(['mov.l @r4,r0', 'mov r4,r0', 'add #4,r0', 'rts', 'nop']); out.append('endasm')
        if call == 'unlent':
            out.append('asm 0600A080'); out.extend(['mov.l @(4,r4),r0', 'rts', 'nop']); out.append('endasm')
        hid = ' r8=5a5a5a5a r9=a5a5a5a5 r10=3c3c3c3c r11=c3c3c3c3 r12=0f0f0f0f r13=f0f0f0f0 r14=12345678'
        a0 = '0600e000' if mode == 'spill' else '11223344'
        out.append('args %s 55 6677 80000001' % a0)
        out.append('args 7fffffff 80 1 ffffffff' + hid)
        out.append('args 0600e000 81 02 0600e100' + hid.replace('5a5a5a5a', '77777777'))
        out.append('args 0600e000 81 0 0600e100')
        out.append('end'); out.append('')
    print('\n'.join(out))

main()
