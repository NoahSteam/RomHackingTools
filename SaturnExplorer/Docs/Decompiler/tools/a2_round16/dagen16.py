#!/usr/bin/env python3
# Area 2 round 16 instrument. Register-only functions for the printed-C differential (and the in-situ checks) on the control-flow and condition shapes that
# families 0 to 85 did not draw (loads dagen14.py and through it the earlier generators; dagen15.py cannot be loaded from the tree since commit 16940af removed
# a2_round11/swtab11.py, which it imports: a copy is kept beside this file and used for family 88):
#   86 loopbrk   structured loops nested to depth 3 (do-while by dt/bf, top-tested by dt/bt with bra back), bodies of live ops and ifs, with conditional breaks to
#                the loop's own exit AND to the exit of an enclosing loop (a multi-level break), continues to the loop's next-iteration point, and an early return
#   87 gotomess  a general control-flow graph of 4 to 10 blocks: any block may branch (bt, bf, bra) to any other, backward and into the middle of other cycles
#                (irreducible flow). Every backward edge goes through a stub `dt r13 ; bt END ; bra target` (r13 a fuel counter), so every run ends
#   88 swloop    a jump-table switch inside a counted loop: arms that continue, break out of the loop, fall to the join, or return; empty arms; arms shared
#   89 condtree  random AND/OR/NOT trees of compares (leaves with side effects before the compare: the comma form of the printer) compiled with short-circuit
#                branches into an if/else, a while, a do-while and an else-if chain, nested to depth 2 in a loop
#   90 loopcond  a loop whose exit test is a condition tree (89's) with an extra `break` and a statement in the test, the exits shared with a second loop
# usage: dagen16.py <first seed> <count> [family 86..90]   -> one function per seed, ";" joined (SE_GENERATED_PRINTEDC_WORDS)
#        dagen16.py show <seed> <family>                   -> the function as labelled items
import importlib.util, os, random, sys

HERE16 = os.path.dirname(os.path.abspath(__file__))
here = HERE16
spec = importlib.util.spec_from_file_location("dagen14", os.path.join(here, "..", "a2_round14", "dagen14.py"))
d14 = importlib.util.module_from_spec(spec)
spec.loader.exec_module(d14)
globals().update({k: v for k, v in vars(d14).items() if not k.startswith('__')})
_s11 = importlib.util.spec_from_file_location("swtab11", os.path.join(HERE16, "swtab11.py"))
sw11 = importlib.util.module_from_spec(_s11)
_s11.loader.exec_module(sw11)

SINK = [12]
LIVE = [0, 1]
SRC = [0, 1, 4, 5, 6, 7]
NOP = 0x0009
CTR = [2, 3, 8, 9]              # loop counters by depth


def cmp_word(r, a, b):
    k = r.randrange(0, 10)
    if k < 5: return [cmpeq, cmphi, cmphs, cmpgt, cmpge][k](b, a)
    if k == 5: return cmppz(a)
    if k == 6: return cmppl(a)
    if k == 7: return tstr(b, a)
    if k == 8: return 0x8800 | r.choice([0, 1, 0x7F, 0x80, 0xFF])        # cmp/eq #imm,r0
    return 0xC800 | r.choice([1, 0x80, 0xFF])                             # tst #imm,r0


def live_op(r):
    d = r.choice(LIVE); s = r.choice(SRC)
    return list(op2(r, d, s))


def live_cmp(r):
    return cmp_word(r, r.choice(LIVE), r.choice(SRC))


class Gen:
    """label allocator and the shared pieces"""
    def __init__(self, r):
        self.r = r
        self.f = Fn(r)
        self.f.d_hint = None
        self.n = 0

    def lab(self, p='L'):
        self.n += 1
        return '%s%d' % (p, self.n)

    def body(self, k=1):
        for _ in range(k):
            self.f.add(*live_op(self.r))


