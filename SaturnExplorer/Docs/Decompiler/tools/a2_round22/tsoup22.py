#!/usr/bin/env python3
# Area 2 round 22: dense register-only soups for the printed-C differential. Every item is drawn from the flag, multiply,
# divide-step, extension and swap ops (not one in four as grammar 3 draws them), over r0..r7, with short forward bt/bf
# skips and bt/s, bf/s, so that T, Q, M, MACH and MACL are produced and consumed across narrow cuts.
# usage: tsoup22.py <first seed> <count>   -> "name words" lines (fam.sh / famrun19.sh input)
import random, sys

def rr(base, n, m): return base | (n << 8) | (m << 4)
TWO = [0x300E, 0x300F, 0x300A, 0x300B, 0x600A, 0x3004, 0x2007, 0x200D, 0x6008, 0x6009, 0x200C, 0x300D, 0x3005, 0x0007,
       0x200F, 0x200E, 0x600E, 0x600F, 0x600C, 0x600D, 0x300C, 0x3008, 0x2009, 0x200A, 0x200B, 0x600B, 0x6007, 0x6003,
       0x3000, 0x3002, 0x3003, 0x3006, 0x3007, 0x2008]
ONE = [0x4024, 0x4025, 0x4004, 0x4005, 0x4000, 0x4001, 0x4020, 0x4021, 0x4008, 0x4009, 0x4018, 0x4019, 0x4028, 0x4029,
       0x4015, 0x4011, 0x0029, 0x4010, 0x000A, 0x001A, 0x400A, 0x401A]
NOARG = [0x0019, 0x0008, 0x0018, 0x0028]

def item(r, regs):
    k = r.random()
    if k < 0.55:
        return [rr(r.choice(TWO), r.choice(regs), r.choice(regs))]
    if k < 0.85:
        return [r.choice(ONE) | (r.choice(regs) << 8)]
    if k < 0.92:
        return [r.choice(NOARG)]
    if k < 0.96:
        return [0xE000 | (r.choice(regs) << 8) | r.choice([0, 1, 0x7F, 0x80, 0xFF, 0xFE, 5, 0x10])]
    return [0x7000 | (r.choice(regs) << 8) | r.choice([1, 0xFF, 0x80, 0x7F])]

def gen(seed):
    r = random.Random(seed * 7919 + 22)
    regs = list(range(8))
    w = []
    n = r.randrange(4, 18)
    i = 0
    while i < n:
        if r.random() < 0.12:
            body = []
            for _ in range(r.randrange(1, 3)): body += item(r, regs)
            br = r.choice([0x8900, 0x8B00, 0x8D00, 0x8F00])
            if br in (0x8D00, 0x8F00):
                slot = item(r, regs)
                w += [br | len(body)] + slot + body   # bt/s L ; slot ; body ; L:
            else:
                w += [br | (len(body) - 1)] + body if len(body) >= 1 else []
            i += 1 + len(body)
            continue
        w += item(r, regs); i += 1
    tail = r.choice([[0x000B, 0x0009], [0x000B, 0x0029], [0x000B, 0x6013], [0x000B, 0x600E], [0x000B, 0x600C],
                     [0x000B, 0x001A], [0x000B, 0x000A], [0x0029, 0x000B, 0x6003 | (0 << 8) | (r.randrange(8) << 4)]])
    if tail[0] == 0x000B and tail[1] in (0x6013, 0x600E, 0x600C): tail = [0x000B, rr(tail[1] & 0xF00F, 0, r.randrange(8))]
    return ' '.join('%04X' % x for x in w + tail)

if __name__ == '__main__':
    s, n = int(sys.argv[1]), int(sys.argv[2])
    for i in range(n): print('t%d %s' % (s + i, gen(s + i)))
