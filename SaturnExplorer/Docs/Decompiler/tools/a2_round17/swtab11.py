#!/usr/bin/env python3
# Area 2 round 11 instrument: jump tables with the bound checks and index normalizations a compiler writes, for the generator's raw-words mode
# (SE_GENERATED_WORDS / FUNCS / TABLES, which run every oracle of the generator on the module, rule 6 among them: each index of the table that
# a run dispatched with reaches the case that holds the code the index runs). Nothing in the generator's own table item draws these shapes:
# guards of both senses (bt over the dispatch, bf into it), cmp/hi and cmp/hs and the signed pairs, a base subtracted first, an index
# extended or masked, no guard at all (and #n), a bound from the pool, tables with repeated and default entries, a longword table read through
# add instead of @(r0,Rm).
# usage: swtab11.py <seed> -> prints "WORDS|FUNCS|TABLES" for the three environment variables, and the shape on stderr
import random, sys

KCODE = 0

class A:
    def __init__(self): self.items = []
    def w(self, *x): self.items.extend(('w', v) for v in x)
    def label(self, n): self.items.append(('label', n))
    def ref(self, kind, n): self.items.append((kind, n))
    def align4(self): self.items.append(('align4',))
    def long(self, n): self.items.append(('long', n))
    def lit(self, reg, n): self.items.append(('lit', reg, n))        # mov.l @(d,pc),Rn from a pool slot (label n holds a longword value)

def assemble(a, pool):
    # pass 1: addresses (a pool is appended after the code: [("name", value)])
    addr = 0; labels = {}
    for it in a.items:
        if it[0] == 'label': labels[it[1]] = addr
        elif it[0] == 'align4': addr = (addr + 3) & ~3
        elif it[0] == 'long': addr += 4
        else: addr += 2
    words = []; addr = 0
    for it in a.items:
        k = it[0]
        if k == 'label': continue
        if k == 'align4':
            while addr & 3: words.append(0x0009); addr += 2
            continue
        if k == 'w': words.append(it[1] & 0xFFFF)
        elif k in ('bt', 'bf', 'bts', 'bfs'):
            d = (labels[it[1]] - (addr + 4)) // 2
            assert -128 <= d <= 127, "branch range"
            words.append({'bt': 0x8900, 'bf': 0x8B00, 'bts': 0x8D00, 'bfs': 0x8F00}[k] | (d & 0xFF))
        elif k == 'bra':
            d = (labels[it[1]] - (addr + 4)) // 2
            words.append(0xA000 | (d & 0xFFF))
        elif k == 'mova':
            d = (labels[it[1]] - (((addr + 4)) & ~3)) // 4
            assert 0 <= d <= 255, "mova range"
            words.append(0xC700 | d)
        elif k == 'lit':
            d = (labels[it[2]] - ((addr + 4) & ~3)) // 4
            assert 0 <= d <= 255, "lit range"
            words.append(0xD000 | it[1] << 8 | d)
        elif k == 'long':
            v = it[1] if isinstance(it[1], int) else (0x06009000 + labels[it[1]])
            words.append(v >> 16); words.append(v & 0xFFFF); addr += 2
        addr += 2
    return words, labels