# ------------------------------------------------------------------------------------------------------------------------------------ 86 loopbrk
def fam_loopbrk(r):
    g = Gen(r)
    f = g.f
    f.add(mov_(0, 4), mov_(1, 5))
    END = 'END'
    # exits: stack of (exit label, continue label)
    def stmts(depth, loops, budget):
        n = r.randrange(1, 4)
        for _ in range(n):
            if budget[0] <= 0: return
            budget[0] -= 1
            x = r.random()
            if x < 0.30 or (depth >= 3 and x < 0.5):
                g.body(r.randrange(1, 3))
            elif x < 0.55:
                # if / if-else with live bodies, maybe a break or continue inside
                f.add(live_cmp(r))
                els, end = g.lab('E'), g.lab('J')
                f.br(r.choice(['bt', 'bf']), els)
                stmts(depth + 1, loops, budget) if r.random() < 0.6 else g.body(1)
                if r.random() < 0.5:
                    f.br('bra', end); f.add(NOP)
                    f.label(els)
                    stmts(depth + 1, loops, budget) if r.random() < 0.6 else g.body(1)
                    f.label(end)
                else:
                    f.label(els)
            elif x < 0.75 and loops:
                # conditional break (maybe multi-level) or continue
                f.add(live_cmp(r))
                lvl = r.randrange(0, len(loops))
                ex, ct = loops[-1 - lvl]
                tgt = ex if r.random() < 0.65 else ct
                if lvl > 0 and tgt == ct:
                    tgt = loops[-1][1]
                f.br(r.choice(['bt', 'bf']), tgt)
            elif x < 0.80:
                f.add(live_cmp(r)); f.br(r.choice(['bt', 'bf']), END)     # early return
            elif depth < 3 and budget[0] > 3:
                loop(depth, loops, budget)
            else:
                g.body(1)

    def loop(depth, loops, budget):
        ctr = CTR[depth]
        top, ex, ct = g.lab('T'), g.lab('X'), g.lab('C')
        f.add(movi(ctr, r.randrange(1, 4)))
        form = r.randrange(0, 3)
        if form == 0:        # do { body } while (--ctr)
            f.label(top)
            stmts(depth + 1, loops + [(ex, ct)], budget)
            f.label(ct)
            f.add(dt(ctr)); f.br('bf', top)
            f.label(ex)
        elif form == 1:      # while (--ctr) { body }   (the test first)
            f.label(top)
            f.add(dt(ctr)); f.br('bt', ex)
            stmts(depth + 1, loops + [(ex, top)], budget)
            f.br('bra', top); f.add(NOP)
            f.label(ex)
        else:                # for (;;) { if (cond) break; body; if (--ctr == 0) break; }
            f.label(top)
            f.add(live_cmp(r)); f.br(r.choice(['bt', 'bf']), ex)
            stmts(depth + 1, loops + [(ex, ct)], budget)
            f.label(ct)
            f.add(dt(ctr)); f.br('bf', top)
            f.label(ex)
        if r.random() < 0.4: g.body(1)
    budget = [r.randrange(6, 16)]
    for _ in range(r.randrange(1, 3)):
        if budget[0] > 3 and r.random() < 0.8: loop(0, [], budget)
        else: stmts(0, [], budget)
    f.label(END)
    f.rts_slot = mov_(0, 1)
    return f


# ------------------------------------------------------------------------------------------------------------------------------------ 87 gotomess
def fam_gotomess(r):
    g = Gen(r)
    f = g.f
    nb = r.randrange(4, 11)
    f.add(mov_(0, 4), mov_(1, 5), movi(13, r.randrange(2, 7)))
    stubs = {}
    def tgt(i, t):
        # t is a block index or nb (END); backward or self edges go through a fuel stub
        if t == nb: return 'END'
        if t <= i:
            stubs.setdefault(t, 'S%d' % t)
            return stubs[t]
        return 'B%d' % t
    for i in range(nb):
        f.label('B%d' % i)
        if r.random() < 0.7: g.body(r.randrange(1, 3))
        if i == nb - 1: break
        k = r.random()
        if k < 0.15:
            continue                                         # fall through
        t = r.randrange(0, nb + 1)
        if t == i + 1 and r.random() < 0.7: t = r.randrange(0, nb + 1)
        if k < 0.30:
            f.br('bra', tgt(i, t)); f.add(NOP if r.random() < 0.7 else live_op(r)[0])
            continue
        if r.random() < 0.85: f.add(live_cmp(r))
        kind = r.choice(['bt', 'bf', 'bt', 'bf', 'bts', 'bfs'])
        f.br(kind, tgt(i, t))
        if kind in ('bts', 'bfs'):
            f.add(live_op(r)[0])
    f.label('B%d' % (nb - 1)) if False else None
    f.br('bra', 'END'); f.add(NOP)
    for t, name in sorted(stubs.items()):
        f.label(name)
        f.add(dt(13)); f.br('bt', 'END'); f.br('bra', 'B%d' % t); f.add(NOP)
    f.label('END')
    f.rts_slot = mov_(0, 1)
    return f


