#!/usr/bin/env python3
# Area 2 round 10 instrument (dagen.py of round 9, families 0..11 unchanged, 12..17 new):
# 12 loopadd, 13 loopor (two accumulators), 14 loopmix, 15 nested loops, 16 loopsel (conditional update), 17 loopmask (masked accumulator / checksum / crc shapes), 18 tworet (two returns)
# Area 2 round 9 instrument: register-only functions with structure the round 8 generators did not draw: a value computed twice (the CSE
# merge of A2-56), diamonds and counted loops (MULTIEQUAL, cycles through it), copies that make partial results shared, and a mask,
# shift pair or extension read after.
# usage: dagen.py <first seed> <count> [family 0..18]   -> one function per seed, ";" joined (SE_GENERATED_PRINTEDC_WORDS / fixtures)
# families: 0 twice, 1 sum, 2 diamond, 3 loop, 4 chain, 5 stack, 6 sign (T-flag idioms), 7 join (A2-58), 8 join2 (A2-58), 9 join3 (A2-58), 10 loopsum, 11 loopand (A2-59)
import random, sys

CONSTS = [0, 1, 2, 3, 4, 5, 6, 8, 12, 0x10, 0x1C, 0x20, 0x7F, 0x80, 0xFF, 0x100, 0xFFFC, 0xFFFF, 0x10000, 0xFFFFFFFC, 0xFFFFFFF8,
          0xFFFFFF00, 0xFFFFFFF0, 0xFFFF0000, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF, 0x06009008, 0x0600900E, 0x00FF00FF, 0x12345678,
          0xFFFFFFFE, 0x55555555, 0x3FFFFFFC, 0xFFFFFFFA, 0x1E, 0x0F]

class Fn:
    def __init__(self, r):
        self.r = r
        self.w = []          # words or ('lit', reg, value) or ('br', kind, label) or ('label', name)
    def add(self, *x):
        self.w.extend(x)
    def lit(self, reg, v=None):
        if v is None: v = self.r.choice(CONSTS)
        self.w.append(('lit', reg, v))
    def label(self, name): self.w.append(('label', name))
    def br(self, kind, name): self.w.append(('br', kind, name))

def finish(f):
    # resolve labels and pool
    code = []
    labels = {}
    for c in f.w:
        if isinstance(c, tuple) and c[0] == 'label': labels[c[1]] = len(code)
        else: code.append(c)
    n = len(code)
    pool_at = (n * 2 + 3) & ~3
    pool = []
    words = []
    for i, c in enumerate(code):
        if isinstance(c, tuple) and c[0] == 'lit':
            _, reg, val = c
            if val not in pool: pool.append(val)
            d = (pool_at + 4 * pool.index(val) - (((i * 2) + 4) & ~3)) // 4
            if d < 0 or d > 255: return None
            words.append(0xD000 | reg << 8 | d)
        elif isinstance(c, tuple) and c[0] == 'br':
            _, kind, name = c
            d = labels[name] - (i + 2)
            if d < -128 or d > 127: return None
            if kind == 'bf': words.append(0x8B00 | (d & 0xFF))
            elif kind == 'bt': words.append(0x8900 | (d & 0xFF))
            elif kind == 'bfs': words.append(0x8F00 | (d & 0xFF))
            elif kind == 'bts': words.append(0x8D00 | (d & 0xFF))
            elif kind == 'bra':
                if d < -2048 or d > 2047: return None
                words.append(0xA000 | (d & 0xFFF))
        else:
            words.append(c)
    while len(words) * 2 < pool_at: words.append(0x0009)
    for v in pool: words += [v >> 16, v & 0xFFFF]
    return " ".join("%04X" % x for x in words)

SH1 = [0x4000, 0x4001, 0x4008, 0x4009, 0x4018, 0x4019, 0x4028, 0x4029, 0x4021, 0x4004, 0x4005]
def op2(r, d, s):
    """one register-register or unary op writing rd (d), reading s"""
    k = r.randrange(0, 40)
    if k < 6: return [0x300C | d << 8 | s << 4]                       # add
    if k < 9: return [0x3008 | d << 8 | s << 4]                       # sub
    if k < 12: return [0x2009 | d << 8 | s << 4]                      # and
    if k < 14: return [0x200B | d << 8 | s << 4]                      # or
    if k < 16: return [0x200A | d << 8 | s << 4]                      # xor
    if k < 17: return [0x600B | d << 8 | s << 4]                      # neg
    if k < 18: return [0x6007 | d << 8 | s << 4]                      # not
    if k < 21: return [r.choice([0x600C, 0x600D, 0x600E, 0x600F, 0x6008, 0x6009]) | d << 8 | s << 4]
    if k < 22: return [0x200D | d << 8 | s << 4]                      # xtrct
    if k < 28: return [r.choice(SH1) | d << 8 for _ in range(r.randrange(1, 3))]
    if k < 31: return [r.choice([0x0007, 0x200E, 0x200F]) | d << 8 | s << 4, 0x001A | d << 8]   # mul + sts macl
    if k < 33: return [0x7000 | d << 8 | r.choice([1, 2, 3, 4, 8, 0xFC, 0xF8, 0xFF, 0xFE, 0x7F, 0x80])]  # add #imm
    if k < 35: return [0x6003 | d << 8 | s << 4]                      # mov
    if k < 36: return [0x4010 | d << 8]                               # dt
    if k < 37: return [0x4009 | d << 8, 0x4008 | d << 8]              # shlr2 shll2
    if k < 38: return [0x4019 | d << 8, 0x4018 | d << 8]
    if k < 39: return [0x4001 | d << 8, 0x4000 | d << 8]
    return [0x4021 | d << 8, 0x4020 | d << 8]

