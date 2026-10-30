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

W16 = int(os.environ.get('DAGEN16_W', '4'))      # the one width of every access of the memory families 91 to 93 (see mkptr16.py)
SINK = [12]
TD89 = int(os.environ.get('DAGEN16_TD', '4'))      # the depth of family 89's condition trees
NB87 = int(os.environ.get('DAGEN16_NB', '10'))     # the most blocks of family 87
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
    nb = r.randrange(4, NB87 + 1)
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
        elif y < 0.60: a.w(r.choice([0x4011, 0x4015]) | 1 << 8); a.ref(r.choice(['bt', 'bf']), 'out'); a.w(0x0009)   # break on the sign of r1; falls to the join
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
    depth = r.randrange(1, TD89 + 1)
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


# ------------------------------------------------------------------------------------------------------------------------------------ 91..93 memory
# Functions that read and write memory through r4 and r5 (pointers into a 16 KiB arena; r5 is r4, a neighbour of r4, or a second area), r6 and r7 data. They are
# run only by the memory mode of the printed-C differential (mkptr16.py, SE_GENERATED_PRINTEDC_PTR=1): the value returned and the arena's bytes afterwards
# must be the emulator's. Every access is aligned for its width (an address error is outside the domain), by a static offset tracker per pointer register.
class Mem:
    def __init__(self, r):
        self.r = r
        self.g = Gen(r)
        self.f = self.g.f
        self.off = {4: 0, 5: 0}          # the offset added to the base since the entry, bytes
        self.f.add(mov_(1, 6), mov_(2, 7), mov_(3, 7))

    def width(self):
        return W16

    def disp_ok(self, base, w, lim=15):
        """a displacement d (a multiple of w, in units of w up to lim) at which base + off + d*w is aligned"""
        for _ in range(8):
            d = self.r.randrange(0, lim + 1)
            if (self.off[base] + d * w) % w == 0 and self.off[base] % 4 == 0 or w == 1:
                return d
        return 0

    def load(self, dst=None):
        r = self.r; f = self.f
        base = r.choice([4, 5]); w = self.width()
        form = r.randrange(0, 10)
        if dst is None: dst = r.choice([0, 1, 2, 3])
        ext = {1: 0x6000, 2: 0x6001, 4: 0x6002}
        if form < 4:        # @(disp,Rm)
            if w == 4:
                d = r.randrange(0, 16)
                f.add(0x5000 | dst << 8 | base << 4 | d)
            else:
                d = r.randrange(0, 16) if w == 1 else r.randrange(0, 16)
                f.add({1: 0x8400, 2: 0x8500}[w] | base << 4 | d); dst = 0
        elif form < 6:      # @Rm
            f.add(ext[w] | dst << 8 | base << 4)
        elif form < 8:      # @(r0,Rm): r0 an index that keeps the alignment
            f.add(movi(0, r.choice([0, 4, 8, 12, 16, 20, 24, 28, 32])))
            f.add({1: 0x000C, 2: 0x000D, 4: 0x000E}[w] | dst << 8 | base << 4)
        elif form < 9:      # @Rm+ : the pointer moves by the width (kept 4-aligned by the caller using w=4, otherwise followed by a repair)
            f.add(ext[w] | dst << 8 | base << 4 | 4)
            self.off[base] += w
            self.fix(base)
        else:               # @-Rn is a store only; use the plain form
            f.add(ext[w] | dst << 8 | base << 4)
        return dst

    def fix(self, base):
        # bring the pointer back to a 4-aligned offset with add #imm (the engine sees an offset ADD)
        m = self.off[base] % 4
        if m:
            f = self.f
            f.add(0x7000 | base << 8 | ((4 - m) & 0xFF)); self.off[base] += 4 - m

    def store(self, src=None):
        r = self.r; f = self.f
        base = r.choice([4, 5]); w = self.width()
        form = r.randrange(0, 10)
        if src is None: src = r.choice([0, 1, 2, 3, 6, 7])
        if form < 4:        # @(disp,Rn)
            if w == 4:
                d = r.randrange(0, 16)
                f.add(0x1000 | base << 8 | src << 4 | d)
            else:
                d = r.randrange(0, 16)
                f.add({1: 0x8000, 2: 0x8100}[w] | base << 4 | d)
                # these store r0 only
        elif form < 6:      # @Rn
            f.add({1: 0x2000, 2: 0x2001, 4: 0x2002}[w] | base << 8 | src << 4)
        elif form < 8:
            f.add(movi(0, r.choice([0, 4, 8, 12, 16, 20, 24, 28, 32])))
            if src == 0: src = 1
            f.add({1: 0x0004, 2: 0x0005, 4: 0x0006}[w] | base << 8 | src << 4)
        elif form < 9:      # @-Rn
            f.add({1: 0x2004, 2: 0x2005, 4: 0x2006}[w] | base << 8 | src << 4)
            self.off[base] -= w
            self.fix(base)
        else:
            f.add({1: 0x2000, 2: 0x2001, 4: 0x2002}[w] | base << 8 | src << 4)

    def alu(self):
        r = self.r
        d = r.choice([0, 1, 2, 3]); s = r.choice([0, 1, 2, 3, 6, 7])
        self.f.add(*op2(r, d, s))

    def finish(self):
        r = self.r; f = self.f
        f.add(mov_(0, r.choice([1, 2, 3])))
        if r.random() < 0.5: f.add(add__(r.choice([1, 2, 3]), 0))
        f.rts_slot = NOP