def build(seed):
    r = random.Random(seed * 7919 + 101)
    a = A()
    n = r.choice([2, 3, 4, 5, 6, 7, 8, 9, 12, 16])
    nonneg = True
    base = r.choice([0, 0, 0, 1, 2, 3, 5, 8])
    guard = r.choice(['hi', 'hs', 'hi', 'hs', 'gtpz', 'bfhi', 'bfhs', 'none', 'poolhi', 'gtge', 'cmp_eq_ltw'])
    ext = r.choice(['none', 'none', 'none', 'b', 'w', 'sb'])
    shape = r.choice(['mova', 'mova', 'add'])
    mask = r.choice([None, None, None, 1, 3, 7, 15])
    selr = r.choice([4, 4, 4, 6, 7])
    join = r.random() < 0.3
    half = r.random() < 0.2 and guard in ('hi', 'hs', 'bfhi', 'bfhs', 'poolhi')
    scaled = (not half) and r.random() < 0.25 and guard in ('hi', 'hs', 'bfhi', 'bfhs', 'poolhi')
    desc = dict(n=n, base=base, guard=guard, ext=ext, shape=shape, mask=mask, sel=selr, join=join, half=half, scaled=scaled)
    if guard == 'none':
        # the index is masked and the table has mask+1 entries
        if mask is None: mask = r.choice([1, 3, 7]); desc['mask'] = mask
        n = mask + 1; desc['n'] = n
    idx = 4
    # --- index preparation, in r0 or r4 ---
    a.w(0x6003 | 0 << 8 | selr << 4)              # mov rSel,r0   (the selector stays in its register; r0 works)
    if ext == 'b': a.w(0x600C)                    # extu.b r0,r0
    elif ext == 'w': a.w(0x600D)
    elif ext == 'sb': a.w(0x600E)
    if base:
        a.w(0x7000 | (-base & 0xFF) if -128 <= -base < 128 else 0x7000)   # add #-base,r0
    if mask is not None and guard == 'none': a.w(0xC900 | mask)
    # r0 holds the index. The bound
    nn = 2 * n if half else n
    bound = nn - 1
    if scaled:
        a.w(0x4008 | 0 << 8)                      # shll2 r0 : the byte offset
        bound = 4 * (nn - 1); nn = 4 * nn
    def mov_bound(reg, v):
        if v < 0x80: a.w(0xE000 | reg << 8 | v)
        else: a.lit(reg, 'pool_b%d' % reg)
    if guard in ('hi', 'bfhi', 'poolhi'):
        if guard == 'poolhi': a.lit(1, 'pool_b1')
        else: mov_bound(1, bound)
        a.w(0x3006 | 0 << 8 | 1 << 4)             # cmp/hi r1,r0   : r0 >u bound
        if guard == 'bfhi':
            a.ref('bf', 'dispatch'); a.ref('bra', 'default'); a.w(0x0009)
        else:
            a.ref('bt', 'default')
    elif guard in ('hs', 'bfhs'):
        mov_bound(1, nn)
        a.w(0x3002 | 0 << 8 | 1 << 4)             # cmp/hs r1,r0   : r0 >=u n
        if guard == 'bfhs':
            a.ref('bf', 'dispatch'); a.ref('bra', 'default'); a.w(0x0009)
        else:
            a.ref('bt', 'default')
    elif guard == 'gtpz':
        a.w(0x4011 | 0 << 8); a.ref('bf', 'default')          # cmp/pz r0 ; bf default   (negative)
        mov_bound(1, bound); a.w(0x3007 | 0 << 8 | 1 << 4); a.ref('bt', 'default')   # cmp/gt r1,r0 ; bt
    elif guard == 'gtge':
        mov_bound(1, nn); a.w(0x3003 | 0 << 8 | 1 << 4); a.ref('bt', 'default')       # cmp/ge r1,r0 : r0 >=s n
        a.w(0x4011 | 0 << 8); a.ref('bf', 'default')
    elif guard == 'cmp_eq_ltw':
        # r0 < 0 || r0 > bound as two tests, either order
        mov_bound(1, bound); a.w(0x3006 | 0 << 8 | 1 << 4); a.ref('bt', 'default')
        a.w(0x4011 | 0 << 8); a.ref('bf', 'default')
    # none: no guard
    a.label('dispatch')
    if half: a.w(0x4001 | 0 << 8)                  # shlr r0 : index / 2 (guard was on the undivided value)
    if scaled:
        a.w(0x6003 | 2 << 8 | 0 << 4)             # mov r0,r2  (already scaled)
        a.ref('mova', 'table')
        a.w(0x000E | 0 << 8 | 2 << 4)
    elif shape == 'mova':
        a.w(0x6003 | 2 << 8 | 0 << 4)             # mov r0,r2
        a.w(0x4008 | 2 << 8)                      # shll2 r2
        a.ref('mova', 'table')
        a.w(0x000E | 0 << 8 | 2 << 4)             # mov.l @(r0,r2),r0   (the index is in r2)
    else:
        a.w(0x6003 | 2 << 8 | 0 << 4)             # mov r0,r2
        a.w(0x4008 | 2 << 8)                      # shll2 r2
        a.ref('mova', 'table')
        a.w(0x300C | 0 << 8 | 2 << 4)             # add r2,r0
        a.w(0x6002 | 0 << 8 | 0 << 4)             # mov.l @r0,r0
    a.w(0x402B | 0 << 8)                          # jmp @r0
    a.w(0x0009)
    # --- cases ---
    cases = []
    tgt = []
    k = 0
    for i in range(n):
        x = r.randrange(0, 100)
        if x < 12: tgt.append('default')
        elif i > 0 and x < 35 and tgt[-1] != 'default': tgt.append(tgt[-1])
        else: tgt.append('c%d' % k); k += 1
    if k == 0: tgt[0] = 'c0'; k = 1
    for c in range(k):
        a.label('c%d' % c)
        a.w(0xE300 | (0x40 + c))                  # mov #0x40+c,r3
        a.w(0x1000 | 5 << 8 | 3 << 4 | c)         # mov.l r3,@(c,r5)
        if join: a.ref('bra', 'join'); a.w(0x0009)
        else: a.w(0x000B); a.w(0x0009)
    a.label('default')
    a.w(0xE300 | 0x7F); a.w(0x1000 | 5 << 8 | 3 << 4 | 15); a.w(0x000B); a.w(0x0009)
    if join:
        a.label('join'); a.w(0xE300 | 0x55); a.w(0x1000 | 5 << 8 | 3 << 4 | 14); a.w(0x000B); a.w(0x0009)
    a.align4()
    a.label('table')
    for t in tgt:
        a.long(t)
    pool = []
    # bound pool slots (constants >= 0x80)
    a.align4()
    for reg in (0, 1, 2):
        pass
    a.label('pool_b1'); a.long(bound if guard in ('hi', 'bfhi', 'poolhi', 'gtpz', 'cmp_eq_ltw') else nn)
    a.label('pool_b0'); a.long(bound)
    words, labels = assemble(a, pool)
    jmp = labels['dispatch'] + 2 * 0
    # find the jmp instruction address: dispatch + (mova form: mov,shll2,mova,mov.l/ add,mov.l ) words
    jaddr = None
    for i, w in enumerate(words):
        if w == 0x402B: jaddr = 2 * i
    entries = [labels['default']] * (desc['base'] if guard != 'none' else 0) + [labels[t] for t in tgt]
    if desc['half']: entries = [labels['default']] * (desc['base']) + [labels[t] for t in tgt for _ in (0, 1)]
    # the index registers: the selector reaches the table as index (r4 - base ...), the oracle's "sel" is r4
    return desc, words, jaddr, labels['default'], entries

def main():
    seed = int(sys.argv[1])
    desc, words, jaddr, default, entries = build(seed)
    end = 2 * len(words)
    sys.stderr.write("%s\n" % desc)
    print(("%s|%X/%X/%X|%X:" + str(desc["sel"]) + ":%d:%X:%s") % (" ".join("%04X" % w for w in words), 0, 0, end, jaddr, len(entries), default, ",".join("%X" % e for e in entries)))

if __name__ == "__main__":
    main()
