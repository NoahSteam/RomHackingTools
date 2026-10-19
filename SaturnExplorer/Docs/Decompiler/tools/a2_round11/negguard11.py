#!/usr/bin/env python3
# Area 2 round 11 (A2-68): a jump table indexed by a scaled value (a multiple of 4 as far as the engine's non-zero mask knows) with a signed lower
# bound that is not a multiple of 4: `cmp/ge` against -6 (the guard range [0xfffffffa, 0x80000000) meets the stride-4 initial range in newStride).
# usage: negguard11.py [bound] -> "WORDS|FUNCS|TABLES" for the generator's raw-words mode
import sys
sys.path.insert(0, __file__.rsplit('/', 1)[0] + '/')
from swtab11 import A, assemble
low = int(sys.argv[1]) if len(sys.argv) > 1 else -6
n = 5
a = A()
a.w(0x6003 | 0 << 8 | 4 << 4)            # mov r4,r0
a.w(0x4008)                              # shll2 r0 : the byte offset, a multiple of 4
a.w(0xE100 | (low & 0xFF))               # mov #low,r1
a.w(0x3003 | 0 << 8 | 1 << 4)            # cmp/ge r1,r0 : r0 >=s low
a.ref('bf', 'default')
a.w(0xE100 | (4 * (n - 1)))              # mov #4*(n-1),r1
a.w(0x3006 | 0 << 8 | 1 << 4)            # cmp/hi r1,r0 : r0 >u 4*(n-1)
a.ref('bt', 'default')
a.w(0x6003 | 2 << 8 | 0 << 4)            # mov r0,r2
a.ref('mova', 'table')
a.w(0x000E | 0 << 8 | 2 << 4)            # mov.l @(r0,r2),r0
a.w(0x402B)                              # jmp @r0
a.w(0x0009)
for c in range(n):
    a.label('c%d' % c)
    a.w(0xE300 | (0x40 + c)); a.w(0x1000 | 5 << 8 | 3 << 4 | c); a.w(0x000B); a.w(0x0009)
a.label('default')
a.w(0xE300 | 0x7F); a.w(0x1000 | 5 << 8 | 3 << 4 | 15); a.w(0x000B); a.w(0x0009)
a.align4()
a.label('table')
for c in range(n): a.long('c%d' % c)
words, labels = assemble(a, [])
jaddr = 2 * [i for i, w in enumerate(words) if w == 0x402B][0]
print("%s|0/0/%X|%X:4:%d:%X:%s" % (" ".join("%04X" % w for w in words), 2 * len(words), jaddr, n, labels['default'], ",".join("%X" % labels['c%d' % c] for c in range(n))))
