#!/usr/bin/env python3
# Area 2 round 15 instrument. Register-only functions for the printed-C differential (and for the in-situ rule checks), on the control-flow shapes that
# patch 0067's ActionLateDoNothing sees (loads dagen14.py and through it the earlier generators; their helpers are reused):
#   79 cfgfuzz   a random control-flow graph of 3 to 11 basic blocks over r0 and r1 (r4..r7 read-only; r2 and r3 the counters of bounded loops; r12 and r13 dead
#                sinks): every block ends with a fall-through, a conditional branch (bt, bf, bt/s, bf/s) or a bra to a LATER block, or closes a bounded loop
#                (dt, bf); a branch into the middle of a loop from outside is never made, so every run ends. The body of a block is empty, a nop, dead
#                (writes only a sink, or consumes T into a sink), or live; T is set by a compare before the branch, or not (the branch then reads the T of an
#                earlier block, or the entry T), and two branches in a row on one T are drawn on purpose. Edges that go to a block that is empty are the point:
#                ActionDoNothing / ActionLateDoNothing remove them, and a conditional both of whose arms are empty must not lose its test.
#   80 emptyarms the same generator with 3 to 6 blocks, most of them empty, so that the empty arms, shared joins and chained empty blocks are dense; some blocks
#                are a second return (rts with a live slot), and a loop whose only body is an empty conditional is drawn
#   81 emptyloop an infinite-or-bounded loop of empty blocks around a conditional (a do-nothing block that returns to itself, two empty blocks in a cycle), kept
#                terminating by the counter, with empty arms inside
# usage: dagen15.py <first seed> <count> [family 79..81]   -> one function per seed, ";" joined (SE_GENERATED_PRINTEDC_WORDS)
#        dagen15.py extras <first seed> <count> <family>   -> the comma-joined hex list for SE_GENERATED_PRINTEDC_EXTRA
import importlib.util, os, random, sys

here = os.path.dirname(os.path.abspath(__file__))
spec = importlib.util.spec_from_file_location("dagen14", os.path.join(here, "..", "a2_round14", "dagen14.py"))
d14 = importlib.util.module_from_spec(spec)
spec.loader.exec_module(d14)
globals().update({k: v for k, v in vars(d14).items() if not k.startswith('__')})

SINK = [12, 13]
LIVE = [0, 1]
SRC = [0, 1, 4, 5, 6, 7]
NOP = 0x0009

def cmp_word(r, a, b):
    k = r.randrange(0, 10)
    if k < 5: return [cmpeq, cmphi, cmphs, cmpgt, cmpge][k](b, a)
    if k == 5: return cmppz(a)
    if k == 6: return cmppl(a)
    if k == 7: return tstr(b, a)
    if k == 8: return 0x8800 | r.choice([0, 1, 0x7F, 0x80, 0xFF])        # cmp/eq #imm,r0
    return 0xC800 | r.choice([1, 0x80, 0xFF])                             # tst #imm,r0

def dead_op(r):
    k = r.randrange(0, 8)
    s = r.choice(SINK); a = r.choice(SRC)
    if k == 0: return [0x300C | s << 8 | a << 4]                          # add a,sink
    if k == 1: return [movt(s)]                                           # sink = T
    if k == 2: return [addc(a, s)]                                        # sink += a + T (T out)
    if k == 3: return [SHLL | s << 8]                                     # shll sink (T out)
    if k == 4: return [0x6003 | s << 8 | a << 4]                          # mov a,sink
    if k == 5: return [subc(a, s)]
    if k == 6: return [movi(s, r.randrange(0, 256))]
    return [0x2009 | s << 8 | a << 4]                                     # and a,sink

def live_op(r):
    d = r.choice(LIVE); s = r.choice(SRC)
    return list(op2(r, d, s))