# ------------------------------------------------------------------------------------------------------------------------------------ 88 swloop
def build_swloop(seed):
    r = random.Random(seed * 7919 + 16)
    a = sw11.A()
    n = r.choice([2, 3, 4, 5, 6])
    bound = n - 1
    ctr = r.choice([2, 3, 4])
    a.w(0x6003 | 1 << 8 | 5 << 4)                      # mov r5,r1
    a.w(0xE000 | 8 << 8 | 0)                           # r8 = 0 (accumulator)
    a.w(0xE000 | 3 << 8 | ctr)                         # r3 = count
    a.label('top')
    a.w(0x6003 | 0 << 8 | 4 << 4)                      # mov r4,r0
    a.w(0x300C | 0 << 8 | 3 << 4)                      # add r3,r0   (the index varies with the iteration)
    if r.random() < 0.5: a.w(0xC900 | r.choice([3, 7, 1]))   # and #k,r0
    a.w(0xE000 | 2 << 8 | bound)
    a.w(0x3006 | 0 << 8 | 2 << 4)                      # cmp/hi r2,r0
    a.ref('bt', 'default')
    a.w(0x6003 | 2 << 8 | 0 << 4)
    a.w(0x4008 | 2 << 8)                               # shll2 r2
    a.ref('mova', 'table')
    a.w(0x000E | 0 << 8 | 2 << 4)                      # mov.l @(r0,r2),r0
    a.w(0x402B | 0 << 8); a.w(0x0009)                  # jmp @r0
    tgt = []; k = 0
    for i in range(n):
        x = r.random()
        if x < 0.15: tgt.append('default')
        elif i > 0 and x < 0.4 and tgt[-1] != 'default': tgt.append(tgt[-1])
        else: tgt.append('c%d' % k); k += 1
    if k == 0: tgt[0] = 'c0'; k = 1
    for c in range(k):
        a.label('c%d' % c)
        x = r.random()
        if x < 0.2: pass
        else:
            a.w(*r.choice([[0x7100 | r.choice([1, 2, 3, 0xFF])], [0x300C | 1 << 8 | 6 << 4], [0x3008 | 1 << 8 | 7 << 4], [0x200A | 1 << 8 | 3 << 4],
                           [0x7800 | r.choice([1, 5, 0xFE])], [0x6003 | 8 << 8 | 1 << 4]]))
        y = r.random()
        if y < 0.25: a.ref('bra', 'cont'); a.w(0x0009)           # continue
        elif y < 0.40: a.ref('bra', 'out'); a.w(0x0009)          # break
        elif y < 0.50: a.w(0x6003 | 0 << 8 | 1 << 4); a.w(0x000B); a.w(0x0009)   # return inside the loop
        elif y < 0.60: a.w(0xC800 | r.choice([1, 2])); a.ref('bt', 'out'); a.w(0x0009)   # break if the low index bits (of r0) are set; falls to the join
        else: a.ref('bra', 'join'); a.w(0x0009)
    a.label('default')
    a.w(0x7100 | r.choice([0x7F, 1, 0]))
    a.label('join')
    a.w(0x300C | 8 << 8 | 1 << 4) if r.random() < 0.5 else a.w(0x0009)
    a.label('cont')
    a.w(0x4010 | 3 << 8)                               # dt r3
    a.ref('bf', 'top')
    a.label('out')
    a.w(0x300C | 1 << 8 | 8 << 4)                      # add r8,r1
    a.w(0x6003 | 0 << 8 | 1 << 4); a.w(0x000B); a.w(0x0009)
    a.align4()
    a.label('table')
    for t in tgt: a.long(t)
    words, labels = sw11.assemble(a, [])
    return " ".join("%04X" % w for w in words)


# ------------------------------------------------------------------------------------------------------------------------------------ 89 condtree, 90 loopcond
def tree(r, depth):
    """('leaf', stmts, cmp) | ('and'|'or', l, r) | ('not', t)"""
    if depth == 0 or r.random() < 0.3:
        stm = live_op(r) if r.random() < 0.35 else []
        return ('leaf', stm, live_cmp(r))
    x = r.random()
    if x < 0.12: return ('not', tree(r, depth - 1))
    return (('and' if x < 0.56 else 'or'), tree(r, depth - 1), tree(r, depth - 1))


def comp(g, t, lt, lf, fall):
    """compile t with short-circuit branches. lt/lf the labels of the true/false outcome; fall is 'T' or 'F' (which outcome comes next in the code)"""
    f = g.f; r = g.r
    if t[0] == 'leaf':
        f.add(*t[1]); f.add(t[2])
        # T set means the compare is true (bt jumps when true)
        if fall == 'F': f.br('bt', lt)
        else: f.br('bf', lf)
    elif t[0] == 'not':
        comp(g, t[1], lf, lt, 'T' if fall == 'F' else 'F')
    elif t[0] == 'and':
        mid = g.lab('M')
        comp(g, t[1], mid, lf, 'T')
        f.label(mid)
        comp(g, t[2], lt, lf, fall)
    else:
        mid = g.lab('M')
        comp(g, t[1], lt, mid, 'F')
        f.label(mid)
        comp(g, t[2], lt, lf, fall)