def fam_memstraight(r):
    m = Mem(r)
    for _ in range(r.randrange(3, 10)):
        x = r.random()
        if x < 0.35: m.load()
        elif x < 0.65: m.store()
        elif x < 0.85: m.alu()
        else:
            # a conditional store or load: bt/bf over one to two memory ops
            m.f.add(cmp_word(r, r.choice([1, 2, 3]), r.choice([1, 2, 3, 6, 7])))
            l = m.g.lab('Z')
            m.f.br(r.choice(['bt', 'bf']), l)
            for _ in range(r.randrange(1, 3)):
                (m.store if r.random() < 0.5 else m.load)()
            m.f.label(l)
            # a pointer moved inside a conditional arm would have an unknown offset; the arms above use no post-increment of different widths
            m.off = {4: 0, 5: 0} if any(o != 0 for o in m.off.values()) and False else m.off
    m.finish()
    return m.f


def fam_memloop(r):
    # array loops: a counted walk of r4 (and r5) with a post-increment or pre-decrement of the element width, summing, copying, filling, or updating in place
    m = Mem(r); f = m.f
    w = W16
    n = r.randrange(2, 7)
    f.add(movi(8, n))
    form = r.randrange(0, 6)
    ld = {1: 0x6004, 2: 0x6005, 4: 0x6006}[w]            # mov.X @Rm+,Rn
    st = {1: 0x2000, 2: 0x2001, 4: 0x2002}[w]            # mov.X Rm,@Rn
    stdec = {1: 0x2004, 2: 0x2005, 4: 0x2006}[w]         # mov.X Rm,@-Rn
    if form == 3 or form == 4:
        f.add(0x7000 | 4 << 8 | (n * w) & 0xFF)          # start at the end: walk down
    f.add(movi(1, 0))
    top = m.g.lab('T'); f.label(top)
    if form == 0:           # sum: r1 += *p++
        f.add(ld | 2 << 8 | 4 << 4, add__(2, 1))
    elif form == 1:         # copy: *q++ = *p++
        f.add(ld | 2 << 8 | 4 << 4, st | 5 << 8 | 2 << 4, 0x7500 | w)
    elif form == 2:         # update in place: *p = *p + r6 (or ^ r7), then advance
        ldn = {1: 0x6000, 2: 0x6001, 4: 0x6002}[w]
        f.add(ldn | 2 << 8 | 4 << 4)
        f.add(add__(6, 2)) if r.random() < 0.5 else f.add(xor_(7, 2))
        f.add(st | 4 << 8 | 2 << 4, 0x7400 | w, add__(2, 1))
    elif form == 3:         # backwards sum with pre-decrement store of the sum
        f.add(0x7400 | (-w & 0xFF), {1: 0x6000, 2: 0x6001, 4: 0x6002}[w] | 2 << 8 | 4 << 4, add__(2, 1))
    elif form == 4:         # backwards store: *--q = i
        f.add(stdec | 4 << 8 | 8 << 4)
        f.add(add__(8, 1))
    else:                   # sum of products of two arrays
        f.add(ld | 2 << 8 | 4 << 4, ld | 3 << 8 | 5 << 4, 0x0007 | 2 << 8 | 3 << 4, 0x001A | 3 << 8, add__(3, 1))
    f.add(dt(8)); f.br('bf', top)
    m.f.add(mov_(0, 1))
    if r.random() < 0.4: m.load(); f.add(add__(r.choice([0, 1, 2, 3]), 0)) if False else None
    f.rts_slot = NOP
    return f


