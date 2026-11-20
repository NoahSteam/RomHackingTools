#!/usr/bin/env python3
"""Area 3 round 41: writes fixtures.src, the fixtures shaped like what an SH-2 C compiler emits (GCC and Hitachi SHC idioms), not probes of one mechanism.

usage: mk.py [COUNT [SEED]]      (default 400 generated modules from seed 1; the hand-shaped group `c*` is always written)

Two groups:
  c*  the compiled shapes by name: argument passing (r4-r7, stack words beyond four), a 64-bit and a struct return (r0:r1, the hidden pointer in r2),
      varargs (r4-r7 pushed, a va_list walk), callee-saved r8-r14 pushed and popped by the callee, PR saved, MACH/MACL, GBR, leaf functions,
      tail calls (jmp with the PR restore in its delay slot, bra), thunks, recursion, mutual recursion, a callee reached through a function-pointer
      table, a return value ignored, r0 on some paths only.
  g*  generated modules: 2 to 5 functions drawn from those shapes (a small compiler-shaped emitter: GCC frame layout, calls through the literal
      pool, results used, ignored or passed on), the call graph a DAG except for the bounded recursions. Same seed, same file.
Every function obeys the convention (r8-r15 and PR restored; GBR, VBR, MACH and MACL as declared in `env SE_GENERATED_CLOBBERS`), so a
finding is never the generator's own (`n:generator:*` lines would say so).
"""
import random, re, sys

PR_PUSH, PR_POP = '.word 0x4F22', '.word 0x4F26'        # sts.l pr,@-r15 / lds.l @r15+,pr


class Fn:
    def __init__(self, name):
        self.name, self.lines, self.n = name, [], 0
        self.ret = 0             # RETURNS intent: 1 r0 on every path, 0 not, 2 on one path only
        self.clob = 0            # SE_GENERATED_CLOBBERS mask: bit r for r8-r15, 17 gbr, 18 vbr, 19 mach, 20 macl

    def lab(self):
        self.n += 1
        return '%s_%d' % (self.name, self.n)

    def a(self, *ls):
        self.lines += ls


def frame(f, saves, pr, local, fp=False):
    """GCC's prologue: callee-saved registers high to low, PR, the locals; r14 as the frame pointer when asked. Returns the offset of the first
    stack argument from r15 after it."""
    saves = sorted(saves, reverse=True)
    if fp and 14 not in saves: saves = [14] + saves
    for r in saves: f.a('mov.l r%d,@-r15' % r)
    if pr: f.a(PR_PUSH)
    if local: f.a('add #-%d,r15' % local)
    if fp: f.a('mov r15,r14')
    return 4 * (len(saves) + (1 if pr else 0)) + local, saves


def unframe(f, saves, pr, local, fp=False, tail=None):
    """The epilogue: locals, PR, the pops with the last one in the delay slot of the return (or of the tail jump: `tail` = the register to jump through)."""
    if fp: f.a('add #%d,r14' % local, 'mov r14,r15') if local else f.a('mov r14,r15')
    elif local: f.a('add #%d,r15' % local)
    if pr: f.a(PR_POP)
    pops = ['mov.l @r15+,r%d' % r for r in sorted(saves)]
    last = pops.pop() if pops else 'nop'
    f.a(*pops)
    f.a(('jmp @r%d' % tail) if tail is not None else 'rts', last)


def pool_call(f, tgt, reg=1, tail=False):
    f.a('mov.l =@%s,r%d' % (tgt, reg), 'jsr @r%d' % reg, 'nop')


def leaf(rng, f, nargs=2):
    """A leaf: no frame, r0 from the arguments (what `int f(int a, int b)` is at -O2); sometimes a few pushes of r8-r10 for a wide body."""
    ops = ['add r%d,r0', 'sub r%d,r0', 'and r%d,r0', 'or r%d,r0', 'xor r%d,r0', 'mul.l r%d,r0']
    f.ret = 1
    args = [4 + i for i in range(nargs)]
    f.a('mov r%d,r0' % args[0])
    for r in args[1:]:
        op = rng.choice(ops)
        if op.startswith('mul'):
            f.a('mul.l r%d,r0' % r, 'sts macl,r0'); f.clob |= 1 << 20
        else:
            f.a(op % r)
    if rng.random() < .3: f.a(rng.choice(['shll2 r0', 'shlr2 r0', 'extu.b r0,r0', 'exts.w r0,r0', 'neg r0,r0', 'not r0,r0']))
    f.a('rts', 'nop')


def leaf_cond(rng, f):
    """`if (a) return b + c; return d;` and friends: which argument registers are read depends on the path, r0 written on one path or both."""
    f.ret = rng.choice([1, 2, 1])
    f.nreg = rng.choice([3, 4])
    L, E = f.lab(), f.lab()
    f.a('tst r4,r4', 'bt %s' % L, 'mov r5,r0', 'add r6,r0')
    if f.nreg == 4 and rng.random() < .5: f.a('add r7,r0')
    f.a('bra %s' % E, 'nop', '%s:' % L)
    if f.ret == 1: f.a('mov %s,r0' % rng.choice(['r6', 'r7' if f.nreg == 4 else 'r6']))
    else: f.a('nop')
    f.a('%s:' % E, 'rts', 'nop')


def leaf_wide(rng, f):
    """A leaf that borrows r8-r10 for its temporaries: pushes, the work, pops with the last in the rts delay slot."""
    f.ret = 1
    k = rng.choice([1, 2, 3]); saves = [8, 9, 10][:k]
    for r in sorted(saves, reverse=True): f.a('mov.l r%d,@-r15' % r)
    f.a('mov r4,r8', 'add r5,r8')
    if k > 1: f.a('mov r8,r9', 'shll2 r9', 'add r9,r8')
    if k > 2: f.a('mov r6,r10', 'xor r8,r10', 'add r10,r8')
    f.a('mov r8,r0')
    for r in sorted(saves)[:-1]: f.a('mov.l @r15+,r%d' % r)
    f.a('rts', 'mov.l @r15+,r%d' % sorted(saves)[-1])