def body_ops(r, pe):
    """the body of a block: empty / nop / dead with probability pe, else live"""
    x = r.random()
    if x < pe * 0.45: return []
    if x < pe * 0.7: return [NOP]
    if x < pe: return dead_op(r) + (dead_op(r) if r.random() < 0.3 else [])
    return live_op(r) + (live_op(r) if r.random() < 0.3 else [])

def build_cfg(r, nb, pe, loops, second_ret=False, tcmp=0.7):
    f = Fn(r)
    f.d_hint = None
    f.add(mov_(0, 4), mov_(1, 5))
    # loop intervals [s, e] over block indices, properly nested or disjoint; the loop branch closes block e
    intervals = []
    if loops and nb >= 3:
        for _ in range(r.randrange(0, 3)):
            s = r.randrange(0, nb - 1); e = r.randrange(s + 1, nb)
            ok = all((e < a or s > b) or (a <= s and e <= b and (a, b) != (s, e)) or (s <= a and b <= e and (a, b) != (s, e)) for (a, b) in intervals)
            # nested loops must not share the closing block or the header block (one counter per depth, one closing branch per block)
            ok = ok and all(e != b and s != a for (a, b) in intervals)
            if ok and len(intervals) < 2: intervals.append((s, e))
    depth_ctr = {}
    for (s, e) in intervals:
        inner = sum(1 for (a, b) in intervals if a <= s and e <= b and (a, b) != (s, e))
        depth_ctr[(s, e)] = 2 + inner               # an inner loop has a counter of its own: the outer one is not reused
    # nested loops: the inner loop (contained in another) gets r3, the outer r2
    for (s, e) in intervals:
        contained = any(a <= s and e <= b and (a, b) != (s, e) for (a, b) in intervals)
        depth_ctr[(s, e)] = 3 if contained else 2
    def allowed(p, t):
        for (s, e) in intervals:
            if s <= t <= e and not (s <= p <= e): return False
        return True
    for i in range(nb):
        for (s, e) in intervals:
            if s == i: f.add(movi(depth_ctr[(s, e)], r.randrange(1, 5)))
        f.label('B%d' % i)
        f.add(*body_ops(r, pe)) if body_ops else None
        closing = [(s, e) for (s, e) in intervals if e == i]
        if closing:
            s, e = closing[0]
            f.add(dt(depth_ctr[(s, e)]))
            f.br('bf', 'B%d' % s)
            continue
        if i == nb - 1: break
        k = r.random()
        targets = [t for t in range(i + 2, nb + 1) if allowed(i, t)]   # i + 1 is the fall-through
        if k < 0.20 or not targets:
            continue                                                   # fall through
        t = r.choice(targets)
        tl = 'B%d' % t if t < nb else 'END'
        if k < 0.30 and (i + 1) < nb and allowed(i, i + 1):
            f.br('bra', tl); f.add(*r.choice([[NOP], dead_op(r)[:1], [NOP]]) if r.random() < 0.7 else live_op(r)[:1])
            continue
        if r.random() < tcmp: f.add(cmp_word(r, r.choice(LIVE), r.choice(SRC)))
        kind = r.choice(['bt', 'bf', 'bt', 'bf', 'bts', 'bfs'])
        f.br(kind, tl)
        if kind in ('bts', 'bfs'):
            # the slot executes on both paths; a slot that is a T producer changes what the target sees only through T (the branch read T first)
            f.add(*(dead_op(r)[:1] if r.random() < 0.6 else live_op(r)[:1]))
        if second_ret and r.random() < 0.15:
            f.add(0x000B, mov_(0, 1))
    f.label('END')
    f.rts_slot = mov_(0, 1)
    return f

def fam_cfgfuzz(r, f2=None):
    nb = r.randrange(3, 12)
    return build_cfg(r, nb, r.choice([0.2, 0.4, 0.6]), True, second_ret=r.random() < 0.2, tcmp=r.choice([0.5, 0.8, 1.0]))

def fam_emptyarms(r, f2=None):
    nb = r.randrange(3, 7)
    return build_cfg(r, nb, r.choice([0.7, 0.85, 0.95]), r.random() < 0.3, second_ret=r.random() < 0.3, tcmp=r.choice([0.3, 0.6, 1.0]))