def fam_condtree(r):
    g = Gen(r); f = g.f
    f.add(mov_(0, 4), mov_(1, 5))
    depth = r.randrange(1, 5)
    form = r.randrange(0, 6)
    t = tree(r, depth)
    if form == 0:      # if (t) A else B
        a, b, j = g.lab('A'), g.lab('B'), g.lab('J')
        comp(g, t, a, b, 'T')
        f.label(a); g.body(r.randrange(1, 3)); f.br('bra', j); f.add(NOP)
        f.label(b); g.body(r.randrange(1, 3))
        f.label(j)
    elif form == 1:    # if (t) A
        a, j = g.lab('A'), g.lab('J')
        comp(g, t, a, j, 'T')
        f.label(a); g.body(r.randrange(1, 3))
        f.label(j)
    elif form == 2:    # while (t) { body; --ctr }
        top, a, j = g.lab('W'), g.lab('A'), g.lab('J')
        f.add(movi(2, r.randrange(1, 4)))
        f.label(top)
        comp(g, t, a, j, 'T')
        f.label(a); g.body(r.randrange(1, 3)); f.add(dt(2)); f.br('bt', j); f.br('bra', top); f.add(NOP)
        f.label(j)
    elif form == 3:    # do { body } while (t && --ctr)   (the counter is the last leaf)
        top, a, j = g.lab('D'), g.lab('A'), g.lab('J')
        f.add(movi(2, r.randrange(1, 4)))
        f.label(top)
        g.body(r.randrange(1, 3))
        f.add(dt(2)); f.br('bt', j)
        comp(g, t, top, j, 'F')
        f.label(j)
    elif form == 4:    # if (t1) A else if (t2) B else C
        t2 = tree(r, r.randrange(1, 4))
        a, b, c, j, n1 = g.lab('A'), g.lab('B'), g.lab('C'), g.lab('J'), g.lab('N')
        comp(g, t, a, n1, 'T')
        f.label(a); g.body(1); f.br('bra', j); f.add(NOP)
        f.label(n1)
        comp(g, t2, b, c, 'T')
        f.label(b); g.body(1); f.br('bra', j); f.add(NOP)
        f.label(c); g.body(1)
        f.label(j)
    else:              # a loop whose body holds an if (t) with a break and a continue
        top, a, ex, ct = g.lab('L'), g.lab('A'), g.lab('X'), g.lab('C')
        f.add(movi(2, r.randrange(1, 4)))
        f.label(top)
        g.body(1)
        comp(g, t, a, ct, 'T')
        f.label(a)
        g.body(1)
        f.add(live_cmp(r)); f.br(r.choice(['bt', 'bf']), ex if r.random() < 0.5 else ct)
        f.label(ct)
        f.add(dt(2)); f.br('bf', top)
        f.label(ex)
    g.body(r.randrange(0, 2))
    f.rts_slot = mov_(0, 1)
    return f


def fam_loopcond(r):
    g = Gen(r); f = g.f
    f.add(mov_(0, 4), mov_(1, 5), movi(2, r.randrange(1, 5)))
    t = tree(r, r.randrange(1, 4))
    top, a, ex, ct, j = g.lab('L'), g.lab('A'), g.lab('X'), g.lab('C'), g.lab('J')
    f.label(top)
    if r.random() < 0.5: g.body(1)
    comp(g, t, a, ex, 'T')                         # the loop runs while t holds
    f.label(a)
    g.body(r.randrange(1, 3))
    if r.random() < 0.6:
        f.add(live_cmp(r)); f.br(r.choice(['bt', 'bf']), ex)      # an extra break
    f.label(ct)
    f.add(dt(2)); f.br('bf', top)
    f.label(ex)
    if r.random() < 0.5:                           # a second loop sharing the exit's code
        f.add(movi(3, r.randrange(1, 4)))
        l2 = g.lab('K')
        f.label(l2); g.body(1); f.add(dt(3)); f.br('bf', l2)
    g.body(1)
    f.rts_slot = mov_(0, 1)
    return f


FAMS16 = {86: fam_loopbrk, 87: fam_gotomess, 89: fam_condtree, 90: fam_loopcond}


def build(seed, fam=None, want_fn=False):
    if fam == 88:
        return build_swloop(seed)
    r = random.Random(seed * 104729 + 16)
    fm = FAMS16[fam] if fam is not None else FAMS16[r.choice(sorted(FAMS16))]
    f = fm(r)
    f.add(0x000B, getattr(f, 'rts_slot', NOP))
    if want_fn: return f
    return finish(f)


if __name__ == "__main__":
    if sys.argv[1] == 'show':
        f = build(int(sys.argv[2]), int(sys.argv[3]), want_fn=True)
        for c in f.w: print(c if isinstance(c, tuple) else "%04X" % c)
    else:
        first, count = int(sys.argv[1]), int(sys.argv[2])
        fam = int(sys.argv[3]) if len(sys.argv) > 3 else None
        out = [g for g in (build(s, fam) for s in range(first, first + count)) if g]
        print(";".join(out))