def leaf_mem(rng, f):
    """A leaf over a pointer argument: struct field loads, a byte read, a store through r4 (`p->b = p->a + x`)."""
    f.ret = rng.choice([1, 1, 0])
    f.a('mov.l @r4,r1', 'mov.l @(4,r4),r2', 'add r2,r1', 'add r5,r1')
    w = rng.choice(['mov.l r1,@(8,r4)', 'mov.l r1,@r4', 'mov.w r1,@r4' if False else 'mov.l r1,@(12,r4)'])
    f.a(w)
    if f.ret: f.a('mov r1,r0')
    f.a('rts', 'nop')


def leaf_u8(rng, f):
    """A byte and word table lookup: sign and zero extension of the loaded value, an index in r5 (`tab[i]` with i scaled by the compiler)."""
    f.ret = 1
    f.a('mov #7,r0', 'and r5,r0', rng.choice(['shll r0', 'shll2 r0']), 'mov.l @(r0,r4),r0', rng.choice(['extu.b r0,r0', 'exts.w r0,r0', 'extu.w r0,r0']), 'rts', 'nop')


def leaf_gbr(rng, f):
    """SHC's #pragma gbr_base: a read-modify-write of a global through GBR; GBR is an input (never written)."""
    f.ret = rng.choice([1, 0])
    f.a('mov.l @(%d,gbr),r0' % rng.choice([0, 4, 8]), 'add r4,r0', 'mov.l r0,@(%d,gbr)' % rng.choice([0, 4, 8, 12]))
    f.a('rts', 'nop')


def leaf_u64(rng, f):
    """A 64-bit function: arguments in r4:r5 and r6:r7 (high word first), the result in r0:r1 (high word in r0)."""
    f.ret = 1
    kind = rng.choice(['add', 'sub', 'mul', 'shl'])
    if kind == 'add': f.a('clrt', 'addc r7,r5', 'addc r6,r4', 'mov r4,r0', 'rts', 'mov r5,r1')
    elif kind == 'sub': f.a('clrt', 'subc r7,r5', 'subc r6,r4', 'mov r4,r0', 'rts', 'mov r5,r1')
    elif kind == 'mul':
        f.clob |= (1 << 19) | (1 << 20)
        f.a('dmulu.l r5,r7', 'mul.l r5,r6', 'sts macl,r1', 'mul.l r4,r7', 'sts macl,r2', 'sts mach,r0', 'add r1,r0', 'add r2,r0', 'sts macl,r1', 'rts', 'nop')
    else: f.a('mov r5,r0', 'shll8 r0', 'mov r4,r1', 'shlr8 r1', 'or r1,r0', 'mov r5,r1', 'rts', 'shll8 r1')


def leaf_struct(rng, f):
    """A struct return: the hidden pointer arrives in r2 (SH GCC and SHC), the callee fills it; sometimes it returns the pointer too."""
    f.ret = rng.choice([1, 0])
    f.a('mov.l r4,@r2', 'mov.l r5,@(4,r2)')
    if rng.random() < .5: f.a('mov r4,r1', 'add r5,r1', 'mov.l r1,@(8,r2)')
    if f.ret: f.a('mov r2,r0')
    f.a('rts', 'nop')


def varargs(rng, f):
    """`int sum(int n, ...)`: r4-r7 are pushed so the register arguments lie below the stack arguments, a va_list walks them."""
    f.ret = 1
    f.a('mov.l r7,@-r15', 'mov.l r6,@-r15', 'mov.l r5,@-r15', 'mov.l r4,@-r15')       # r4 lowest: the va_list is r15 at entry minus 16
    f.a('mov.l @r15,r1', 'mov #3,r2', 'and r2,r1', 'add #1,r1', 'mov r15,r2', 'add #4,r2', 'mov #0,r0')
    L = f.lab()
    f.a('%s:' % L, 'mov.l @r2+,r3', 'add r3,r0', 'dt r1', 'bf %s' % L)
    f.a('add #16,r15', 'rts', 'nop')