def fam_memstruct(r):
    # read-modify-write of the fields of a record at r4 (and a second record at r5): p->a += p->b; swap two fields; a flag updated by a mask; a field copied
    # from the other record (which may be the same record: the aliasing inputs). Every access has the width W16.
    m = Mem(r); f = m.f
    w = W16
    ldr0 = {1: 0x8400, 2: 0x8500}.get(w); str0 = {1: 0x8000, 2: 0x8100}.get(w)
    def ld(reg, p, idx):         # reg = p->field[idx] (idx in units of w, 0..15)
        return [0x5000 | reg << 8 | p << 4 | idx] if w == 4 else ([ldr0 | p << 4 | idx] + ([mov_(reg, 0)] if reg != 0 else []))
    def st(reg, p, idx):
        return [0x1000 | p << 8 | reg << 4 | idx] if w == 4 else ([mov_(0, reg)] if reg != 0 else []) + [str0 | p << 4 | idx]
    for _ in range(r.randrange(2, 7)):
        x = r.random()
        a = r.randrange(0, 8); b = r.randrange(0, 8)
        pa, pb = r.choice([4, 5]), r.choice([4, 5])
        if x < 0.35:         # pa->a += pb->b
            f.add(*ld(1, pb, b)); f.add(*ld(2, pa, a)); f.add(add__(1, 2)); f.add(*st(2, pa, a))
        elif x < 0.55:       # swap fields
            f.add(*ld(1, pa, a)); f.add(*ld(2, pb, b)); f.add(*st(2, pa, a)); f.add(*st(1, pb, b))
        elif x < 0.75:       # flags: read-modify-write with a mask
            f.add(*ld(2, pa, a)); f.add(r.choice([and__(6, 2), or__(7, 2), xor_(6, 2)])); f.add(*st(2, pa, a))
        elif x < 0.9:        # *pa = r6 ; accumulate pb->b
            f.add(*st(6, pa, a)); f.add(*ld(2, pb, b)); f.add(add__(2, 3))
        else:                # a pair of stores then a load of the first (an alias when pa == pb)
            f.add(*st(7, pa, a)); f.add(*st(6, pb, a)); f.add(*ld(2, pa, a)); f.add(add__(2, 3))
    f.add(mov_(0, 3), add__(1, 0)) if r.random() < 0.5 else f.add(mov_(0, 3))
    f.rts_slot = NOP
    return f


FAMS16.update({91: fam_memstraight, 92: fam_memloop, 93: fam_memstruct})


