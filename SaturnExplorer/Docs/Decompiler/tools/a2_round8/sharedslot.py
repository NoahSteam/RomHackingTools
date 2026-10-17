#!/usr/bin/env python3
# Area 2 round 8 instrument: SE_AREA2_FIXTURES file: a PC-relative slot that other code also reaches as an instruction of its own (the host flags it unsupported).
# usage: sharedslot.py > shared.txt; same use as slotgen.py. All 216 decodable fixtures come back "not-ok" (flagged), the switch layout does not resolve.
# A PC-relative slot that other code also reaches as an instruction of its own (the host flags it unsupported when it sees an immediate
# branch or a jump table to it; this asks about the jumps it does not look at).
BASE = 0x06009000
def w(x): return "%04X" % x
def build(link, bkind, slot, d, shift):
    mem = {}
    def put(o, v): mem[o] = v & 0xFFFF
    sl = {'mova': 0xC700 | d, 'movw0': 0x9000 | d, 'movl0': 0xD000 | d}[slot]
    X = 0x30
    P = 0x20
    off = 2 if shift else 0
    if shift: put(0, 0x0009)
    def poolload(at, reg, pool):
        base = (at & ~3) + 4
        assert pool >= base and (pool - base) % 4 == 0, (at, pool)
        return 0xD000 | reg << 8 | (pool - base) // 4
    def B(at):
        # a delayed branch to X at address 'at'
        if bkind == 'bra': return 0xA000 | ((X - (at + 4)) // 2) & 0xFFF
        if bkind == 'bt/s': return 0x8D00 | ((X - (at + 4)) // 2) & 0xFF
        if bkind == 'bf/s': return 0x8F00 | ((X - (at + 4)) // 2) & 0xFF
    if link == 'above':       # tst r4,r4 ; bf S (r4 != 0) ; B ; S ; rts ; nop
        put(off, 0x2448); off += 2
        at = off; Sadr = off + 4
        put(off, 0x8B00 | ((Sadr - (at + 4)) // 2) & 0xFF); off += 2
        if bkind == 'bf/s': put(off, 0x0018 if False else 0x2448); 
        # for bt/s the branch must take when T=1 (r4 == 0) ; for bf/s when T=0: make T = (r4 == 0) by the tst above, bf/s taken when r4 != 0 -> not on this path.
        # The B path is the r4 == 0 path (fall through from bf): T = 1 there, so bt/s takes, bf/s does not (falls to S+2 = rts), both still agree.
        put(off, B(off)); off += 2
        put(off, sl); off += 2
        put(off, 0x000B); off += 2; put(off, 0x0009); off += 2
    elif link == 'below':     # tst r4,r4 ; bt B ; bra S ; nop ; B ; S ; rts ; nop     (r4 == 0 -> B path)
        put(off, 0x2448); off += 2
        at = off
        Badr = off + 6
        put(off, 0x8900 | ((Badr - (at + 4)) // 2) & 0xFF); off += 2
        at = off; Sadr = Badr + 2
        put(off, 0xA000 | ((Sadr - (at + 4)) // 2) & 0xFFF); off += 2
        put(off, 0x0009); off += 2
        put(off, B(off)); off += 2
        put(off, sl); off += 2
        put(off, 0x000B); off += 2; put(off, 0x0009); off += 2
    elif link == 'jmpconst':  # mov.l pool,r1 ; tst r4,r4 ; bt B ; jmp @r1 ; nop ; B ; S ; rts ; nop
        put(off, poolload(off, 1, P)); off += 2
        put(off, 0x2448); off += 2
        at = off; Badr = off + 6
        put(off, 0x8900 | ((Badr - (at + 4)) // 2) & 0xFF); off += 2
        put(off, 0x412B); off += 2
        put(off, 0x0009); off += 2
        Badr_ = off
        put(off, B(off)); off += 2
        Sadr = off
        put(off, sl); off += 2
        put(off, 0x000B); off += 2; put(off, 0x0009); off += 2
        put(P, (BASE + Sadr) >> 16); put(P + 2, (BASE + Sadr) & 0xFFFF)
    elif link == 'brafconst': # same with braf r1 (r1 = S - (braf + 4))
        put(off, poolload(off, 1, P)); off += 2
        put(off, 0x2448); off += 2
        at = off; Badr = off + 6
        put(off, 0x8900 | ((Badr - (at + 4)) // 2) & 0xFF); off += 2
        brafat = off
        put(off, 0x0123); off += 2
        put(off, 0x0009); off += 2
        put(off, B(off)); off += 2
        Sadr = off
        put(off, sl); off += 2
        put(off, 0x000B); off += 2; put(off, 0x0009); off += 2
        dist = Sadr - (brafat + 4)
        put(P, (dist >> 16) & 0xFFFF); put(P + 2, dist & 0xFFFF)
    elif link == 'switch':    # r4 selects: jump table of two entries: B and S
        # mov.l pool,r1 (table) ; shll2 r4 ; mov.l @(r0?,..) -- simple: r4 = 0/1 -> mov.l @(r0,r1) needs r0; use  mov r4,r0 ; shll2 r0 ; mov.l @(r0,r1),r2 ; jmp @r2 ; nop
        put(off, poolload(off, 1, P)); off += 2
        put(off, 0x6043); off += 2         # mov r4,r0
        put(off, 0x4008); off += 2         # shll2 r0
        put(off, 0x021E); off += 2         # mov.l @(r0,r1),r2
        # a bound check keeps the table known: and #1,r0 would mask; use cmp/hi
        put(off, 0x422B); off += 2         # jmp @r2
        put(off, 0x0009); off += 2
        Badr = off
        put(off, B(off)); off += 2
        Sadr = off
        put(off, sl); off += 2
        put(off, 0x000B); off += 2; put(off, 0x0009); off += 2
        put(P, (BASE + Badr) >> 16); put(P + 2, (BASE + Badr) & 0xFFFF)
        put(P + 4, (BASE + Sadr) >> 16); put(P + 6, (BASE + Sadr) & 0xFFFF)
    put(X, 0x000B); put(X + 2, 0x0009)
    words = []
    for o in range(0, 0x100, 2):
        words.append(mem.get(o, 0x4000 + o // 2))
    for o in range(0, X, 2):
        if o not in mem: words[o // 2] = 0x0009
    name = "%s %s+%s d=%d shift=%d" % (link, bkind, slot, d, shift)
    return name, words
out = []
for link in ('above', 'below', 'jmpconst', 'brafconst', 'switch'):
    for bkind in ('bra', 'bt/s', 'bf/s'):
        for slot in ('mova', 'movw0', 'movl0'):
            for d in (1, 2, 3):
                for shift in (0, 1):
                    try:
                        name, words = build(link, bkind, slot, d, shift)
                    except AssertionError:
                        continue
                    ins = "0;1" if link != 'switch' else "0;1"
                    out.append("%s|%s|%s|pc|" % (name, " ".join(w(x) for x in words), ins))
print("\n".join(out))