def mid(rng, f, callees, nregargs, nstack, saves_hint=None):
    """A function that calls: keeps its arguments in r8 up (survive the calls), builds the callee's arguments, uses or drops the result."""
    f.ret = rng.choice([1, 1, 1, 2, 0])
    keep = [8 + i for i in range(min(nregargs, 3))]
    fp = rng.random() < .35
    nout = max([getattr(c, 'nstack', 0) for c in callees] + [0])
    wantloop = bool(keep) and rng.random() < .3
    ctr = 8 + len(keep)
    saves = list(keep) + ([ctr] if wantloop else [])
    structs = any(getattr(c, 'struct', False) for c in callees)
    local = 4 * nout + (12 if structs else 0) + 4 * rng.choice([0, 0, 1, 2])
    off, saves = frame(f, saves, True, local, fp)
    if local and local < 64:
        f.a('mov #0,r1')
        for q in range(0, local, 4): f.a('mov.l r1,@(%d,r15)' % q)      # a local is read only after the code wrote it
    for i, r in enumerate(keep): f.a('mov r%d,r%d' % (4 + i, r))
    haveres = None
    loopdone = False
    for k, c in enumerate(callees):
        guard = None
        if rng.random() < .35:                       # `if (a) f(...)`: the call on one path
            guard = f.lab(); f.a('tst r%d,r%d' % ((keep[0],) * 2 if keep else (4, 4)), 'bt %s' % guard)
        loop = None
        if wantloop and not loopdone and rng.random() < .6:   # `for (i = 0; i < (a & 3) + 1; i++)`: a counter in a callee-saved register
            loopdone = True
            loop = f.lab(); f.a('mov r%d,r%d' % (keep[0], ctr), 'mov #3,r1', 'and r1,r%d' % ctr, 'add #1,r%d' % ctr, '%s:' % loop)
        n = getattr(c, 'nreg', 2)
        for i in range(n):
            src = rng.choice(['keep', 'const', 'prev', 'glob', 'local'])
            if i in getattr(c, 'ptr', ()): src = rng.choice(['ptr', 'ptr', 'keep']) if keep else 'ptr'
            if src == 'ptr': f.a('mov.l =0x0603%04X,r%d' % (rng.choice([0x40, 0x80]), 4 + i))
            elif src == 'keep' and keep: f.a('mov r%d,r%d' % (keep[i % len(keep)] if rng.random() < .8 else keep[0], 4 + i))
            elif src == 'prev' and haveres: f.a('mov r%d,r%d' % (haveres, 4 + i))
            elif src == 'glob': f.a('mov.l =0x0603%04X,r%d' % (rng.choice([0, 4, 8]), 4 + i), 'mov.l @r%d,r%d' % (4 + i, 4 + i))
            elif src == 'local' and local >= 4: f.a('mov.l @(%d,r15),r%d' % (min(4 * rng.randrange(0, local // 4), 60), 4 + i))
            else: f.a('mov #%d,r%d' % (rng.choice([0, 1, 5, 7, -1]), 4 + i))
        if getattr(c, 'struct', False): f.a('mov r15,r2', 'add #%d,r2' % (4 * nout))
        for j in range(getattr(c, 'nstack', 0)):         # stack arguments beyond four: stored into the outgoing area at the bottom of the frame
            f.a('mov.l =0x%x,r1' % rng.choice([0x11, 0x2222, 0x6030010]) if rng.random() < .5 else 'mov r%d,r1' % (keep[0] if keep else 4), 'mov.l r1,@(%d,r15)' % (4 * j))
        pool_call(f, c.name, 1)
        if loop: f.a('dt r%d' % ctr, 'bf %s' % loop)
        if guard: f.a('%s:' % guard)
        use = rng.choice(['keep', 'drop', 'store', 'add'])
        if use == 'store' and c.ret: f.a('mov.l =0x06030010,r1', 'mov.l r0,@r1')
        elif use == 'add' and keep and c.ret: f.a('add r0,r%d' % keep[0])
        elif use == 'keep' and c.ret and keep: f.a('mov r0,r%d' % keep[-1]); haveres = keep[-1]
    if f.ret:
        if f.ret == 2:                               # r0 on one path: `if (x) return x; g();` falls out with whatever r0 holds
            E = f.lab(); f.a('mov r%d,r0' % (keep[0] if keep else 4), 'tst r0,r0', 'bt %s' % E, 'mov #%d,r0' % rng.choice([1, 2]), '%s:' % E)
        elif keep and rng.random() < .6: f.a('mov r%d,r0' % rng.choice(keep))
        else: f.a('add #%d,r0' % rng.choice([1, 2, 4]))
    unframe(f, saves, True, local, fp)


def tailer(rng, f, tgt, via_bra=False):
    """A tail call, the epilogue in front of it: GCC restores PR in the delay slot of the jmp; a leaf-like thunk adjusts `this` and jumps."""
    f.ret = 1
    k = rng.choice([0, 1, 2])
    if k == 0: f.a('mov.l =@%s,r1' % tgt, 'jmp @r1', 'nop')                                  # a thunk
    elif k == 1: f.a('add #%d,r4' % rng.choice([4, 8, 16]), 'mov.l =@%s,r1' % tgt, 'jmp @r1', 'nop')   # a this-adjusting thunk
    else:
        f.a('mov.l r8,@-r15', PR_PUSH, 'mov r4,r8', 'add r5,r8', 'mov r8,r4', 'mov.l @r15+,r8' if False else 'nop')
        f.a(PR_POP, 'mov.l @r15+,r8', 'mov.l =@%s,r1' % tgt, 'jmp @r1', 'nop')            # the callee-saved restored, then jump: r8 is the caller's


def recur(rng, f, other=None):
    """A bounded recursion: f(n) = n + f(n-1) with n masked to 0..3 (to the other function of a mutual pair when given)."""
    f.ret = 1
    base = f.lab()
    off, saves = frame(f, [8], True, 0)
    f.a('mov #3,r1', 'and r1,r4', 'mov r4,r8', 'mov #0,r0', 'tst r4,r4', 'bt %s' % base, 'add #-1,r4')
    pool_call(f, other or f.name)
    f.a('add r8,r0', '%s:' % base)
    unframe(f, saves, True, 0)


def dispatch(rng, f, c1, c2):
    """`tab[i & 1](x)`: a table of two function addresses after the code, the callee loaded from it and called through the register."""
    f.ret = 1
    off, saves = frame(f, [8], True, 0)
    f.a('mov r5,r8', 'mov #1,r1', 'and r1,r4', 'mov r4,r0', 'shll2 r0', 'mov.l =@T,r1', 'mov.l @(r0,r1),r1', 'mov r8,r4', 'jsr @r1', 'nop', 'add r8,r0')
    unframe(f, saves, True, 0)
    f.a('.align4', 'T:', '.addr %s' % c1, '.addr %s' % c2)


def write(out, name, fns, note, env=None):
    out.append('# %s: %s' % (name, note))
    out.append('name %s' % name)
    out.append('ret %s' % ','.join(str(f.ret) for f in fns))
    out.append('env SE_GENERATED_CLOBBERS=%s%s' % (','.join('%x' % f.clob for f in fns), (' ' + env) if env else ''))
    for i, f in enumerate(fns):
        if i: out.append('fn')
        ls = list(f.lines)
        for d in [l[:-1] for l in ls if l.endswith(':') and ' ' not in l]:
            ls = [re.sub(r'(?<!\w)%s(?!\w)' % re.escape(d), '%s_%s' % (f.name, d), l) for l in ls]
        out.append('%s:' % f.name)
        out += ls
    return out


def mod(rng, name, out):
    n = rng.randint(2, 5)
    fns = []
    kinds = ['leaf', 'cond', 'cond', 'wide', 'mem', 'u8', 'gbr', 'u64', 'struct', 'vararg', 'mid', 'mid', 'tail', 'recur']
    # build from the last function (a leaf) up so a function only calls ones after it
    made = []
    for i in range(n - 1, -1, -1):
        f = Fn('F%d' % i)
        callees = [c for c in made if rng.random() < .55][:3] or ([made[-1]] if made else [])
        k = 'mid' if i == 0 else rng.choice(kinds)
        if not made and k in ('mid', 'tail', 'disp'): k = rng.choice(['leaf', 'cond', 'wide', 'mem', 'u8', 'u64', 'struct', 'vararg', 'gbr'])
        if k in ('tail', 'disp') and not callees: k = 'leaf'
        f.nreg, f.nstack, f.struct, f.ptr = 2, 0, False, ()
        if k == 'leaf': f.nreg = rng.choice([1, 2, 3, 4]); leaf(rng, f, f.nreg)
        elif k == 'cond': leaf_cond(rng, f)
        elif k == 'wide': f.nreg = 3; leaf_wide(rng, f)
        elif k == 'mem': f.nreg = 2; f.ptr = (0,); leaf_mem(rng, f)
        elif k == 'u8': f.ptr = (0,); leaf_u8(rng, f)
        elif k == 'gbr': f.nreg = 1; leaf_gbr(rng, f)
        elif k == 'u64': f.nreg = 4; leaf_u64(rng, f)
        elif k == 'struct': f.struct = True; leaf_struct(rng, f)
        elif k == 'vararg': f.nreg = 4; f.nstack = rng.choice([0, 1, 2]); varargs(rng, f)
        elif k == 'tail': f.nreg = callees[0].nreg; tailer(rng, f, callees[0].name); f.ret = callees[0].ret; f.struct = callees[0].struct; f.nstack = callees[0].nstack; f.clob = callees[0].clob
        elif k == 'recur': f.nreg = 1; recur(rng, f)
        elif k == 'disp':
            f.nreg = 2; c2 = callees[-1]; dispatch(rng, f, callees[0].name, c2.name); f.clob = callees[0].clob | c2.clob
        else:
            f.nreg = rng.choice([1, 2, 3, 4]); mid(rng, f, callees, f.nreg, 0)
            f.nstack = 0
            for c in callees: f.clob |= c.clob
        made.insert(0, f)
    # a stack-argument callee: the last leaf takes two words past the registers when asked
    fns = made
    note = ' '.join('%s=%s' % (f.name, 'x') for f in fns)
    write(out, name, fns, 'generated module of %d functions' % n)


def named(out):
    P = lambda *ls: list(ls)

    def fx(name, note, specs, env=None):
        fns = []
        for nm, ret, clob, ls in specs:
            f = Fn(nm); f.ret, f.clob, f.lines = ret, clob, ls; fns.append(f)
        write(out, name, fns, note, env)

    fx('x1', 'the smallest module: selftest.sh runs it against a substituted generator binary', [('F0', 1, 0, P('mov r4,r0', 'rts', 'add r5,r0'))])
    # --- c1x: r4-r7 and stack arguments ---
    for nst in (1, 2, 3, 5):
        callee = P('mov.l @(0,r15),r1', 'mov r4,r0', 'add r5,r0', 'add r6,r0', 'add r7,r0', 'add r1,r0') + sum((['mov.l @(%d,r15),r2' % (4 * k), 'add r2,r0'] for k in range(1, nst)), []) + P('rts', 'nop')
        caller = P(PR_PUSH, 'add #-%d,r15' % (4 * nst), 'mov #1,r1', 'mov.l r1,@(0,r15)') + sum((['mov.l r%d,@(%d,r15)' % (4 + k % 4, 4 * k)] for k in range(1, nst)), []) + \
                 P('mov #4,r5', 'mov #5,r6', 'mov #6,r7', 'mov.l =@F1,r1', 'jsr @r1', 'nop', 'add #%d,r15' % (4 * nst), PR_POP, 'rts', 'nop')
        fx('c1_stack%d' % nst, '%d stack arguments beyond the four register ones, the callee reads r15+4k at entry' % nst, [('F0', 1, 0, caller), ('F1', 1, 0, callee)])
    # a callee that ignores an argument register it is handed, and one that uses r7 only
    fx('c1_unused', 'the callee reads r7 only: r4-r6 are handed and unread', [('F0', 1, 0, P(PR_PUSH, 'mov.l =@F1,r1', 'jsr @r1', 'nop', PR_POP, 'rts', 'nop')), ('F1', 1, 0, P('mov r7,r0', 'add #1,r0', 'rts', 'nop'))])
    fx('c1_r5only', 'the callee reads r5 only, called with r4 and r6 set', [('F0', 1, 0, P(PR_PUSH, 'mov #3,r4', 'mov #4,r6', 'mov.l =@F1,r1', 'jsr @r1', 'nop', PR_POP, 'rts', 'nop')), ('F1', 1, 0, P('mov r5,r0', 'shll r0', 'rts', 'nop'))])
    fx('c1_readlate', 'the callee writes r6 before reading it on one path, reads it first on the other', [
        ('F0', 1, 0, P(PR_PUSH, 'mov.l =@F1,r1', 'jsr @r1', 'nop', PR_POP, 'rts', 'nop')),
        ('F1', 1, 0, P('tst r4,r4', 'bt K1', 'mov #2,r6', 'K1:', 'mov r6,r0', 'rts', 'add r5,r0'))])
    # --- c2x: 64-bit and struct ---
    fx('c2_u64add', '64-bit add: r4:r5 + r6:r7 -> r0:r1, the caller stores both halves', [
        ('F0', 0, 0, P(PR_PUSH, 'mov.l =@F1,r2', 'jsr @r2', 'nop', 'mov.l =0x06030000,r2', 'mov.l r0,@r2', 'mov.l r1,@(4,r2)', PR_POP, 'rts', 'nop')),
        ('F1', 1, 0, P('clrt', 'addc r7,r5', 'addc r6,r4', 'mov r4,r0', 'rts', 'mov r5,r1'))])
    fx('c2_u64lowonly', 'the callee returns r0:r1, the caller uses the low word r1 only (and ignores r0)', [
        ('F0', 0, 0, P(PR_PUSH, 'mov.l =@F1,r2', 'jsr @r2', 'nop', 'mov r1,r4', 'mov.l =0x06030000,r2', 'mov.l r4,@r2', PR_POP, 'rts', 'nop')),
        ('F1', 1, 0, P('mov r5,r1', 'add r7,r1', 'mov r4,r0', 'rts', 'add r6,r0'))])
    fx('c2_u64mul', '64-bit multiply with dmulu.l: mach:macl, the low word in r1, the high in r0', [
        ('F0', 0, (1 << 19) | (1 << 20), P(PR_PUSH, 'mov.l =@F1,r2', 'jsr @r2', 'nop', 'mov.l =0x06030000,r2', 'mov.l r0,@r2', 'mov.l r1,@(4,r2)', PR_POP, 'rts', 'nop')),
        ('F1', 1, (1 << 19) | (1 << 20), P('dmulu.l r4,r5', 'sts macl,r1', 'rts', 'sts mach,r0'))])
    fx('c2_struct', 'a struct returned through the hidden pointer in r2; the caller reads the fields back', [
        ('F0', 1, 0, P(PR_PUSH, 'add #-12,r15', 'mov r15,r2', 'mov.l =@F1,r1', 'jsr @r1', 'nop', 'mov.l @(4,r15),r0', 'mov.l @r15,r1', 'add r1,r0', 'add #12,r15', PR_POP, 'rts', 'nop')),
        ('F1', 0, 0, P('mov.l r4,@r2', 'mov.l r5,@(4,r2)', 'rts', 'mov.l r6,@(8,r2)'))])
    fx('c2_structptr', 'the callee returns the hidden pointer in r0 as well; the caller returns the field through it', [
        ('F0', 1, 0, P(PR_PUSH, 'add #-8,r15', 'mov r15,r2', 'mov.l =@F1,r1', 'jsr @r1', 'nop', 'mov.l @(4,r0),r0', 'add #8,r15', PR_POP, 'rts', 'nop')),
        ('F1', 1, 0, P('mov.l r4,@r2', 'mov.l r5,@(4,r2)', 'rts', 'mov r2,r0'))])
    fx('c2_structarg', 'a struct passed by value in r4-r7 then the stack: the caller copies words, the callee sums them', [
        ('F0', 1, 0, P(PR_PUSH, 'add #-4,r15', 'mov #9,r1', 'mov.l r1,@r15', 'mov #1,r4', 'mov #2,r5', 'mov #3,r6', 'mov #4,r7', 'mov.l =@F1,r1', 'jsr @r1', 'nop', 'add #4,r15', PR_POP, 'rts', 'nop')),
        ('F1', 1, 0, P('mov r4,r0', 'add r5,r0', 'add r6,r0', 'add r7,r0', 'mov.l @r15,r1', 'rts', 'add r1,r0'))])
    # --- c3x: varargs ---
    for nst in (0, 2):
        va = P('mov.l r7,@-r15', 'mov.l r6,@-r15', 'mov.l r5,@-r15', 'mov.l r4,@-r15', 'mov r15,r2', 'add #4,r2', 'mov.l @r2+,r0', 'mov.l @r2+,r1', 'add r1,r0', 'mov.l @r2+,r1', 'add r1,r0', 'add #16,r15', 'rts', 'nop')
        callers = P(PR_PUSH, 'add #-%d,r15' % (4 * max(nst, 1)), 'mov #7,r1', 'mov.l r1,@r15', 'mov #1,r4', 'mov #2,r5', 'mov #3,r6', 'mov #4,r7', 'mov.l =@F1,r1', 'jsr @r1', 'nop', 'add #%d,r15' % (4 * max(nst, 1)), PR_POP, 'rts', 'nop')
        fx('c3_vararg%d' % nst, 'a varargs callee spilling r4-r7 and walking a va_list across the register words into the stack ones', [('F0', 1, 0, callers), ('F1', 1, 0, va)])
    fx('c3_printf', 'a varargs callee that reads r4 as the format and passes the va_list pointer (r15 after the spill) to a helper', [
        ('F0', 1, 0, P(PR_PUSH, 'mov #5,r5', 'mov #6,r6', 'mov #7,r7', 'mov.l =@F1,r1', 'jsr @r1', 'nop', PR_POP, 'rts', 'nop')),
        ('F1', 1, 0, P('mov.l r7,@-r15', 'mov.l r6,@-r15', 'mov.l r5,@-r15', PR_PUSH, 'mov r15,r5', 'add #4,r5', 'mov.l =@F2,r1', 'jsr @r1', 'nop', PR_POP, 'add #12,r15', 'rts', 'nop')),
        ('F2', 1, 0, P('mov.l @r5,r0', 'mov.l @(4,r5),r1', 'add r4,r0', 'rts', 'add r1,r0'))])
    # --- c4x: callee-saved and PR ---
    for k in (1, 3, 7):
        regs = list(range(14, 14 - k, -1))
        pro = P(*['mov.l r%d,@-r15' % r for r in regs]) + P(PR_PUSH)
        body = P(*sum((['mov r%d,r%d' % (4 + (i % 4), r)] for i, r in enumerate(regs)), []))
        call = P('mov.l =@F2,r1', 'jsr @r1', 'nop')
        use = P(*sum((['add r%d,r0' % r] for r in regs), []))
        epi = P(PR_POP) + P(*['mov.l @r15+,r%d' % r for r in sorted(regs)[:-1]]) + P('rts', 'mov.l @r15+,r%d' % max(regs))
        fx('c4_saves%d' % k, '%d callee-saved registers pushed by the callee, held across a call, popped, PR saved' % k, [
            ('F0', 1, 0, P(PR_PUSH, 'mov #3,r4', 'mov #4,r5', 'mov #5,r6', 'mov #6,r7', 'mov.l =@F1,r1', 'jsr @r1', 'nop', PR_POP, 'rts', 'nop')),
            ('F1', 1, 0, pro + body + call + use + epi), ('F2', 1, 0, P('mov r4,r0', 'rts', 'add r5,r0'))])
    fx('c4_fp', 'a frame with r14 as the frame pointer and 24 bytes of locals, an address-taken local handed to a callee', [
        ('F0', 1, 0, P('mov.l r14,@-r15', PR_PUSH, 'add #-24,r15', 'mov r15,r14', 'mov r14,r4', 'add #8,r4', 'mov #5,r5', 'mov.l =@F1,r1', 'jsr @r1', 'nop', 'mov.l @(8,r14),r0', 'add #24,r14', 'mov r14,r15', PR_POP, 'rts', 'mov.l @r15+,r14')),
        ('F1', 0, 0, P('mov.l r5,@r4', 'rts', 'nop'))])
    fx('c4_ignored_saved', 'the callee pushes r8, clobbers it, pops it: the caller keeps a value in r8 across the call', [
        ('F0', 1, 0, P('mov.l r8,@-r15', PR_PUSH, 'mov r4,r8', 'mov.l =@F1,r1', 'jsr @r1', 'nop', 'add r8,r0', PR_POP, 'rts', 'mov.l @r15+,r8')),
        ('F1', 1, 0, P('mov.l r8,@-r15', 'mov r4,r8', 'shll2 r8', 'mov r8,r0', 'rts', 'mov.l @r15+,r8'))])
    fx('c4_prleaf', 'a function that calls but never saves PR is not compiled code: PR saved in a register (mov, not a push) like SHC', [
        ('F0', 1, 0, P('mov.l r8,@-r15', 'sts pr,r8', 'mov.l =@F1,r1', 'jsr @r1', 'nop', 'lds r8,pr', 'rts', 'mov.l @r15+,r8')),
        ('F1', 1, 0, P('mov r4,r0', 'rts', 'add #3,r0'))])
    # --- c5x: MACH/MACL ---
    mac = (1 << 19) | (1 << 20)
    fx('c5_macl', 'the callee multiplies and returns macl; the caller multiplies, calls, multiplies again: no value kept in macl across the call', [
        ('F0', 1, mac, P(PR_PUSH, 'mul.l r4,r5', 'sts macl,r8' if False else 'sts macl,r1', 'mov.l r1,@-r15', 'mov.l =@F1,r2', 'jsr @r2', 'nop', 'mov.l @r15+,r1', 'add r1,r0', PR_POP, 'rts', 'nop')),
        ('F1', 1, mac, P('mul.l r4,r6', 'rts', 'sts macl,r0'))])
    fx('c5_macread', 'a callee that reads MACL it did not set (an input), called after the caller set it', [
        ('F0', 1, mac, P(PR_PUSH, 'mul.l r4,r5', 'mov.l =@F1,r2', 'jsr @r2', 'nop', PR_POP, 'rts', 'nop')),
        ('F1', 1, 0, P('sts macl,r0', 'rts', 'add r4,r0'))])
    fx('c5_mac_l', 'mac.l accumulate loop in the callee over a table, MACH:MACL result summed to r0', [
        ('F0', 1, mac, P(PR_PUSH, 'mov.l =0x06030000,r4', 'mov.l =0x06030020,r5', 'mov #3,r6', 'mov.l =@F1,r2', 'jsr @r2', 'nop', PR_POP, 'rts', 'nop')),
        ('F1', 1, mac, P('mov #3,r1', 'and r1,r6', 'add #1,r6', 'clrmac', 'L1:', '.word 0x445F', 'dt r6', 'bf L1', 'rts', 'sts macl,r0'))])
    # --- c6x: GBR ---
    fx('c6_gbrread', 'a chain of three functions that read GBR-relative globals; GBR is never written', [
        ('F0', 1, 0, P(PR_PUSH, 'mov.l =@F1,r1', 'jsr @r1', 'nop', 'mov r0,r4', 'mov.l =@F2,r1', 'jsr @r1', 'nop', PR_POP, 'rts', 'nop')),
        ('F1', 1, 0, P('mov.l @(4,gbr),r0', 'add r4,r0', 'rts', 'nop')),
        ('F2', 1, 0, P('mov.b @(8,gbr),r0', 'extu.b r0,r0', 'add r4,r0', 'rts', 'nop'))])
    fx('c6_gbrset', 'an init function that sets GBR from its argument (crt0 style), a later function reads GBR-relative', [
        ('F0', 1, 1 << 17, P(PR_PUSH, 'mov.l =@F1,r1', 'jsr @r1', 'nop', 'mov.l =@F2,r1', 'jsr @r1', 'nop', PR_POP, 'rts', 'nop')),
        ('F1', 0, 1 << 17, P('ldc r4,gbr', 'rts', 'nop')),
        ('F2', 1, 0, P('mov.l @(0,gbr),r0', 'rts', 'nop'))])
    fx('c6_gbrsave', 'a function that borrows GBR for a block and puts it back (stc, ldc), the caller reads GBR-relative after', [
        ('F0', 1, 0, P(PR_PUSH, 'mov.l =@F1,r1', 'jsr @r1', 'nop', 'mov.l @(4,gbr),r0', PR_POP, 'rts', 'nop')),
        ('F1', 0, 0, P('stc gbr,r1', 'mov.l r1,@-r15', 'ldc r4,gbr', 'mov.l @(0,gbr),r0', 'mov.l @r15+,r1', 'ldc r1,gbr', 'rts', 'nop'))])
    # --- c7x: leaves, tail calls, thunks ---
    fx('c7_tailjmp', 'GCC tail call: the epilogue first, PR restored in the delay slot of jmp @r1; the target returns to the caller of the tail-caller', [
        ('F0', 1, 0, P(PR_PUSH, 'mov #3,r5', 'mov.l =@F1,r1', 'jsr @r1', 'nop', 'add #1,r0', PR_POP, 'rts', 'nop')),
        ('F1', 1, 0, P('mov.l r8,@-r15', PR_PUSH, 'mov r4,r8', 'add r5,r8', 'mov r8,r4', PR_POP, 'mov.l =@F2,r1', 'jmp @r1', 'mov.l @r15+,r8')),
        ('F2', 1, 0, P('mov r4,r0', 'rts', 'shll r0'))])
    fx('c7_tailbra', 'a tail call by bra to a function placed after it (SHC), with a delay-slot argument move', [
        ('F0', 1, 0, P(PR_PUSH, 'mov #3,r4', 'mov.l =@F1,r1', 'jsr @r1', 'nop', PR_POP, 'rts', 'nop')),
        ('F1', 1, 0, P('bra F2', 'add #4,r4', 'nop')),
        ('F2', 1, 0, P('mov r4,r0', 'rts', 'add r4,r0'))])
    fx('c7_thunk2', 'a thunk to a thunk to the function; arguments r4-r7 pass through untouched', [
        ('F0', 1, 0, P(PR_PUSH, 'mov #1,r4', 'mov #2,r5', 'mov #3,r6', 'mov #4,r7', 'mov.l =@F1,r1', 'jsr @r1', 'nop', PR_POP, 'rts', 'nop')),
        ('F1', 1, 0, P('mov.l =@F2,r1', 'jmp @r1', 'nop')),
        ('F2', 1, 0, P('add #8,r4', 'mov.l =@F3,r1', 'jmp @r1', 'nop')),
        ('F3', 1, 0, P('mov r7,r0', 'add r6,r0', 'add r5,r0', 'rts', 'add r4,r0'))])
    fx('c7_tailret', 'the tail callee returns only on some paths (r0 written on one)', [
        ('F0', 1, 0, P(PR_PUSH, 'mov.l =@F1,r1', 'jsr @r1', 'nop', PR_POP, 'rts', 'nop')),
        ('F1', 2, 0, P('mov.l =@F2,r1', 'jmp @r1', 'nop')),
        ('F2', 2, 0, P('tst r4,r4', 'bt K1', 'mov r5,r0', 'K1:', 'rts', 'nop'))])
    # --- c8x: recursion, mutual recursion, tables, ignored and partial returns ---
    fx('c8_recur', 'f(n) = n + f(n-1), n masked to 0..3', [('F0', 1, 0, P(PR_PUSH, 'mov.l =@F1,r1', 'jsr @r1', 'nop', PR_POP, 'rts', 'nop')),
        ('F1', 1, 0, P('mov.l r8,@-r15', PR_PUSH, 'mov #3,r1', 'and r1,r4', 'mov r4,r8', 'mov #0,r0', 'tst r4,r4', 'bt K1', 'add #-1,r4', 'mov.l =@F1,r1', 'jsr @r1', 'nop', 'add r8,r0', 'K1:', PR_POP, 'rts', 'mov.l @r15+,r8'))])
    fx('c8_mutual', 'even/odd by mutual recursion on n masked to 0..3', [('F0', 1, 0, P(PR_PUSH, 'mov.l =@F1,r1', 'jsr @r1', 'nop', PR_POP, 'rts', 'nop')),
        ('F1', 1, 0, P(PR_PUSH, 'mov #3,r1', 'and r1,r4', 'mov #1,r0', 'tst r4,r4', 'bt K1', 'add #-1,r4', 'mov.l =@F2,r1', 'jsr @r1', 'nop', 'K1:', PR_POP, 'rts', 'nop')),
        ('F2', 1, 0, P(PR_PUSH, 'mov #3,r1', 'and r1,r4', 'mov #0,r0', 'tst r4,r4', 'bt K1', 'add #-1,r4', 'mov.l =@F1,r1', 'jsr @r1', 'nop', 'K1:', PR_POP, 'rts', 'nop'))])
    fx('c8_table', 'tab[i & 1](x): the callee is picked from a table of two function addresses and called through it', [
        ('F0', 1, 0, P('mov.l r8,@-r15', PR_PUSH, 'mov r5,r8', 'mov #1,r1', 'and r1,r4', 'mov r4,r0', 'shll2 r0', 'mov.l =@T,r1', 'mov.l @(r0,r1),r1', 'mov r8,r4', 'jsr @r1', 'nop', 'add r8,r0', PR_POP, 'rts', 'mov.l @r15+,r8', '.align4', 'T:', '.addr F1', '.addr F2')),
        ('F1', 1, 0, P('mov r4,r0', 'rts', 'add #1,r0')),
        ('F2', 1, 0, P('mov r4,r0', 'rts', 'shll r0'))])
    fx('c8_tableret', 'a table of two callees one of which returns nothing (r0 left as the argument it was handed)', [
        ('F0', 1, 0, P('mov.l r8,@-r15', PR_PUSH, 'mov r5,r8', 'mov #1,r1', 'and r1,r4', 'mov r4,r0', 'shll2 r0', 'mov.l =@T,r1', 'mov.l @(r0,r1),r1', 'mov r8,r4', 'jsr @r1', 'nop', 'add r8,r0', PR_POP, 'rts', 'mov.l @r15+,r8', '.align4', 'T:', '.addr F1', '.addr F2')),
        ('F1', 1, 0, P('mov r4,r0', 'rts', 'add #1,r0')),
        ('F2', 0, 0, P('mov.l =0x06030000,r1', 'rts', 'mov.l r4,@r1'))])
    fx('c8_ignored', 'a callee returning r0 whose value the caller ignores, and a void callee whose r0 the caller reads (stale)', [
        ('F0', 1, 0, P(PR_PUSH, 'mov #4,r4', 'mov.l =@F1,r1', 'jsr @r1', 'nop', 'mov.l =@F2,r1', 'jsr @r1', 'nop', 'mov r0,r4', 'mov r4,r0', PR_POP, 'rts', 'nop')),
        ('F1', 1, 0, P('mov r4,r0', 'rts', 'add #9,r0')),
        ('F2', 0, 0, P('mov.l =0x06030000,r1', 'rts', 'mov.l r4,@r1'))])
    fx('c8_partial', 'a callee that returns r0 on one path and falls out with r0 as left by an earlier call on the other', [
        ('F0', 1, 0, P(PR_PUSH, 'mov.l =@F1,r1', 'jsr @r1', 'nop', PR_POP, 'rts', 'nop')),
        ('F1', 2, 0, P(PR_PUSH, 'tst r4,r4', 'bf K1', 'mov.l =@F2,r1', 'jsr @r1', 'nop', 'K1:', 'mov r4,r0' if False else 'nop', PR_POP, 'rts', 'nop')),
        ('F2', 1, 0, P('mov #7,r0', 'rts', 'nop'))])
    fx('c8_deep', 'a chain of six functions, each passing its argument on and adding to the result, with r8 in use at every level', [
        ('F%d' % i, 1, 0, P('mov.l r8,@-r15', PR_PUSH, 'mov r4,r8', 'add #1,r4', 'mov.l =@F%d,r1' % (i + 1), 'jsr @r1', 'nop', 'add r8,r0', PR_POP, 'rts', 'mov.l @r15+,r8')) for i in range(5)] +
        [('F5', 1, 0, P('mov r4,r0', 'rts', 'shll r0'))])
    fx('c8_r0cond', 'a callee that sets r0 only inside one arm of an if; the caller uses r0 after the call whichever arm', [
        ('F0', 1, 0, P(PR_PUSH, 'mov.l =@F1,r1', 'jsr @r1', 'nop', 'add #1,r0', PR_POP, 'rts', 'nop')),
        ('F1', 2, 0, P('mov r4,r0', 'tst r5,r5', 'bt K1', 'mov r6,r0', 'K1:', 'rts', 'nop'))])
    # --- c3w: a callee that walks a pointer from its spill of r4-r7 up into the stack words (va_arg over a va_list; `&a` of a fifth parameter) ---
    spill = P('mov.l r7,@-r15', 'mov.l r6,@-r15', 'mov.l r5,@-r15', 'mov.l r4,@-r15')
    walk = P('mov r15,r2', 'add #4,r2', 'mov #0,r0', 'L1:', 'mov.l @r2+,r3', 'add r3,r0', 'dt r1', 'bf L1', 'add #16,r15', 'rts', 'nop')
    callw = lambda r4: P(PR_PUSH, 'add #-4,r15', 'mov.l =0x2222,r1', 'mov.l r1,@(0,r15)', 'mov #%d,r4' % r4, 'mov #2,r5', 'mov #3,r6', 'mov #4,r7', 'mov.l =@F1,r1', 'jsr @r1', 'nop', 'add #4,r15', PR_POP, 'rts', 'nop')
    fx('c3w_const4', 'A3-280: the callee spills r4-r7 and sums four words from the first spilled one on: the fifth argument (0x2222) is the fourth word read', [
        ('F0', 1, 0, callw(3)), ('F1', 1, 0, spill + P('mov #4,r1') + walk)])
    fx('c3w_countr4', 'A3-280: the same with the count taken from r4 (va_arg until the count of the first argument)', [
        ('F0', 1, 0, callw(3)), ('F1', 1, 0, spill + P('mov r4,r1', 'mov #3,r2', 'and r2,r1', 'add #1,r1') + walk)])
    fx('c3w_const3', 'control: three words, which stop before the stack argument (the prototype of 4 is right)', [
        ('F0', 1, 0, callw(3)), ('F1', 1, 0, spill + P('mov #3,r1') + walk)])
    fx('c3w_index', 'A3-280: an index in r4 into its own entry area through r0 (`mov.l @(r0,r2),r0`): word 0 is the stack argument', [
        ('F0', 1, 0, callw(0)), ('F1', 1, 0, P('mov r15,r2', 'mov r4,r0', 'shll2 r0', 'mov.l @(r0,r2),r0', 'rts', 'nop'))])
    fx('c3w_loop_entry', 'control: a fixed loop from the entry r15 (no spill): the engine withholds the callee, the call keeps the stack word', [
        ('F0', 1, 0, callw(0)), ('F1', 1, 0, P('mov #4,r1', 'mov r15,r2', 'mov #0,r0', 'L1:', 'mov.l @r2+,r3', 'add r3,r0', 'dt r1', 'bf L1', 'rts', 'nop'))])
    return out


def main():
    count = int(sys.argv[1]) if len(sys.argv) > 1 else 400
    seed = int(sys.argv[2]) if len(sys.argv) > 2 else 1
    out = ['# Area 3 round 41 (mk.py): compiled-shaped fixtures. Run: W=<SaturnExplorer> S=<scratch> run.sh [name]']
    named(out)
    for i in range(count):
        mod(random.Random(seed * 1000003 + i), 'g%d' % (seed * 10000 + i), out)
    open('fixtures.src', 'w').write('\n'.join(out) + '\n')
    print('wrote fixtures.src: %d lines' % len(out))


main()