def fam_emptyloop(r, f2=None):
    # a bounded loop whose body is empty blocks around a conditional, nothing live inside; a live use of the counter after the loop
    f = Fn(r); f.d_hint = None
    f.add(mov_(0, 4), mov_(1, 5), movi(2, r.randrange(1, 5)))
    form = r.randrange(0, 5)
    if form == 0:         # while(ctr) { if (c) {} else {} }
        f.label('top'); f.add(cmp_word(r, 0, r.choice(SRC))); f.br(r.choice(['bt', 'bf']), 'a')
        f.add(*body_ops(r, 1.0)); f.br('bra', 'j'); f.add(NOP); f.label('a'); f.add(*body_ops(r, 1.0)); f.label('j'); f.add(dt(2)); f.br('bf', 'top')
    elif form == 1:       # two chained conditionals on one T with empty arms inside the loop
        f.label('top'); f.add(cmp_word(r, 1, r.choice(SRC))); f.br('bt', 'x'); f.add(*body_ops(r, 1.0)); f.label('x')
        f.br(r.choice(['bt', 'bf']), 'y'); f.add(*body_ops(r, 1.0)); f.label('y'); f.add(dt(2)); f.br('bf', 'top')
    elif form == 2:       # empty block in a cycle: the loop branch jumps back over nothing
        f.label('top'); f.add(*body_ops(r, 0.9)); f.add(dt(2)); f.br('bf', 'top')
        f.add(cmp_word(r, 0, r.choice(SRC))); f.br('bt', 'z'); f.add(*body_ops(r, 1.0)); f.label('z')
    elif form == 3:       # nested empty conditionals
        f.label('top'); f.add(cmp_word(r, 0, r.choice(SRC))); f.br('bf', 'o')
        f.add(cmp_word(r, 1, r.choice(SRC))); f.br('bt', 'i'); f.add(*body_ops(r, 1.0)); f.label('i'); f.add(*body_ops(r, 1.0)); f.label('o')
        f.add(dt(2)); f.br('bf', 'top')
    else:                 # a triangle whose body is empty, repeated on the same test
        f.label('top'); f.add(cmp_word(r, 0, r.choice(SRC)))
        for _ in range(r.randrange(2, 4)):
            l = 'l%d' % r.randrange(1000000)
            f.br(r.choice(['bt', 'bf']), l); f.add(*body_ops(r, 1.0)); f.label(l)
        f.add(dt(2)); f.br('bf', 'top')
    f.add(add__(1, 0)) if r.random() < 0.5 else None
    f.rts_slot = mov_(0, 1)
    return f

