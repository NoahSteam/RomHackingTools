#!/usr/bin/env python3
# Area 2 round 8 instrument: SE_AREA2_FIXTURES file: mac.l and mac.w with the operands in the code area and the accumulator from r6:r7, S clear and set.
# usage: macgen.py > mac.txt; SE_AREA2_FIXTURES=mac.txt SaturnExplorerSh2DecompilerTests <scratch> (1,600 fixtures, 27,200 comparisons).
# mac.l / mac.w with the operands in the code area (read through the constant pointers mova makes) and the accumulator from r6/r7,
# held by value: the text (evaluated with the code area readable) against the run.
import itertools, sys
L = [0, 1, 0xFFFFFFFF, 0x7FFFFFFF, 0x80000000, 0x80000001, 0x00010000, 0xFFFF0000, 0x0000FFFF, 0x00008000, 0xFFFF8000, 0x12345678, 0x3FFFFFFF, 0x40000000, 0xC0000000, 0x00000100]
W = [0, 1, 0xFFFF, 0x7FFF, 0x8000, 0x8001, 0x00FF, 0xFF00, 0x4000, 0xC000, 0x1234, 0x0100]
ACC = [(0, 0), (0, 1), (0, 0xFFFFFFFF), (0xFFFFFFFF, 0xFFFFFFFF), (0, 0x7FFFFFFF), (0, 0x80000000), (0x7FFF, 0xFFFFFFFF), (0xFFFF8000, 0),
       (0x00007FFF, 0x7FFFFFFF), (0xFFFF8000, 0x80000000), (0x80000000, 0), (0x7FFFFFFF, 0xFFFFFFFF), (1, 0), (0xFFFFFFFF, 0), (0x00008000, 0x00000000), (0xFFFF7FFF, 0xFFFFFFFF), (0x00007FFF, 0xFFFF8000)]
def fx(op, s, ret, a, b):
    w = [0xC707, 0x6403, 0xC707, 0x6503, 0x460A, 0x471A, 0xE002 if s else 0xE000, 0x400E, (0x054F if op == 'l' else 0x455F), 0x001A if ret == 'macl' else 0x000A, 0x000B, 0x0009]
    while len(w) < 16: w.append(0x0009)
    if op == 'l':
        w += [a >> 16, a & 0xFFFF, b >> 16, b & 0xFFFF]
    else:
        w += [a & 0xFFFF, 0, b & 0xFFFF, 0]
    return w
out = []
ins = ";".join("0,0,%x,%x" % (mh, ml) for mh, ml in ACC)
for op, vals in (('l', L), ('w', W)):
    for s in (0, 1):
        for ret in ('macl', 'mach'):
            for a in vals:
                for b in vals:
                    w = fx(op, s, ret, a, b)
                    out.append("mac.%s S=%d %s a=%x b=%x|%s|%s|c" % (op, s, ret, a, b, " ".join("%04X" % x for x in w), ins))
print("\n".join(out))