def tbool(r, f, d):
    a, b = r.randrange(0, 8), r.randrange(0, 8)
    k = r.randrange(0, 9)
    f.add([0x3000, 0x3003, 0x3007, 0x3006, 0x3002][k % 5] | a << 8 | b << 4 if k < 5 else [0x4015 | a << 8, 0x4011 | a << 8, 0x2008 | a << 8 | b << 4, 0x200C | a << 8 | b << 4][k - 5])
    f.add(0x0029 | d << 8)    # movt d
    if r.random() < 0.5: f.add(0x600B | d << 8 | d << 4)   # neg d,d

def tail(r, f, d):
    """read d through a mask / shift pair / extension, moving the result to r0"""
    t = r.randrange(0, 14)
    if d != 0: f.add(0x6003 | d << 4)         # mov rd,r0
    if t == 0: f.add(0xC9FC)
    elif t == 1: f.add(0xC9F8)
    elif t == 2: f.add(0x4009, 0x4008)
    elif t == 3: f.add(0x600D)
    elif t == 4: f.add(0x600C)
    elif t == 5: f.add(0x4019, 0x4018)
    elif t == 6: f.add(0xC903)
    elif t == 7: f.add(0x4009, 0x4009, 0x4008, 0x4008)
    elif t == 8: f.lit(1, r.choice([0xFFFFFFFC, 0xFFFFFFF8, 0xFFFFFFF0, 0xFFFFFFFE, 0xFFFFFF00, 0xFFFF0000])); f.add(0x2019)
    elif t == 9: f.add(0x600F)
    elif t == 10: f.add(0x600E)
    elif t == 11: f.add(0xC93F)
    elif t == 12: f.add(0x4001, 0x4000)
    else: pass
    return

def body(r, f, n, regs, allow_t=True):
    for _ in range(n):
        d = r.choice(regs); s = r.choice(regs + [4, 5, 6, 7])
        if allow_t and r.random() < 0.12:
            tbool(r, f, d)
        elif r.random() < 0.10:
            e = r.choice(regs)
            f.lit(e)
            f.add(r.choice([0x300C, 0x2009, 0x200B, 0x200A, 0x3008]) | d << 8 | e << 4)
        else:
            f.add(*op2(r, d, s))

def fam_twice(r, f):
    """the same value computed twice, one read masked and one whole, or one read as byte and one wide"""
    src = r.choice([4, 5, 6, 7])
    seq = []
    k = r.randrange(0, 5)
    for _ in range(r.randrange(1, 4)):
        seq.append(r.choice([lambda d: [0x4009 | d << 8], lambda d: [0x4008 | d << 8], lambda d: [0x4019 | d << 8], lambda d: [0x4001 | d << 8],
                             lambda d: [0x4000 | d << 8], lambda d: [0x7000 | d << 8 | r.choice([1, 2, 4, 8, 0xFC])],
                             lambda d: [0x600C | d << 8 | d << 4], lambda d: [0x600D | d << 8 | d << 4], lambda d: [0x6008 | d << 8 | d << 4],
                             lambda d: [0x600B | d << 8 | d << 4], lambda d: [0x4008 | d << 8, 0x4008 | d << 8]]))
    cs = r.choice([None, 0xFFFFFFFC, 0x06009008, 0xFF, 0x1C])
    rr = r.sample([1, 2, 3], 2)
    ws = []
    for d in rr:
        ws.append(0x6003 | d << 8 | src << 4)
        s2 = [x(d) for x in seq]
        for q in s2: ws.extend(q)
    # interleave or not: either all of d1 then d2 (CSE across the block) or alternate
    f.add(*ws)
    a, b = rr
    # a read through a mask, b whole, combined
    f.add(0x6003 | 0 << 8 | a << 4)
    f.add(r.choice([0xC901, 0xC903, 0xC9FC, 0xC91C, 0xC9FF, 0xC9F0, 0xC980]))
    f.add(r.choice([0x300C, 0x3008, 0x200B, 0x200A, 0x2009]) | 0 << 8 | b << 4)
    if r.random() < 0.5: tail(r, f, 0)