def fam_carrycond(r, f2=None):
    # The nested block conditions of the printer (A2-101): `bt BODY ; <carry op> ; bf END ; <cmp> ; bf END ; BODY: ... ; END:` and its senses. The carry or overflow of
    # addc, subc, addv, subv, negc is an OR of two compares inside one block (with a statement for the sum), tested by one branch and followed by a second test,
    # under a first branch that decides the body on its own (the entry T or a compare): OR(t0, AND(carry, c)) in the printer's block conditions.
    f = Fn(r); f.d_hint = None
    f.add(mov_(0, 4), mov_(1, 5))
    if r.random() < 0.4:
        # the same conditions as the test of a loop: `while (count && carry && c) { ... }` with a statement before the test (the block conditions of loops print with
        # the comma form, as the nested ones of ifs do)
        f.add(movi(2, r.randrange(1, 5)))
        f.label('TOP')
        f.add(dt(2)); f.br('bt', 'END')
        a = r.choice([0, 1, 12]); b = r.choice(SRC)
        f.add(r.choice([addc(b, a), subc(b, a), 0x300F | a << 8 | b << 4, 0x300B | a << 8 | b << 4, 0x600A | a << 8 | b << 4]))
        f.br(r.choice(['bt', 'bf']), 'END')
        if r.random() < 0.8: f.add(cmp_word(r, r.choice(LIVE), r.choice(SRC)))
        f.br(r.choice(['bt', 'bf']), 'END')
        f.add(*live_op(r))
        f.br('bra', 'TOP'); f.add(NOP)
        f.label('END')
        f.rts_slot = mov_(0, 1)
        return f
    if r.random() < 0.4: f.add(*op2(r, 1, r.choice([0, 6, 7])))
    body, end = 'BODY', 'END'

    if r.random() < 0.5: f.add(cmp_word(r, r.choice(LIVE), r.choice(SRC)))
    f.br(r.choice(['bt', 'bf']), body)
    a = r.choice([0, 1, 12]); b = r.choice(SRC)
    t = r.randrange(0, 7)
    f.add({0: addc(b, a), 1: subc(b, a), 2: 0x300F | a << 8 | b << 4, 3: 0x300B | a << 8 | b << 4, 4: 0x600A | a << 8 | b << 4,
           5: addc(b, a), 6: subc(b, a)}[t])
    f.br(r.choice(['bt', 'bf']), end)
    if r.random() < 0.8: f.add(cmp_word(r, r.choice(LIVE), r.choice(SRC)))
    f.br(r.choice(['bt', 'bf']), end)
    if r.random() < 0.3: f.add(*body_ops(r, 0.5))
    f.label(body)
    f.add(*live_op(r))
    if r.random() < 0.5: f.add(*live_op(r))
    f.label(end)
    f.rts_slot = mov_(0, 1)
    return f

def clrt_(): return 0x0008
def sett_(): return 0x0018