def fam_memarr(r):
    # computed addresses: an index from data (masked to the arena, a multiple of the width), r0-indexed loads and stores, a pointer bumped by a variable amount,
    # the distance between two pointers (the same number for the emulator and for the compiled text, unlike a pointer), pointer compares and a pointer walked by a loop
    # that is read back as a count (the pointer minus its start)
    m = Mem(r); f = m.f
    w = W16
    mask = {4: 0x3C, 2: 0x3E, 1: 0x3F}[w]
    ldx = {1: 0x000C, 2: 0x000D, 4: 0x000E}[w]          # mov.X @(r0,Rm),Rn
    stx = {1: 0x0004, 2: 0x0005, 4: 0x0006}[w]          # mov.X Rm,@(r0,Rn)
    ldp = {1: 0x6000, 2: 0x6001, 4: 0x6002}[w]          # mov.X @Rm,Rn
    stp = {1: 0x2000, 2: 0x2001, 4: 0x2002}[w]          # mov.X Rm,@Rn
    f.add(mov_(8, 4))                                   # the start of the walk, for the count
    def index(reg, src):                                # reg = (src & mask), an offset that keeps the alignment
        f.add(mov_(reg, src), mov_(0, reg), andi(mask), mov_(reg, 0))
    for _ in range(r.randrange(2, 7)):
        x = r.random()
        base = r.choice([4, 5])
        if x < 0.25:        # r3 = base[idx]
            index(1, r.choice([6, 7, 2, 3])); f.add(mov_(0, 1), ldx | 3 << 8 | base << 4)
        elif x < 0.45:      # base[idx] = data
            index(1, r.choice([6, 7, 2, 3])); f.add(mov_(0, 1), stx | base << 8 | r.choice([2, 6, 7]) << 4)
        elif x < 0.6:       # bump the pointer by a computed amount: r4 += (data & mask), loads through it
            index(1, r.choice([6, 7, 3])); f.add(add__(1, base))
            f.add(ldp | 2 << 8 | base << 4)
        elif x < 0.72:      # load through the pointer then store a neighbour
            f.add(ldp | 3 << 8 | base << 4, add__(3, 2), 0x7000 | base << 8 | w, stp | base << 8 | 2 << 4)
        elif x < 0.84:      # compare the two pointers and branch around a store
            f.add(r.choice([cmphi, cmphs, cmpgt, cmpeq])(4, 5))
            l = m.g.lab('Z'); f.br(r.choice(['bt', 'bf']), l)
            f.add(stp | 5 << 8 | 6 << 4)
            f.label(l)
        else:               # data from the distance between the pointers
            f.add(mov_(3, 5), sub__(4, 3), add__(3, 2))
    # the result: the pointer's walk, data, or both
    k = r.randrange(0, 3)
    if k == 0: f.add(mov_(0, 4), sub__(8, 0))                # r4 - start
    elif k == 1: f.add(mov_(0, 3), add__(2, 0))
    else: f.add(mov_(0, 4), sub__(8, 0), add__(3, 0))
    f.rts_slot = NOP
    return f


FAMS16[94] = fam_memarr


