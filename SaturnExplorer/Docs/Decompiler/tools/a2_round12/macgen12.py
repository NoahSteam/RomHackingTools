#!/usr/bin/env python3
# Area 2 round 12 instrument: macgen.py of round 8 with the accumulator built from ONE register, the way a 32-bit value is widened into the
# 64-bit mach:macl: macl = r7 and mach = r7 s>> 31 (a chain of 31 shar: the sign extension), mach = 0 (the zero extension), or both halves of
# the same register. Then mac.l / mac.w (S clear and set) on operands in the code area, and mach, macl or the high word compared read back.
# usage: macgen12.py > mac12.txt; SE_AREA2_FIXTURES=mac12.txt SaturnExplorerSh2DecompilerTests <scratch>   (same format as macgen.py)
import sys
L = [0, 1, 0xFFFFFFFF, 0x7FFFFFFF, 0x80000000, 0x80000001, 0x00010000, 0xFFFF0000, 0x0000FFFF, 0x00008000, 0xFFFF8000, 0x12345678, 0x40000000, 0xC0000000]
W = [0, 1, 0xFFFF, 0x7FFF, 0x8000, 0x8001, 0x00FF, 0xFF00, 0x1234]
INS = ";".join("0,0,%x,%x" % (r6, r7) for r6, r7 in [(0, 0), (0, 1), (0, 0xFFFFFFFF), (0, 0x7FFFFFFF), (0, 0x80000000), (0xFFFF, 0x80000001), (5, 0xFFFF8000), (0, 0x12345678), (0xFFFFFFFF, 0x7FFFFFFF), (0, 0x40000000), (0, 0xC0000000)])
def fx(op, s, acc, ret, a, b):
    w = []
    # the accumulator
    w += [0x471A]                                   # lds r7,macl
    if acc == 'sext': w += [0x6173] + [0x4121] * 31 + [0x410A]       # mov r7,r1 ; shar r1 x31 ; lds r1,mach
    elif acc == 'zext': w += [0xE100, 0x410A]       # mov #0,r1 ; lds r1,mach
    elif acc == 'same': w += [0x470A]               # lds r7,mach
    else: w += [0x460A]                             # lds r6,mach
    w += [0xE002 if s else 0xE000, 0x400E]          # S bit
    i1 = len(w); w += [0xC700, 0x6403]              # mova ... ; mov r0,r4
    i2 = len(w); w += [0xC700, 0x6503]
    w += [0x054F if op == 'l' else 0x455F]
    if ret == 'macl': w += [0x001A]
    elif ret == 'mach': w += [0x000A]
    else: w += [0x010A, 0x001A, 0x301C]            # mach into r1, macl into r0, r0 = r0 + r1
    w += [0x000B, 0x0009]
    while (len(w) * 2) % 4: w.append(0x0009)
    pa = len(w) * 2; pb = pa + 4
    w[i1] = 0xC700 | ((pa - (((i1 * 2) + 4) & ~3)) // 4); w[i2] = 0xC700 | ((pb - (((i2 * 2) + 4) & ~3)) // 4)
    if op == 'l': w += [a >> 16, a & 0xFFFF, b >> 16, b & 0xFFFF]
    else: w += [a & 0xFFFF, 0, b & 0xFFFF, 0]
    return w
out = []
for op, vals in (('l', L), ('w', W)):
    for s in (0, 1):
        for acc in ('sext', 'zext', 'same'):
            for ret in ('macl', 'mach', 'both'):
                for a in vals:
                    for b in vals:
                        w = fx(op, s, acc, ret, a, b)
                        out.append("mac.%s S=%d %s %s a=%x b=%x|%s|%s|c" % (op, s, acc, ret, a, b, " ".join("%04X" % x for x in w), INS))
print("\n".join(out))