def fam_cmp64(r, f2=None):
    # 64-bit compares and selections on the pairs (r4:r5) and (r6:r7) (high word first): random decision ladders over the high and low words (cmp/hi, cmp/gt, cmp/ge,
    # cmp/hs, cmp/eq, tst, in either operand order, each branch sense), a flag materialised by movt after such a ladder, the borrow chain clrt; subc; subc read as T,
    # a 64-bit negate or absolute value by negc, a conditional select of one of the pairs (min, max), and the 64-bit zero and sign tests (RuleDouble*, the
    # pair forms of double.cc and the carry/borrow rules).
    f = Fn(r); f.d_hint = None
    hi_a, lo_a, hi_b, lo_b = 4, 5, 6, 7
    f.add(mov_(1, 5))
    form = r.randrange(0, 7)
    def swp():
        return r.random() < 0.5
    def hicmp():
        x, y = (hi_a, hi_b) if swp() else (hi_b, hi_a)
        return r.choice([cmphi, cmpgt, cmpge, cmphs])(x, y)
    def locmp():
        x, y = (lo_a, lo_b) if swp() else (lo_b, lo_a)
        return r.choice([cmphi, cmphs])(x, y)
    if form == 0 or form == 1:
        # ladder: hi strict, hi eq, lo compare (any order of the first two), two exits
        t, fl, end = 'T', 'F', 'END'
        order = r.randrange(0, 2)
        steps = [('c', hicmp(), r.choice(['bt', 'bf']), r.choice([t, fl])),
                 ('c', cmpeq(hi_a, hi_b), r.choice(['bt', 'bf']), r.choice([t, fl]))]
        if order: steps.reverse()
        for _, w, br, lab in steps:
            f.add(w); f.br(br, lab)
        f.add(locmp()); f.br(r.choice(['bt', 'bf']), r.choice([t, fl]))
        # falling off the ladder lands in the false arm
        f.label(fl); f.add(movi(1, r.choice([0, 0, 1, 5]))); f.br('bra', end); f.add(NOP)
        f.label(t); f.add(movi(1, r.choice([1, 1, 0, 7])))
        f.label(end)
        if form == 1: f.add(add__(4, 1))
    elif form == 2:
        # a flag by movt: equality of the pairs with the second compare skipped when the first fails; the result combined with a pair word
        f.add(cmpeq(hi_a, hi_b)); f.br(r.choice(['bf', 'bt']), 'X'); f.add(cmpeq(lo_a, lo_b)); f.label('X'); f.add(movt(1))
        if r.random() < 0.5: f.add(r.choice([xor_, add__, or__, and__])(r.choice([4, 5, 6, 7]), 1))
    elif form == 3:
        # the borrow chain: a - b over the pair, T the borrow, the high word of the difference read
        f.add(clrt_(), mov_(2, lo_a), subc(lo_b, 2), mov_(3, hi_a), subc(hi_b, 3))
        k = r.randrange(0, 4)
        if k == 0: f.add(movt(1))
        elif k == 1: f.add(movt(1), add__(3, 1))
        elif k == 2: f.add(mov_(1, 3), xor_(2, 1))
        else: f.add(mov_(1, 3), SHLL | 1 << 8, movt(1), add__(2, 1)) if False else f.add(mov_(1, 3), cmppz(1), movt(1))
    elif form == 4:
        # negate or absolute value of the pair (negc chain), then a word of the result read
        if r.random() < 0.5:
            f.add(cmppz(hi_a)); f.br('bt', 'S')
        f.add(clrt_(), 0x600A | lo_a << 8 | lo_a << 4, 0x600A | hi_a << 8 | hi_a << 4)
        f.label('S')
        f.add(mov_(1, r.choice([hi_a, lo_a])))
        if r.random() < 0.5: f.add(xor_(r.choice([lo_a, hi_a]), 1))
    elif form == 5:
        # min or max of the pair: copy the chosen pair, ladder as above
        f.add(mov_(2, hi_a), mov_(3, lo_a))
        f.add(hicmp()); f.br(r.choice(['bt', 'bf']), 'K')
        f.add(cmpeq(hi_a, hi_b)); f.br('bf', 'P'); f.add(locmp()); f.br(r.choice(['bt', 'bf']), 'K')
        f.label('P'); f.add(mov_(2, hi_b), mov_(3, lo_b))
        f.label('K'); f.add(mov_(1, 2), xor_(3, 1))
    else:
        # zero and sign tests of the pair: or the words, test; sign by the high word; combined into a flag
        f.add(mov_(2, hi_a), or__(lo_a, 2), tstr(2, 2))
        k = r.randrange(0, 3)
        if k == 0: f.add(movt(1))
        elif k == 1:
            f.br('bt', 'Z'); f.add(cmppz(hi_a), movt(1), SHLL | 1 << 8, 0x7101); f.br('bra', 'E'); f.add(NOP)
            f.label('Z'); f.add(movi(1, 0)); f.label('E')
        else: f.add(movt(1), cmppz(hi_b), movt(2), xor_(2, 1))
    f.rts_slot = mov_(0, 1)
    return f