def fam_sum(r, f):
    """a sum of scaled terms over shared variables read twice, partial sums kept in registers"""
    vs = [4, 5, 6, 7]
    f.lit(0, r.choice([0, 0x06009008, 4, 8, 0x100, 0xFFFFFFFC, 0x10]))
    keep = []
    for t in range(r.randrange(2, 6)):
        v = r.choice(vs); d = r.choice([1, 2, 3])
        c = r.choice([1, 2, 3, 4, 5, 6, 8, 12, 14, 0x10, 0x12, 0x16, 0xFFFFFFFC, 0xFFFFFFFE, 0xFFFFFFFF])
        f.lit(d, c); f.add(0x0007 | d << 8 | v << 4, 0x001A | d << 8)
        f.add(r.choice([0x300C, 0x300C, 0x3008]) | 0 << 8 | d << 4)
        if r.random() < 0.3:
            k = r.choice([5, 6, 7]) if v != 5 else 6
            f.add(0x6003 | k << 8 | 0 << 4)       # keep the partial sum in a register
            keep.append(k)
    if keep and r.random() < 0.7:
        # read a partial sum through a mask
        k = r.choice(keep)
        f.add(0x6003 | 1 << 8 | k << 4, r.choice([0x4009, 0x4019, 0x4001]) | 1 << 8, r.choice([0x4008, 0x4018, 0x4000]) | 1 << 8)
        f.add(r.choice([0x301C, 0x3018, 0x201A, 0x201B]))   # add/sub/xor/or r1,r0
    tail(r, f, 0)

def fam_diamond(r, f):
    """an if/else diamond; both arms compute from the inputs, the join reads through a mask"""
    body(r, f, r.randrange(1, 4), [0, 1, 2, 3])
    tbool(r, f, 3) if r.random() < 0.3 else f.add(r.choice([0x3047, 0x3043, 0x3046, 0x3040, 0x4515, 0x2458, 0x245C]))
    f.br(r.choice(['bf', 'bt']), 'else')
    body(r, f, r.randrange(1, 5), [0, 1, 2, 3])
    f.br('bra', 'end'); f.add(0x0009)
    f.label('else')
    body(r, f, r.randrange(1, 5), [0, 1, 2, 3])
    f.label('end')
    body(r, f, r.randrange(0, 3), [0, 1, 2, 3])
    tail(r, f, r.choice([0, 1, 2, 3]))

def fam_loop(r, f):
    """a counted loop with a few ops on accumulators; cycles through MULTIEQUAL"""
    f.add(0xE600 | r.randrange(1, 5))          # mov #n,r6   (counter)
    f.add(0xE100 | r.choice([0, 1, 2, 4, 0xFC, 0x7F]))   # mov #c,r1
    if r.random() < 0.5: f.add(0x6003 | 2 << 8 | 4 << 4)
    f.label('top')
    body(r, f, r.randrange(1, 5), [1, 2, 3, 0], allow_t=False)
    f.add(0x4610)                              # dt r6
    f.br('bf', 'top')
    body(r, f, r.randrange(0, 3), [0, 1, 2, 3])
    tail(r, f, r.choice([0, 1, 2, 3]))

def fam_chain(r, f):
    body(r, f, r.randrange(4, 18), [0, 1, 2, 3, 4, 5], allow_t=True)
    tail(r, f, r.choice([0, 1, 2, 3, 4, 5]))

