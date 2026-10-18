#!/usr/bin/env python3
# Area 2 round 11: a stack buffer written with direct stores, then reached by a loop through an index that is one of two strided constants at the
# loop's start and, after, a value a call leaves in r7 (the engine models r7 as clobbered, so the index phi of the solver gets a full input beside
# the stride-8 constants: the shape of function 0x0602CD36 of hwram007 that makes ValueSet::iterate's union unsound, A2-67).
#   variant 0: the loop loads buf[r7] and sums the loads
#   variant 1: the loop stores the running value to buf[r7] and the function returns buf[4] (offset 16, read directly after the loop)
#   variant 2: as 1 with the frame addressed by r15 itself (no copy in r14): the locals are plain stack variables
# usage: loopfix11.py <variant>   -> the words on stdout, FUNCS for SE_GENERATED_FUNCS on stderr (the callee is outside the function, an unknown call)
import sys
sys.path.insert(0, __file__.rsplit('/', 1)[0] + '/')
from swtab11 import A, assemble

variant = int(sys.argv[1]) if len(sys.argv) > 1 else 0
a = A()
a.w(0x4F22)                          # sts.l pr,@-r15
a.w(0x2F86)                          # mov.l r8,@-r15
a.w(0x2F96)                          # mov.l r9,@-r15
a.w(0x2FA6)                          # mov.l r10,@-r15
a.w(0x2FE6)                          # mov.l r14,@-r15
a.w(0x7FC0)                          # add #-64,r15
a.w(0x6EF3)                          # mov r15,r14
base = 14 if variant < 2 else 15
for k in range(16):
    a.w(0xE100 | (0x10 + k))         # mov #0x10+k,r1
    a.w(0x1000 | base << 8 | 1 << 4 | k)   # mov.l r1,@(k,base)
a.w(0xE200)                          # mov #0,r2
a.w(0x2448)                          # tst r4,r4
a.ref('bt', 'L1')
a.w(0xE202)                          # mov #2,r2
a.label('L1')
a.w(0x6723)                          # mov r2,r7
a.w(0x4708)                          # shll2 r7 : 0 or 8
a.w(0xE800)                          # mov #0,r8
a.w(0x6963)                          # mov r6,r9
a.w(0xE003)                          # mov #3,r0
a.w(0x2909)                          # and r0,r9
a.w(0x7901)                          # add #1,r9 : the loop runs 1 to 4 times
a.label('LOOP')
a.w(0x6073)                          # mov r7,r0
if variant == 0:
    a.w(0x010E | base << 4)          # mov.l @(r0,base),r1
    a.w(0x381C)                      # add r1,r8
else:
    a.w(0x7805)                      # add #5,r8
    a.w(0x0086 | base << 8)          # mov.l r8,@(r0,base)
a.lit(10, 'leafaddr')
a.w(0x4A0B)                          # jsr @r10
a.w(0x0009)
a.w(0x7708)                          # add #8,r7
a.w(0x4910)                          # dt r9
a.ref('bf', 'LOOP')
if variant == 0:
    a.w(0x6083)                      # mov r8,r0
else:
    a.w(0x5004 | base << 4)          # mov.l @(4,base),r0 : buf[4], offset 16
a.w(0x7F40)                          # add #64,r15
a.w(0x6EF6)                          # mov.l @r15+,r14
a.w(0x6AF6)                          # mov.l @r15+,r10
a.w(0x69F6)                          # mov.l @r15+,r9
a.w(0x68F6)                          # mov.l @r15+,r8
a.w(0x4F26)                          # lds.l @r15+,pr
a.w(0x000B)
a.w(0x0009)
a.label('end')
a.label('leaf')
a.w(0x000B)
a.w(0x0009)
a.align4()
a.label('leafaddr')
a.long('leaf')
words, labels = assemble(a, [])
print(" ".join("%04X" % w for w in words))
sys.stderr.write("0/0/%X\n" % labels['end'])