def fam_exprtree(r, f2=None):
    # A random expression tree of depth 2 to 5 over the inputs r4..r7 and literals, each temporary used once (so the engine inlines it into one expression and the
    # printer decides every parenthesis): binary add/sub/and/or/xor/mul, shifts by constants, negate/not, extensions and swaps, compares materialised by movt
    # (cmp + movt), and trees whose root is a branch condition (cmp of two subtrees, then a diamond). A register stack: the left operand stays in its register
    # while the right is built in the next one.
    f = Fn(r); f.d_hint = None
    pool = [1, 2, 3, 8, 9, 10, 11, 12]
    BIN = [add__, sub__, and__, or__, xor_]
    def gen(depth, k):
        d = pool[k]
        if depth == 0 or r.random() < 0.15:
            if r.random() < 0.25:
                f.lit(d, r.choice([1, 2, 3, 0xFF, 0x1F, 0x80000000, 0xFFFFFFFF, 0x7FFFFFFF, 0x10000, 0xFFFF, r.getrandbits(32)]))
            elif r.random() < 0.15:
                f.add(movi(d, r.choice([0, 1, 2, 7, 0xFF, 0x80, 0xFE])))
            else:
                f.add(mov_(d, r.choice([4, 5, 6, 7])))
            return
        x = r.random()
        if x < 0.55 and k + 1 < len(pool):
            gen(depth - 1, k); gen(depth - 1, k + 1)
            e = pool[k + 1]
            y = r.random()
            if y < 0.70: f.add(r.choice(BIN)(e, d))
            elif y < 0.80: f.add(mulL(e, d), stsmacl(d))
            elif y < 0.92: f.add(r.choice([cmpeq, cmphi, cmphs, cmpgt, cmpge])(e, d), movt(d))
            else: f.add(r.choice([0x200F, 0x200E]) | d << 8 | e << 4, stsmacl(d))     # muls.w / mulu.w
        else:
            gen(depth - 1, k)
            y = r.random()
            if y < 0.30: f.add(r.choice([SHLL, SHLR, SHAR, 0x4008, 0x4009, 0x4018, 0x4019, 0x4028, 0x4029, ROTL, ROTR]) | d << 8)
            elif y < 0.45: f.add(r.choice([neg_, not_, extub, extuw, extsb, extsw, swapw_, swapb_])(d, d))
            elif y < 0.60: f.add(cmppz(d) if r.random() < 0.5 else cmppl(d), movt(d))
            elif y < 0.75: f.add(tstr(d, d), movt(d))
            elif y < 0.88: f.add(addi(d, r.choice([1, 0xFF, 0xFE, 2, 0x7F, 0x80])))
            else: f.add(andi(r.choice([0xFF, 1, 0x7F, 0xF0])) if d == 0 else neg_(d, d))
    depth = r.randrange(2, 6)
    if r.random() < 0.35:
        # the root is a condition: cmp of two subtrees (or one subtree and zero), a diamond that picks one of two subtrees' values
        gen(depth - 1, 0); gen(depth - 1, 1)
        f.add(r.choice([cmpeq, cmphi, cmphs, cmpgt, cmpge])(pool[1], pool[0]))
        f.br(r.choice(['bt', 'bf']), 'A')
        gen(depth - 2 if depth > 2 else 1, 2); f.add(mov_(pool[0], pool[2]))
        f.br('bra', 'B'); f.add(NOP)
        f.label('A'); f.add(movi(pool[0], r.choice([0, 1, 5])))
        f.label('B')
    else:
        gen(depth, 0)
    f.add(mov_(1, pool[0]) if pool[0] != 1 else NOP)
    f.rts_slot = mov_(0, 1)
    return f

_s11 = importlib.util.spec_from_file_location("swtab11", os.path.join(here, "..", "a2_round16", "swtab11.py"))   # commit 16940af removed a2_round11/swtab11.py; the copy kept with round 16 is the same file (A2-105)
sw11 = importlib.util.module_from_spec(_s11)
_s11.loader.exec_module(sw11)