def fam_stack(r, f):
    """locals in the frame written at one width and read at another (pieces), then arithmetic on the reads; reads only of written bytes"""
    size = r.choice([8, 12, 16])
    written = set()
    f.add(0x7F00 | ((-size) & 0xFF))                 # add #-size,r15
    for _ in range(r.randrange(2, 7)):
        w = r.choice([1, 2, 4, 4])
        o = r.randrange(0, size // w) * w
        src = r.choice([0, 4, 5, 6, 7])
        if r.random() < 0.4:
            p = r.choice([1, 2, 3])
            if src == p: src = 4
            f.add(0x6003 | p << 8 | 15 << 4, 0x7000 | p << 8 | o)
            f.add({1: 0x2000, 2: 0x2001, 4: 0x2002}[w] | p << 8 | src << 4)
        elif w == 4:
            f.add(0x1000 | 15 << 8 | src << 4 | o // 4)          # mov.l Rm,@(d,r15)
        else:
            if src != 0: f.add(0x6003 | 0 << 8 | src << 4)
            f.add((0x8000 if w == 1 else 0x8100) | 15 << 4 | (o // w))   # mov.b/w r0,@(d,Rn=r15)
        for q in range(o, o + w): written.add(q)
        if r.random() < 0.3:
            f.add(*op2(r, r.choice([4, 5, 6, 7]), r.choice([4, 5, 6, 7])))
    for _ in range(r.randrange(1, 4)):
        cand = [(w, o) for w in (1, 2, 4) for o in range(0, size, w) if all(q in written for q in range(o, o + w))]
        if not cand: break
        w, o = r.choice(cand)
        d = r.choice([0, 0, 1, 2, 3]) if w == 4 else 0
        if w == 4:
            f.add(0x5000 | d << 8 | 15 << 4 | o // 4)           # mov.l @(d,r15),Rd
        else:
            f.add((0x8400 if w == 1 else 0x8500) | 15 << 4 | (o // w))   # mov.b/w @(d,Rm),r0
            d = 0
        if r.random() < 0.6:
            f.add(*op2(r, d, r.choice([0, 4, 5, 6, 7, d])))
        if d != 0 and r.random() < 0.5: f.add(0x300C | 0 << 8 | d << 4)
    tail(r, f, 0)
    f.add(0x7F00 | size)                                # add #size,r15

def fam_sign(r, f):
    """T-flag idioms: sign masks (shll ; subc), carries (addc / negc / subc chains), movt after shifts and rotates, div0s, then arithmetic on them"""
    body(r, f, r.randrange(0, 3), [0, 1, 2, 3])
    for _ in range(r.randrange(2, 7)):
        d = r.choice([0, 1, 2, 3]); s = r.choice([4, 5, 6, 7, 0, 1, 2, 3]); e = r.choice([0, 1, 2, 3, 4, 5, 6, 7])
        k = r.randrange(0, 14)
        if k < 2:   f.add(0x6003 | d << 8 | s << 4, 0x4000 | d << 8, 0x300A | (d << 8) | (d << 4))                  # mov s,d ; shll d ; subc d,d  = -msb
        elif k < 3: f.add(0x6003 | d << 8 | s << 4, 0x4000 | d << 8, 0x0029 | d << 8)                                      # msb
        elif k < 4: f.add(0x6003 | d << 8 | s << 4, 0x4001 | d << 8, 0x300A | (d << 8) | (d << 4))                  # lsb mask
        elif k < 5: f.add(0x6003 | d << 8 | s << 4, 0x4004 | d << 8, 0x0029 | d << 8)                                      # rotl ; movt
        elif k < 6: f.add(0x0008, 0x3000 | 0x0E | d << 8 | e << 4)                                                           # clrt ; addc e,d
        elif k < 7: f.add(0x0008, 0x600A | d << 8 | e << 4)                                                                  # clrt ; negc e,d
        elif k < 8: f.add(0x0018, 0x3000 | 0x0A | d << 8 | e << 4)                                                          # sett ; subc e,d
        elif k < 9: f.add(0x2007 | d << 8 | e << 4, 0x0029 | (d if d != 0 else 1) << 8)                                     # div0s e,d ; movt
        elif k < 10: f.add(0x3000 | 0x0F | d << 8 | e << 4, 0x0029 | e << 8)                                                # addv ; movt
        elif k < 11: f.add(0x3000 | 0x0B | d << 8 | e << 4, 0x0029 | e << 8)                                                # subv ; movt
        elif k < 12: f.add(0x4000 | 0x10 | d << 8, 0x0029 | e << 8)                                                          # dt ; movt
        else: f.add(*op2(r, d, s))
        if r.random() < 0.4: f.add(*op2(r, r.choice([0, 1, 2, 3]), d))
    tail(r, f, r.choice([0, 1, 2, 3]))

def fam_join(r, f):
    """the A2-56 route through RulePushMulti: two predecessors each compute a value and test it (tst / cmp), the join reads the phi of the
    values through a mask that leaves out the bits the test needs (the engine pushes the test into the join, onto a phi whose consume mask
    was worked out without it)"""
    # r3 = a one-bit or narrow mask; r4 flag; r5, r6 sources
    mk = r.choice([0, 1, 2, 3])
    if mk == 0: f.add(0xE301, 0x4305)                     # r3 = 0x80000000
    elif mk == 1: f.add(0xE301, 0x4305, 0x4309)           # 0x20000000
    elif mk == 2: f.add(0xE3FF, 0x4328)                   # mov #-1,r3 ; shll16 -> 0xFFFF0000
    else: f.add(0xE3FF, 0x4328, 0x4318)                   # shll8 -> 0xFF000000 (shll16 then shll8: 0xFF000000 << ...)
    f.add(r.choice([0x4415, 0x4411, 0x2448, 0x3447]))      # cmp/pl r4 | cmp/pz r4 | tst r4,r4 | cmp/gt r4,r4(0)
    f.br(r.choice(['bf', 'bt']), 'B')
    def pred(src):
        f.add(0x6003 | 1 << 8 | src << 4)                  # mov src,r1
        f.add(0x2009 | 1 << 8 | 3 << 4)                    # and r3,r1
        if r.random() < 0.4: f.add(0x7100 | r.choice([0, 0, 1, 2]))     # add #k,r1 (keeps a 0/1 low bit possible)
        f.add(r.choice([0x2118, 0x2118, 0x4115, 0x4111, 0x3113]) if True else 0x2118)   # tst r1,r1 | cmp/pl r1 | cmp/pz r1 | cmp/eq... (n=1,m=1)
    pred(5)
    f.br('bra', 'J'); f.add(0x0009)
    f.label('B')
    pred(6)
    f.label('J')
    # readers of the phi r1 that do not need the bits the test needs
    rd = r.randrange(0, 5)
    f.add(0x6003 | 0 << 8 | 1 << 4)                        # mov r1,r0
    if rd == 0: f.add(0x4000)                              # shll r0
    elif rd == 1: f.add(0x600D)                            # extu.w r0,r0
    elif rd == 2: f.add(0xC97F)                            # and #0x7f,r0
    elif rd == 3: f.add(0x4008)                            # shll2 r0
    else: f.add(0x3 << 12 | 0 << 8 | 1 << 4 | 0xC)         # add r1,r0
    f.add(0x0029 | 2 << 8)                                 # movt r2
    f.add(r.choice([0x302C, 0x3028, 0x202B, 0x202A]))      # add/sub/or/xor r2,r0
    if r.random() < 0.5: tail(r, f, 0)

def fam_join2(r, f):
    """RulePushMulti with an op other than a test: each predecessor computes x = src & HIGH and y = op(x); the join reads x through its low
    bits only and y whole (the engine merges the two y computations onto the join phi of x, whose consume mask was worked out without y)"""
    mk = r.choice([(0xE301, 0x4305), (0xE3FF, 0x4328), (0xE3FF, 0x4328, 0x4318), (0xE3FF, 0x4329, 0x4328, 0x4318)])
    f.add(*mk)
    f.add(r.choice([0x4415, 0x4411, 0x2448, 0x3447, 0x3443]))
    f.br(r.choice(['bf', 'bt']), 'B')
    ops = [[0x4029], [0x4019], [0x4009], [0x4001], [0x4000], [0x4008], [0x4018], [0x4028], [0x7101], [0x7104], [0x7180], [0x6118 + 0x1 * 0], [0x611B], [0x6117 + 0x0], [0x611C + 0], [0x611D + 0]]
    pick = r.choice(ops)
    def pred(src):
        f.add(0x6003 | 1 << 8 | src << 4)                  # mov src,r1
        f.add(0x2009 | 1 << 8 | 3 << 4)                    # and r3,r1  -> r1 high bits only
        f.add(0x6003 | 2 << 8 | 1 << 4)                    # mov r1,r2
        f.add(*[(w & ~0x0F00) | (2 << 8) if (w & 0xF000) == 0x4000 or (w & 0xF000) == 0x7000 else (w & ~0x0F00 & ~0x00F0) | (2 << 8) | (2 << 4) for w in pick]) if True else None
    pred(5)
    f.br('bra', 'J'); f.add(0x0009)
    f.label('B')
    pred(6)
    f.label('J')
    rd = r.randrange(0, 5)
    f.add(0x6003 | 0 << 8 | 1 << 4)                        # mov r1,r0
    if rd == 0: f.add(0x600D)
    elif rd == 1: f.add(0xC97F)
    elif rd == 2: f.add(0x600C)
    elif rd == 3: f.add(0x4008)
    else: f.add(0x4000)
    f.add(r.choice([0x302C, 0x3028, 0x202B, 0x202A]) | 2 << 4)    # r0 op= r2
    if r.random() < 0.5: tail(r, f, 0)

def fam_join3(r, f):
    """RulePushMulti / RulePullsubMulti routes with generic pieces: each predecessor makes r1 = src & M and r2 = G(r1); the join reads r1 through a
    narrow H and r2 whole"""
    masks = [(0xE301, 0x4305), (0xE3FF, 0x4328), (0xE3FF, 0x4328, 0x4318), (0xE3FF, 0x4318), (0xE3FF, 0x4308, 0x4318), (0xE3FF, 0x4328, 0x4009),
             (0xE3FF, 0x4318, 0x4308), (0xE30F, 0x4318), (0xE3F0, 0x4318, 0x4308), (0xE301, 0x4305, 0x4309, 0x4309)]
    f.add(*r.choice(masks))
    f.add(r.choice([0x4415, 0x4411, 0x2448, 0x3447, 0x3443, 0x4410]))
    f.br(r.choice(['bf', 'bt']), 'B')
    G = []
    for _ in range(r.randrange(1, 4)):
        G.append(r.choice([[0x4019], [0x4009], [0x4001], [0x4000], [0x4008], [0x4018], [0x4028], [0x4029], [0x7101], [0x7104], [0x7180], [0x7FF0 & 0x7F0F | 0x0 ],
                           [0x6008], [0x6009], [0x600C], [0x600D], [0x600E], [0x600F], [0x6007], [0x600B], [0x4004], [0x4005]]))
    def regfix(w, d):
        hi = w & 0xF000
        if hi in (0x4000, 0x7000): return (w & ~0x0F00) | (d << 8)
        return (w & ~0x0F00 & ~0x00F0) | (d << 8) | (d << 4)
    def pred(src):
        f.add(0x6003 | 1 << 8 | src << 4)
        f.add(0x2009 | 1 << 8 | 3 << 4)
        f.add(0x6003 | 2 << 8 | 1 << 4)
        for g in G:
            for w in g: f.add(regfix(w, 2))
    pred(5)
    f.br('bra', 'J'); f.add(0x0009)
    f.label('B')
    pred(6)
    f.label('J')
    H = r.choice([[0x600C], [0x600D], [0x4019, 0x600C], [0x4009, 0x600C], [0x4029, 0x600D], [0x4018, 0x4019], [0x4000], [0x4008], [0x6008], [0xC97F], [0xC9F0],
                  [0x4019, 0x4019, 0x600C], [0x4021], [0x4001, 0x600C], [0x600F], [0x600E]])
    f.add(0x6003 | 0 << 8 | 1 << 4)
    for w in H: f.add(regfix(w, 0) if (w & 0xF000) != 0xC000 else w)
    f.add(r.choice([0x302C, 0x3028, 0x202B, 0x202A, 0x2029]) | 2 << 4)
    if r.random() < 0.5: tail(r, f, 0)

def fam_loopsum(r, f):
    """sums of scaled terms (RuleCollectTerms) inside a counted loop, partial sums carried round the loop through a phi, read through a mask in or after it"""
    f.add(0xE600 | r.randrange(1, 4))                       # mov #n,r6
    f.lit(0, r.choice([0, 0x06009008, 4, 8, 0x100, 0xFFFFFFFC]))   # acc
    f.label('top')
    for t in range(r.randrange(2, 5)):
        v = r.choice([4, 5, 7]); d = r.choice([1, 2, 3])
        c = r.choice([2, 4, 6, 8, 12, 14, 0x10, 0x12, 0x16, 0xFFFFFFFC, 0xFFFFFFFE])
        f.lit(d, c); f.add(0x0007 | d << 8 | v << 4, 0x001A | d << 8)
        f.add(r.choice([0x300C, 0x300C, 0x3008]) | 0 << 8 | d << 4)
        if r.random() < 0.3:
            f.lit(3, r.choice([0xFFFFFFFC, 0xFFFFFFF8, 0xFFFFFFF0])); f.add(0x2039)    # and r3,r0 inside the loop
    f.add(0x4610)                                            # dt r6
    f.br('bf', 'top')
    tail(r, f, 0)

def fam_loopand(r, f):
    """AND/OR distribute against humpty-or: a loop carrying r1 &= (a value assembled from pieces of an input), the carried value starting narrow"""
    f.add(0xE100 | r.choice([1, 2, 4, 0x0F, 0x1C, 0x7F, 0xFC, 0x03, 0x10]))      # mov #c,r1
    if r.random() < 0.5: f.add(0xE600 | r.randrange(1, 4))                       # mov #n,r6
    else: f.add(0xE601)
    f.label('top')
    src = r.choice([2, 4, 5, 7])
    asm = []
    k = r.randrange(0, 6)
    if k == 0: asm = [0x6003 | 3 << 8 | src << 4, 0x6008 | 3 << 8 | 3 << 4]                    # swap.b
    elif k == 1: asm = [0x6003 | 3 << 8 | src << 4, 0x6009 | 3 << 8 | 3 << 4]                  # swap.w
    elif k == 2: asm = [0x6003 | 3 << 8 | src << 4, 0x6008 | 3 << 8 | 3 << 4, 0x6009 | 3 << 8 | 3 << 4]
    elif k == 3: asm = [0x6003 | 3 << 8 | src << 4, 0x200D | 3 << 8 | r.choice([0, 1, 2, 4, 5]) << 4]   # xtrct
    elif k == 4: asm = [0x6003 | 3 << 8 | src << 4, 0x4019 | 3 << 8, 0x400B & 0 | 0x6008 | 3 << 8 | 3 << 4]
    else: asm = [0x6003 | 3 << 8 | src << 4, 0x6008 | 3 << 8 | 3 << 4, 0x200B | 3 << 8 | src << 4]    # swap.b ; or src,r3
    for w in asm: f.add(w)
    f.add(r.choice([0x2139, 0x2139, 0x213B, 0x213A]))                           # and / or / xor r3,r1
    f.add(0x4610)                                                               # dt r6
    f.br('bf', 'top')
    f.add(0x000B, 0x6013)                                                        # rts ; mov r1,r0


def piece(r, f, dst, src):
    """build dst from a piece of src (a register of the input): swap.b, swap.w, xtrct, shifts, extension, a mask"""
    f.add(0x6003 | dst << 8 | src << 4)
    for _ in range(r.randrange(1, 4)):
        k = r.randrange(0, 12)
        if k == 0: f.add(0x6008 | dst << 8 | dst << 4)
        elif k == 1: f.add(0x6009 | dst << 8 | dst << 4)
        elif k == 2: f.add(0x200D | dst << 8 | r.choice([0, 1, 2, 4, 5, 7, src]) << 4)
        elif k == 3: f.add(0x4019 | dst << 8)
        elif k == 4: f.add(0x4018 | dst << 8)
        elif k == 5: f.add(0x600C | dst << 8 | dst << 4)
        elif k == 6: f.add(0x600D | dst << 8 | dst << 4)
        elif k == 7: f.add(0x4009 | dst << 8)
        elif k == 8: f.add(0x4008 | dst << 8)
        elif k == 9: f.add(0x6008 | dst << 8 | dst << 4, 0x200B | dst << 8 | src << 4)
        elif k == 10: f.lit(2 if dst != 2 else 3, r.choice([0xFF00, 0x00FF00FF, 0xFFFF0000, 0xF0F0F0F0, 0x0000FFFF])); f.add(0x2009 | dst << 8 | (2 if dst != 2 else 3) << 4)
        else: f.add(0x4021 | dst << 8)
    return

ACCOPS = {'add': 0x300C, 'sub': 0x3008, 'and': 0x2009, 'or': 0x200B, 'xor': 0x200A}

def seedacc(r, f, reg):
    f.add(0xE000 | reg << 8 | r.choice([0, 1, 2, 3, 4, 0x0F, 0x1C, 0x7F, 0xFC, 0x80, 0xFF]))

def loopfinish(r, f, cnt):
    f.add(0x4010 | cnt << 8)
    f.br('bf', 'top')

def retacc(r, f, reg):
    f.add(0x6003 | reg << 4)          # mov reg,r0
    if r.random() < 0.5: tail(r, f, 0)

def fam_loopadd(r, f):
    """ADD / SUB accumulator seeded small, fed by a piece of an input each iteration"""
    seedacc(r, f, 1); f.add(0xE600 | r.randrange(1, 5))
    f.label('top')
    piece(r, f, 3, r.choice([2, 4, 5, 7]))
    f.add(r.choice([0x300C, 0x300C, 0x3008]) | 1 << 8 | 3 << 4)
    if r.random() < 0.4: f.add(r.choice([0x6018 + 0x4, 0x611C, 0x611D, 0x6118 | 0x2]) & 0xFF0F | 1 << 8 | 1 << 4)   # a swap or extension of the accumulator itself
    loopfinish(r, f, 6)
    retacc(r, f, 1)

def fam_loopor(r, f):
    """two accumulators, OR / XOR fed by pieces, the second reads the first"""
    seedacc(r, f, 1); seedacc(r, f, 2); f.add(0xE600 | r.randrange(1, 5))
    f.label('top')
    piece(r, f, 3, r.choice([4, 5, 7]))
    f.add(r.choice([0x200B, 0x200A]) | 1 << 8 | 3 << 4)
    if r.random() < 0.6:
        f.add(0x6003 | 3 << 8 | 1 << 4)
        f.add(r.choice([0x4009, 0x4019, 0x4001, 0x4008, 0x4018]) | 3 << 8)
        f.add(r.choice([0x200B, 0x200A, 0x300C]) | 2 << 8 | 3 << 4)
    else:
        f.add(r.choice([0x200B, 0x200A, 0x300C]) | 2 << 8 | 1 << 4)
    loopfinish(r, f, 6)
    f.add(0x6003 | 1 << 4)
    f.add(r.choice([0x201B, 0x201A, 0x301C, 0x2019]) | 0 << 8 | 2 << 4)    # or/xor/add/and r2,r0
    if r.random() < 0.5: tail(r, f, 0)

def fam_loopmix(r, f):
    """several accumulate ops of different kinds in one iteration, shifts and rotates of the accumulator"""
    seedacc(r, f, 1); f.add(0xE600 | r.randrange(1, 4))
    f.label('top')
    for _ in range(r.randrange(2, 4)):
        piece(r, f, 3, r.choice([2, 4, 5, 7]))
        f.add(r.choice(list(ACCOPS.values())) | 1 << 8 | 3 << 4)
        if r.random() < 0.5: f.add(r.choice([0x4000, 0x4001, 0x4004, 0x4005, 0x4008, 0x4009, 0x4018, 0x4019, 0x4028, 0x4029, 0x6108 | 0x4]) & 0xFF0F | 1 << 8 if True else 0)
    loopfinish(r, f, 6)
    retacc(r, f, 1)

def fam_nested(r, f):
    """nested counted loops, accumulators updated at both levels"""
    seedacc(r, f, 1); seedacc(r, f, 2)
    f.add(0xE700 | r.randrange(1, 3))
    f.label('outer')
    f.add(0xE600 | r.randrange(1, 3))
    f.label('top')
    piece(r, f, 3, r.choice([4, 5]))
    f.add(r.choice(list(ACCOPS.values())) | 1 << 8 | 3 << 4)
    loopfinish(r, f, 6)
    piece(r, f, 3, r.choice([2, 4, 5, 7]))
    f.add(r.choice(list(ACCOPS.values())) | 2 << 8 | 1 << 4)
    f.add(0x4710)         # dt r7
    f.br('bf', 'outer')
    f.add(0x6003 | 2 << 4)
    f.add(r.choice([0x201B, 0x301C, 0x201A]) | 1 << 4)
    if r.random() < 0.5: tail(r, f, 0)

def fam_loopsel(r, f):
    """a conditional update of the accumulator inside the loop (a diamond in a cycle)"""
    seedacc(r, f, 1); f.add(0xE600 | r.randrange(1, 5))
    f.label('top')
    piece(r, f, 3, r.choice([2, 4, 5, 7]))
    f.add(r.choice([0x2338, 0x3337, 0x3333 + 0x4, 0x4315, 0x4311, 0x2338]))   # tst r3,r3 | cmp/gt | ... a test of the piece
    f.br(r.choice(['bt', 'bf']), 'skip')
    f.add(r.choice(list(ACCOPS.values())) | 1 << 8 | 3 << 4)
    if r.random() < 0.5: f.add(r.choice([0x4000, 0x4001, 0x4008, 0x4009]) | 1 << 8)
    f.label('skip')
    if r.random() < 0.5: f.add(r.choice(list(ACCOPS.values())) | 1 << 8 | r.choice([2, 4, 5]) << 4)
    loopfinish(r, f, 6)
    retacc(r, f, 1)

def fam_loopmask(r, f):
    """a masked / truncated accumulator (checksum, crc shapes): acc = (acc op piece) & M, acc = ext(acc), acc = (acc << 1) ^ K"""
    seedacc(r, f, 1); f.add(0xE600 | r.randrange(1, 5))
    f.label('top')
    k = r.randrange(0, 4)
    piece(r, f, 3, r.choice([2, 4, 5, 7]))
    if k == 0:
        f.add(r.choice([0x300C, 0x200A, 0x200B]) | 1 << 8 | 3 << 4); f.add(r.choice([0x611C, 0x611D, 0x6119 | 0x0]) & 0xFF0F | 1 << 8 | 1 << 4)
    elif k == 1:
        f.add(0x4000 | 1 << 8); f.lit(3, r.choice([0x04C11DB7, 0x1021, 0xEDB88320, 0x80, 0x1B, 0xA001])); f.add(r.choice([0x200A, 0x200A, 0x300C]) | 1 << 8 | 3 << 4)
    elif k == 2:
        f.add(0x4001 | 1 << 8)
        f.add(r.choice([0x8900, 0x8B00]) | 1)      # bt/bf +1 over the next op (skip one instruction)
        f.lit(3, r.choice([0x04C11DB7, 0xEDB88320, 0xA001])) if False else f.add(0x0009)
        f.add(0x200A | 1 << 8 | 3 << 4)
    else:
        f.add(r.choice([0x300C, 0x3008]) | 1 << 8 | 3 << 4)
        f.lit(2, r.choice([0xFF, 0xFFFF, 0x0F0F0F0F, 0xFFFFFF00, 0x7FFFFFFF])); f.add(0x2009 | 1 << 8 | 2 << 4)
    loopfinish(r, f, 6)
    retacc(r, f, 1)


def fam_tworet(r, f):
    """two returns (two RETURN ops, A2-58's gatherConsumedReturn: one consume mask for all of them), each arm computing its own value, one often narrow"""
    body(r, f, r.randrange(0, 3), [0, 1, 2, 3])
    tbool(r, f, 3) if r.random() < 0.3 else f.add(r.choice([0x3047, 0x3043, 0x3046, 0x3040, 0x4515, 0x2458, 0x245C]))
    f.br(r.choice(['bf', 'bt']), 'else')
    body(r, f, r.randrange(1, 4), [0, 1, 2, 3])
    if r.random() < 0.7: tail(r, f, r.choice([0, 1, 2, 3]))
    else: f.add(0x6003 | r.choice([1, 2, 3]) << 4)
    f.add(0xC9FF if r.random() < 0.3 else 0x600C if r.random() < 0.5 else 0x0009)   # a narrowing of the return in one arm
    f.add(0x000B, r.choice([0x0009, 0x600C, 0x600D, 0x6003 | 1 << 4, 0x7001]))   # rts ; slot
    f.label('else')
    body(r, f, r.randrange(1, 4), [0, 1, 2, 3])
    if r.random() < 0.5: tail(r, f, r.choice([0, 1, 2, 3]))
    else: f.add(0x6003 | r.choice([1, 2, 3]) << 4)

FAMS = [fam_twice, fam_sum, fam_diamond, fam_loop, fam_chain, fam_stack, fam_sign, fam_join, fam_join2, fam_join3, fam_loopsum, fam_loopand, fam_loopadd, fam_loopor, fam_loopmix, fam_nested, fam_loopsel, fam_loopmask, fam_tworet]

def build(seed, fam=None):
    r = random.Random(seed * 7919 + 13)
    f = Fn(r)
    if r.random() < 0.6: f.add(0x6043, 0x6153, 0x6263, 0x6373)     # r0..r3 = r4..r7 so the temps hold values
    fm = FAMS[fam] if fam is not None else r.choice(FAMS)
    fm(r, f)
    f.add(0x000B, 0x6003 | r.randrange(0, 4) << 4 if r.random() < 0.3 else 0x0009)
    return finish(f)

if __name__ == "__main__":
    first, count = int(sys.argv[1]), int(sys.argv[2])
    fam = int(sys.argv[3]) if len(sys.argv) > 3 else None
    out = [g for g in (build(s, fam) for s in range(first, first + count)) if g]
    print(";".join(out))