def fam_ptree(r):
    # Expression trees for the printer (depth 2 to 6, each temporary used once so that the engine inlines the whole tree into one expression and the printer decides
    # every parenthesis, cast and negative constant): the binary add, sub, and, or, xor, mul, muls.w, mulu.w, the high word of dmuls/dmulu, xtrct, the unary neg, not,
    # extensions, swaps, constant shifts and rotates, add #imm, T-valued nodes (compares of two subtrees, tst, cmp/str, cmp/pz and cmp/pl, then movt or negc) combined with
    # and, or, xor and neg, and selects (a diamond whose arms are subtrees, picked by a compare of two subtrees: the mid-tree branch). Constants are the signed edge values.
    g = Gen(r); f = g.f
    pool = [1, 2, 3, 8, 9, 10, 11, 12, 13, 14]
    BIN = [add__, sub__, and__, or__, xor_, add__, sub__]
    EDGE = [0, 1, 2, 3, 0x7F, 0x80, 0xFF, 0x100, 0x7FFF, 0x8000, 0xFFFF, 0x10000, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF, 0xFFFFFFFE, 0xFFFFFF80, 0xFFFF8000, 0x55555555, r.getrandbits(32)]
    CMP = [cmpeq, cmphi, cmphs, cmpgt, cmpge]
    def leaf(d):
        x = r.random()
        if x < 0.3: f.lit(d, r.choice(EDGE))
        elif x < 0.4: f.add(movi(d, r.choice([0, 1, 2, 7, 0xFF, 0x80, 0xFE])))
        else: f.add(mov_(d, r.choice([4, 5, 6, 7])))
    def gen(depth, k):
        d = pool[k]
        if depth == 0 or r.random() < 0.12 or k + 2 >= len(pool):
            leaf(d); return
        x = r.random()
        if x < 0.40:
            gen(depth - 1, k); gen(depth - 1, k + 1)
            e = pool[k + 1]; y = r.random()
            if y < 0.55: f.add(r.choice(BIN)(e, d))
            elif y < 0.65: f.add(mulL(e, d), stsmacl(d))
            elif y < 0.72: f.add(r.choice([0x200F, 0x200E]) | d << 8 | e << 4, stsmacl(d))
            elif y < 0.80: f.add(r.choice([dmulu, dmuls])(e, d), stsmach(d))
            elif y < 0.86: f.add(xtrct(e, d))
            else: f.add(r.choice(CMP)(e, d), movt(d))
        elif x < 0.60:        # unary
            gen(depth - 1, k); y = r.random()
            if y < 0.2: f.add(neg_(d, d))
            elif y < 0.35: f.add(not_(d, d))
            elif y < 0.55: f.add(r.choice([extub, extuw, extsb, extsw, swapw_, swapb_])(d, d))
            elif y < 0.8: f.add(r.choice([SHLL, SHLR, SHAR, 0x4008, 0x4009, 0x4018, 0x4019, 0x4028, 0x4029, ROTL, ROTR]) | d << 8)
            else: f.add(addi(d, r.choice([1, 0xFF, 0xFE, 2, 0x7F, 0x80])))
        elif x < 0.75:        # a T-valued node
            y = r.random()
            if y < 0.5:
                gen(depth - 1, k); gen(depth - 1, k + 1); e = pool[k + 1]
                f.add(r.choice(CMP + [tstr, cmp_str])(e, d))
            else:
                gen(depth - 1, k); f.add(r.choice([cmppz, cmppl])(d))
            z = r.random()
            if z < 0.7: f.add(movt(d))
            elif z < 0.85: f.add(movt(d), neg_(d, d))
            else: f.add(movt(d), 0x4010 | d << 8)         # dt: T = (d - 1 == 0)
        elif x < 0.88 and k + 3 < len(pool):   # a boolean combination of two T values
            gen(depth - 1, k); f.add(r.choice(CMP)(r.choice([4, 5, 6, 7]), d), movt(d))
            gen(depth - 1, k + 1); e = pool[k + 1]; f.add(r.choice(CMP)(r.choice([4, 5, 6, 7]), e), movt(e))
            f.add(r.choice([and__, or__, xor_])(e, d))
        else:                  # select: (c1 cmp c2) ? a : b
            if k + 4 >= len(pool): leaf(d); return
            gen(depth - 1, k + 1); gen(depth - 1, k + 2)
            f.add(r.choice(CMP)(pool[k + 2], pool[k + 1]))
            a, b = g.lab('S'), g.lab('E')
            f.br(r.choice(['bt', 'bf']), a)
            gen(depth - 1, k + 3 if k + 3 < len(pool) else k); f.add(mov_(d, pool[k + 3 if k + 3 < len(pool) else k]))
            f.br('bra', b); f.add(NOP)
            f.label(a)
            gen(depth - 1, k + 3 if k + 3 < len(pool) else k); f.add(mov_(d, pool[k + 3 if k + 3 < len(pool) else k]))
            f.label(b)
    gen(r.randrange(2, 7), 0)
    if r.random() < 0.35:       # the tree as the condition of a branch whose arms return different things
        f.add(cmppz(pool[0]) if r.random() < 0.5 else r.choice(CMP)(4, pool[0]))
        l = g.lab('Q'); f.br(r.choice(['bt', 'bf']), l)
        f.add(mov_(pool[0], 5)); f.label(l)
    f.add(mov_(1, pool[0]))
    f.rts_slot = mov_(0, 1)
    return f


FAMS16[95] = fam_ptree


def fam_forloop(r):
    # loops the engine can print as `for`: a counter initialised before the loop, tested at the top, stepped by one op at the end of the last block; the body has
    # conditional jumps to the stepping block (a "continue"), breaks, a nested for loop, an early return, and sometimes a second op in the stepping block (which keeps it
    # a while), the step reading a data register, the test an unsigned or a signed compare, and the counter read after the loop.
    g = Gen(r); f = g.f
    f.add(mov_(0, 4), mov_(1, 5))
    def loop(depth, outer):
        ctr = CTR[depth]; lim = [10, 11, 13][depth]
        top, ex, ct = g.lab('T'), g.lab('X'), g.lab('C')
        f.add(mov_(0, r.choice([6, 7])), andi(r.choice([3, 7])), mov_(lim, 0))
        f.add(movi(ctr, r.choice([0, 0, 1, 0xFF])))
        f.label(top)
        cmpk = r.randrange(0, 3)
        if cmpk == 0: f.add(cmpge(lim, ctr)); f.br('bt', ex)           # i >= lim: exit
        elif cmpk == 1: f.add(cmphs(lim, ctr)); f.br('bt', ex)         # unsigned
        else: f.add(cmpgt(ctr, lim)); f.br('bf', ex)                   # lim > i continues
        for _ in range(r.randrange(1, 4)):
            x = r.random()
            if x < 0.4: g.body(1)
            elif x < 0.6:
                f.add(live_cmp(r)); f.br(r.choice(['bt', 'bf']), ct)             # continue
            elif x < 0.75:
                f.add(live_cmp(r)); f.br(r.choice(['bt', 'bf']), ex)             # break
            elif x < 0.85 and depth < 1 and not outer:
                loop(depth + 1, True)
            elif x < 0.92:
                f.add(live_cmp(r)); f.br(r.choice(['bt', 'bf']), 'END')          # return
            else:
                g.body(2)
        f.label(ct)
        st = r.random()
        if st < 0.5: f.add(addi(ctr, 1))
        elif st < 0.7: f.add(addi(ctr, 2))
        elif st < 0.85: f.add(addi(ctr, 1)); f.add(*live_op(r)[:1])           # a second op in the stepping block
        else: f.add(addi(ctr, 3))
        f.br('bra', top); f.add(NOP)
        f.label(ex)
        if r.random() < 0.5: f.add(add__(ctr, r.choice([0, 1])))               # the counter read after the loop
    loop(0, False)
    if r.random() < 0.4: loop(0, False)
    f.label('END')
    f.rts_slot = mov_(0, 1)
    return f