def build_swreg(seed):
    """a jump-table switch over r4 whose arms set r1 (or do nothing: empty arms, dead arms, several arms with the same target), joined or returning, for the printed-C differential"""
    r = random.Random(seed * 7919 + 15)
    a = sw11.A()
    n = r.choice([2, 3, 4, 5, 6, 8])
    base = r.choice([0, 0, 0, 1, 2])
    guard = r.choice(['hi', 'hs', 'bfhi'])
    join = r.random() < 0.75
    a.w(0x6003 | 1 << 8 | 5 << 4)                      # mov r5,r1
    a.w(0x6003 | 0 << 8 | 4 << 4)                      # mov r4,r0
    if r.random() < 0.3: a.w(r.choice([0x600C, 0x600D]))
    if base: a.w(0x7000 | (-base & 0xFF))
    bound = n - 1
    a.w(0xE000 | 2 << 8 | (n if guard in ('hs',) else bound))
    if guard == 'hi':
        a.w(0x3006 | 0 << 8 | 2 << 4); a.ref('bt', 'default')
    elif guard == 'hs':
        a.w(0x3002 | 0 << 8 | 2 << 4); a.ref('bt', 'default')
    else:
        a.w(0x3006 | 0 << 8 | 2 << 4); a.ref('bf', 'dispatch'); a.ref('bra', 'default'); a.w(0x0009)
    a.label('dispatch')
    a.w(0x6003 | 2 << 8 | 0 << 4)
    a.w(0x4008 | 2 << 8)
    a.ref('mova', 'table')
    a.w(0x000E | 0 << 8 | 2 << 4)
    a.w(0x402B | 0 << 8); a.w(0x0009)
    tgt = []; k = 0
    emptyp = r.choice([0.3, 0.6, 0.9])
    for i in range(n):
        x = r.random()
        if x < 0.15: tgt.append('default')
        elif i > 0 and x < 0.45 and tgt[-1] != 'default': tgt.append(tgt[-1])
        else: tgt.append('c%d' % k); k += 1
    if k == 0: tgt[0] = 'c0'; k = 1
    for c in range(k):
        a.label('c%d' % c)
        x = r.random()
        if x < emptyp * 0.5: pass
        elif x < emptyp * 0.8: a.w(*r.choice([[0x0009], dead_op(r)]))
        elif x < emptyp: a.w(*dead_op(r))
        else: a.w(*r.choice([[0xE100 | r.randrange(0, 256)], [0x7100 | r.randrange(1, 255)], [0x3018 | 4 << 4 | 1 << 8 if False else 0x300C | 1 << 8 | 6 << 4]]))
        if join: a.ref('bra', 'join'); a.w(0x0009)
        else: a.w(0x6003 | 0 << 8 | 1 << 4); a.w(0x000B); a.w(0x0009)
    a.label('default')
    a.w(0xE100 | r.choice([0x7F, 0x01, 0x00]))
    if join: a.w(0x0009)
    else: a.w(0x6003 | 0 << 8 | 1 << 4); a.w(0x000B); a.w(0x0009)
    if join:
        a.label('join'); a.w(0x300C | 1 << 8 | 4 << 4) if r.random() < 0.5 else a.w(0x0009)
        a.w(0x6003 | 0 << 8 | 1 << 4); a.w(0x000B); a.w(0x0009)
    a.align4()
    a.label('table')
    for t in tgt: a.long(t)
    words, labels = sw11.assemble(a, [])
    return " ".join("%04X" % w for w in words)

FAMS15_85 = 85
FAMS15 = {79: fam_cfgfuzz, 80: fam_emptyarms, 81: fam_emptyloop, 82: fam_carrycond, 83: fam_cmp64, 84: fam_exprtree}

def build(seed, fam=None, want_fn=False):
    if fam == 85:
        return build_swreg(seed)
    r = random.Random(seed * 104729 + 15)
    fm = FAMS15[fam] if fam is not None else FAMS15[r.choice(sorted(FAMS15))]
    f = fm(r)
    f.add(0x000B, getattr(f, 'rts_slot', NOP))
    if want_fn: return f
    return finish(f)

def lits_of15(f):
    return [c[2] & M32 for c in f.w if isinstance(c, tuple) and c[0] == 'lit']

def extras(first, count, fam):
    vals = set()
    for s in range(first, first + count):
        f = build(s, fam, want_fn=True)
        for v in lits_of15(f):
            for dv in (-2, -1, 0, 1, 2): vals.add((v + dv) & M32)
            vals.add((-v) & M32)
    return sorted(vals)

if __name__ == "__main__":
    if sys.argv[1] == 'extras':
        first, count, fam = int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4])
        print(",".join("%X" % v for v in extras(first, count, fam)))
    else:
        first, count = int(sys.argv[1]), int(sys.argv[2])
        fam = int(sys.argv[3]) if len(sys.argv) > 3 else None
        out = [g for g in (build(s, fam) for s in range(first, first + count)) if g]
        print(";".join(out))
