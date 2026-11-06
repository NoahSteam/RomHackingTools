# r19: random stack-piece functions. frame of 8 bytes initialised by two longs (r4, r5), 1..3 overwrites at aligned offsets of any width from r4..r7,
# two reads (b/w signed via r0, or long) with optional extensions, a combining step (compare, arithmetic), movt/sub, return in r0.
# usage: pgen.py <first seed> <count> [mode]  -> ';'-joined words ; pgen.py show <seed>
import random, sys
def h(x): return '%04X' % x
def mov_rr(n, m): return 0x6003 | (n << 8) | (m << 4)
def gen(seed, mode=None):
    r = random.Random(seed)
    w = [0x7FF8, 0x1F40, 0x1F51]      # add #-8,r15 ; mov.l r4,@(0,r15) ; mov.l r5,@(4,r15)
    names = []
    for _ in range(r.choice([1, 1, 2, 2, 3])):
        kind = r.choice(['b', 'b', 'h', 'h', 'l'])
        src = r.choice([4, 5, 6, 7])
        if kind == 'b':
            off = r.randrange(8); w += [mov_rr(0, src), 0x80F0 | off]
        elif kind == 'h':
            off = r.randrange(4) * 2; w += [mov_rr(0, src), 0x81F0 | (off // 2)]
        else:
            off = r.randrange(2) * 4; w += [0x1F00 | (src << 4) | (off // 4)]
        names.append((kind, off))
    def read(reg):
        kind = r.choice(['b', 'h', 'h', 'l', 'l'])
        if kind == 'b': w.extend([0x84F0 | r.randrange(8), mov_rr(reg, 0)])
        elif kind == 'h': w.extend([0x85F0 | r.randrange(4), mov_rr(reg, 0)])
        else: w.append(0x50F0 | (reg << 8) | r.randrange(2))
        # optional cut/extension of the value
        e = r.choice([None, None, 0xC, 0xD, 0xE, 0xF])
        if e is not None: w.append(0x6000 | (reg << 8) | (reg << 4) | e)
        if r.random() < 0.15: w.append(0x4000 | (reg << 8) | r.choice([0x01, 0x21, 0x09, 0x29, 0x28]))   # shlr, shar, shlr8, shlr16, shll16
    read(1)
    second = r.choice(['piece', 'cut', 'cut', 'cut'])
    if second == 'piece': read(2)
    else:
        w.append(mov_rr(2, r.choice([4, 5, 6, 7])))
        e = r.choice([None, 0xC, 0xD, 0xE, 0xF, 0xE, 0xF])
        if e is not None: w.append(0x6000 | (2 << 8) | (2 << 4) | e)
    step = r.choice(['cmp', 'cmp', 'cmp', 'cmp', 'cmpbr', 'arith'])
    c = r.choice([0x0, 0x2, 0x3, 0x6, 0x7]); a, b = r.choice([(1, 2), (2, 1)])
    if step == 'cmp':
        w += [0x3000 | (a << 8) | (b << 4) | c, 0x0029]
    elif step == 'cmpbr':
        # cmp ; bf L0 ; mov #1,r0 ; bra E ; nop ; L0: mov #0,r0 ; E:
        w += [0x3000 | (a << 8) | (b << 4) | c, 0x8B02, 0xE001, 0xA001, 0x0009, 0xE000]
    else:
        o = r.choice([0x3C, 0x38, 0x29, 0x2A, 0x2B])
        if o in (0x3C, 0x38): w.append(0x3000 | (a << 8) | (b << 4) | (o & 0xF))
        else: w.append(0x2000 | (a << 8) | (b << 4) | (o & 0xF))
        w.append(mov_rr(0, a))
    w += [0x7F08, 0x000B, 0x0009]
    return ' '.join(h(x) for x in w)
if __name__ == '__main__':
    if sys.argv[1] == 'show':
        print(gen(int(sys.argv[2])))
    else:
        s = int(sys.argv[1]); n = int(sys.argv[2])
        for i in range(n): print('p%d %s' % (s + i, gen(s + i)))
