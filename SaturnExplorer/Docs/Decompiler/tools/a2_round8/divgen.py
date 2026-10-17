#!/usr/bin/env python3
# Area 2 round 8 instrument: the compiler's divide-by-constant idiom (Sh2GeneratedTests DivisionItem) for a sweep of divisors.
# usage: divgen.py u|s <first> <last> <step> <scale>; same use as maskgen.py.
"""A sweep of the compiler's divide-by-constant idiom (Sh2GeneratedTests DivisionItem), for divisors the generator does not draw.
usage: divgen.py kind first last step scale [x]   kind: u | s ; output: functions joined by ';' """
import sys
M = 0xFFFFFFFF
def magic_u(d):
    nc = (M - ((-d) & M) % d) & M
    p = 31
    q1 = (0x80000000 // nc) & M; r1 = (0x80000000 - q1 * nc) & M
    q2 = 0x7FFFFFFF // d; r2 = (0x7FFFFFFF - q2 * d) & M
    add = False
    while True:
        p += 1
        if r1 >= (nc - r1) & M:
            q1 = (2 * q1 + 1) & M; r1 = (2 * r1 - nc) & M
        else:
            q1 = (2 * q1) & M; r1 = (2 * r1) & M
        if (r2 + 1) >= (d - r2):
            if q2 >= 0x7FFFFFFF: add = True
            q2 = (2 * q2 + 1) & M; r2 = (2 * r2 + 1 - d) & M
        else:
            if q2 >= 0x80000000: add = True
            q2 = (2 * q2) & M; r2 = (2 * r2 + 1) & M
        delta = (d - 1 - r2) & M
        if not (p < 64 and (q1 < delta or (q1 == delta and r1 == 0))): break
    return (q2 + 1) & M, p - 32, add
def magic_s(d):
    two31 = 0x80000000
    ad = (-d) & M if d < 0 else d
    t = (two31 + ((d & M) >> 31)) & M
    anc = (t - 1 - t % ad) & M
    p = 31
    q1 = two31 // anc; r1 = two31 - q1 * anc
    q2 = two31 // ad; r2 = two31 - q2 * ad
    while True:
        p += 1
        q1 = (2 * q1) & M; r1 = (2 * r1) & M
        if r1 >= anc: q1 = (q1 + 1) & M; r1 = (r1 - anc) & M
        q2 = (2 * q2) & M; r2 = (2 * r2) & M
        if r2 >= ad: q2 = (q2 + 1) & M; r2 = (r2 - ad) & M
        delta = (ad - r2) & M
        if not (q1 < delta or (q1 == delta and r1 == 0)): break
    m = (q2 + 1) & M
    if d < 0: m = (-m) & M
    return m, p - 32
def shift_words(n, count, left, arith):
    b1 = 0x4000 if left else (0x4021 if arith else 0x4001)
    b2 = 0x4008 if left else 0x4009; b8 = 0x4018 if left else 0x4019; b16 = 0x4028 if left else 0x4029
    w = []
    if arith:
        return [b1 | n << 8 for _ in range(count)]
    while count >= 16: w.append(b16 | n << 8); count -= 16
    while count >= 8: w.append(b8 | n << 8); count -= 8
    while count >= 2: w.append(b2 | n << 8); count -= 2
    while count >= 1: w.append(b1 | n << 8); count -= 1
    return w
def item(d, signed, scale, x, l, t, u, to_r0):
    w = [0xD000 | l << 8]
    lit = None
    if not signed:
        m, s, add = magic_u(d)
        lit = m
        w.append(0x3005 | x << 8 | l << 4)
        w.append(0x000A | t << 8)
        if not add:
            w += shift_words(t, s, False, False); result = t; last = s
        else:
            w.append(0x3008 | x << 8 | t << 4)
            w.append(0x4001 | x << 8)
            w.append(0x300C | x << 8 | t << 4)
            w += shift_words(x, s - 1, False, False); result = x; last = s - 1
    else:
        sd = d
        m, s = magic_s(sd)
        lit = m
        w.append(0x300D | x << 8 | l << 4)
        w.append(0x000A | t << 8)
        msigned = m - (1 << 32) if m & 0x80000000 else m
        if sd > 0 and msigned < 0: w.append(0x300C | t << 8 | x << 4)
        if sd < 0 and msigned > 0: w.append(0x3008 | t << 8 | x << 4)
        w += shift_words(t, s, False, True)
        w.append(0x6003 | u << 8 | t << 4)
        w.append(0x4000 | u << 8)
        w.append(0xE000 | u << 8)
        w.append(0x300E | t << 8 | u << 4)
        result = t; last = s
    if scale < 0: scale = last
    if scale > 12: scale = 8
    if scale > 0: w += shift_words(result, scale, True, False)
    if to_r0: w.append(0x6003 | 0 << 8 | result << 4)
    return w, lit
def function(d, signed, scale, x=4, to_r0=True):
    l, t, u = 1, 2, 3
    w, lit = item(d, signed, scale, x, l, t, u, to_r0)
    out = list(w) + [0x000B, 0x0009]
    if len(out) % 2: out.append(0x0009)
    pool = len(out)
    pc = (0 + 4) & ~3
    out[0] = (out[0] & 0xFF00) | ((2 * pool - pc) // 4)
    out += [lit >> 16, lit & 0xFFFF]
    return " ".join("%04X" % v for v in out)
if __name__ == "__main__":
    kind, first, last, step, scale = sys.argv[1], int(sys.argv[2], 0), int(sys.argv[3], 0), int(sys.argv[4], 0), int(sys.argv[5])
    outs = []
    d = first
    while d <= last:
        if kind == 'u' and d >= 2 and (d & (d - 1)) != 0:
            outs.append(function(d, False, scale))
        if kind == 's' and abs(d) >= 2 and (abs(d) & (abs(d) - 1)) != 0:
            outs.append(function(d, True, scale))
        d += step
    print(";".join(outs))
