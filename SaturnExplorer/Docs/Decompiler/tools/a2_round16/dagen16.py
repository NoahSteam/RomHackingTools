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