FAMS16[96] = fam_forloop


def fam_memmac(r):
    # mac.l / mac.w @Rm+,@Rn+ over arrays (dot products: straight-line or a counted loop), clrmac or the entry MACH:MACL, the S bit as the entry SR has it, the
    # accumulator read as macl, mach, both, or stored; r5 may alias r4's array. The widths 4 (mac.l) and 2 (mac.w); w = 1 has no mac and uses the plain loop instead.
    m = Mem(r); f = m.f
    w = W16
    if w == 1:
        return fam_memloop(r)
    mac = 0x000F if w == 4 else 0x400F                  # mac.l @Rm+,@Rn+ is 0x0nmF; mac.w is 0x4nmF
    inc = w
    if r.random() < 0.6: f.add(0x0028)                  # clrmac
    n = r.randrange(1, 6)
    if r.random() < 0.5:                                # straight line
        for _ in range(n): f.add(mac | 5 << 8 | 4 << 4)   # n = r5 (first operand, the multiplicand pair is @Rn+ x @Rm+): mac.X @r4+,@r5+
    else:
        top = m.g.lab('M')
        f.add(movi(8, n)); f.label(top); f.add(mac | 5 << 8 | 4 << 4, dt(8)); f.br('bf', top)
    if r.random() < 0.3:
        f.add(0x2002 | 4 << 8 | 7 << 4) if w == 4 else f.add(0x2001 | 4 << 8 | 7 << 4)   # a store through the advanced pointer
    k = r.randrange(0, 4)
    if k == 0: f.add(0x001A | 0 << 8)                   # sts macl,r0
    elif k == 1: f.add(0x000A | 0 << 8)                 # sts mach,r0
    elif k == 2: f.add(0x001A | 0 << 8, 0x000A | 1 << 8, add__(1, 0))
    else: f.add(0x001A | 1 << 8, 0x000A | 0 << 8, 0x4000 | 0 << 8, xor_(1, 0))
    f.rts_slot = NOP
    return f


FAMS16[97] = fam_memmac


def fam_edgeconst(r):
    # one or two operations of a register with an edge constant from the literal pool (the signed and unsigned limits of 8, 16 and 32 bits, their neighbours and
    # negatives), then a compare with a second edge constant (all five compares and both senses) read as movt, as a branch around an assignment, or as a returned
    # difference: the printer's sign, hex or decimal, `-0x80000000` and unsigned-versus-signed choices for every constant, and the engine's folds of the pair.
    g = Gen(r); f = g.f
    E = [0, 1, 2, 3, 0x7F, 0x80, 0x81, 0xFF, 0x100, 0x7FFF, 0x8000, 0x8001, 0xFFFF, 0x10000, 0x7FFFFFFE, 0x7FFFFFFF, 0x80000000, 0x80000001, 0xFFFFFFFE, 0xFFFFFFFF,
         0xFFFFFF00, 0xFFFFFF80, 0xFFFF8000, 0xFFFF0000, 0xFFFF7FFF, 0x55555555, 0xAAAAAAAA, r.getrandbits(32)]
    f.add(mov_(1, r.choice([4, 5, 6, 7])), mov_(2, r.choice([4, 5, 6, 7])))
    for _ in range(r.randrange(1, 3)):
        f.lit(3, r.choice(E))
        k = r.randrange(0, 14)
        d = 1
        if k == 0: f.add(add__(3, d))
        elif k == 1: f.add(sub__(3, d))
        elif k == 2: f.add(and__(3, d))
        elif k == 3: f.add(or__(3, d))
        elif k == 4: f.add(xor_(3, d))
        elif k == 5: f.add(mulL(3, d), stsmacl(d))
        elif k == 6: f.add(neg_(d, d), add__(3, d))
        elif k == 7: f.add(not_(d, d), sub__(3, d))
        elif k == 8: f.add(r.choice([extsb, extsw, extub, extuw])(d, d), add__(3, d))
        elif k == 9: f.add(r.choice([SHLL, SHLR, SHAR, 0x4008, 0x4009, 0x4018, 0x4019, 0x4028, 0x4029, ROTL, ROTR]) | d << 8, add__(3, d))
        elif k == 10: f.add(sub__(d, 3), mov_(d, 3))
        elif k == 11: f.add(r.choice([addc, subc, addv, subv])(3, d))
        elif k == 12: f.add(dmuls(3, d) if r.random() < 0.5 else dmulu(3, d), stsmach(d))
        else: f.add(0x6000 | d << 8 | 3 << 4 | 0xB)
    f.lit(3, r.choice(E))
    c = r.randrange(0, 4)
    cmps = [cmpeq, cmphi, cmphs, cmpgt, cmpge]
    if c == 0:
        f.add(r.choice(cmps)(3, 1), movt(0))
        if r.random() < 0.4: f.add(neg_(0, 0))
    elif c == 1:
        f.add(r.choice(cmps)(3, 1)); l = g.lab('Z'); f.br(r.choice(['bt', 'bf']), l)
        f.lit(1, r.choice(E)); f.label(l); f.add(mov_(0, 1))
    elif c == 2:
        f.add(r.choice(cmps)(3, 2), movt(0)); f.add(r.choice(cmps)(3, 1), movt(2)); f.add(r.choice([add__, sub__, xor_, or__, and__])(2, 0))
    else:
        f.add(mov_(0, 1), sub__(3, 0)) if r.random() < 0.5 else f.add(mov_(0, 1), add__(3, 0))
    f.rts_slot = NOP
    return f


FAMS16[98] = fam_edgeconst


def fam_tnarrow(r):
    # A2-104's shape: a sum or difference that carries T (addc, subc, negc, or a movt added), cut to a byte or a word by an extension, compared for equality or order
    # with a constant of that width (so that the engine's compare is two or one bytes wide and the constant is a negative or a high one)
    g = Gen(r); f = g.f
    f.add(mov_(1, r.choice([4, 5, 6, 7])), mov_(2, r.choice([4, 5, 6, 7])))
    if r.random() < 0.3: f.add(r.choice([cmpeq, cmphi, cmpgt])(r.choice([4, 5]), r.choice([6, 7])))      # a T of the function's own
    k = r.randrange(0, 5)
    if k == 0: f.add(addc(2, 1))
    elif k == 1: f.add(subc(2, 1))
    elif k == 2: f.add(negc(1, 1))
    elif k == 3: f.add(movt(3), add__(3, 1))
    else: f.add(movt(3), sub__(3, 1))
    ext = r.choice([extsw, extsw, extsb, extuw, extub])
    f.add(ext(1, 1))
    c = r.choice([0xFFFF, 0xFF, 0x7FFF, 0x80, 0x8000, 0xFFFFFFFF, 0xFFFFFF80, 0xFFFF8000, 0x7F, 0, 1, 0x100, 0x10000])
    if r.random() < 0.5:
        bias = r.choice([0x8000, 0x80, 0x80000000, 1, 0xFFFFFFFF])
        f.lit(3, bias); f.add(add__(3, 1)); f.lit(3, (c + bias) & 0xFFFFFFFF)
    else:
        f.lit(3, c)
    f.add(r.choice([cmpeq, cmpeq, cmphi, cmphs, cmpgt, cmpge])(3, 1), movt(0))
    if r.random() < 0.5: f.add(neg_(0, 0))
    f.rts_slot = NOP
    return f


FAMS16[99] = fam_tnarrow


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
