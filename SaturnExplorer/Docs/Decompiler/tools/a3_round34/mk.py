#!/usr/bin/env python3
"""Area 3 round 34: writes fixtures.src (A3-243's change under review). Each fixture: F0 pushes r10, sets it to 5, calls F2 (the callee
under test, not lent), stores r10 through r4, pops, returns; G (the last function) sets r10 to 4. The body of F2 is given per fixture."""
import os
head = """.word 0x4F22
mov.l r10,@-r15
mov #5,r10
bsr F2
nop
mov.l r10,@r4
mov.l @r15+,r10
.word 0x4F26
rts
mov #0,r0
fn
F2:
.word 0x4F22
mov.l r10,@-r15
"""
tail = """.word 0x4F26
rts
mov r3,r0
fn
G:
mov #4,r10
rts
mov r3,r0
"""
callg = "bsr G\nnop\n"
out_extra = []
fx = [
 ("s1", "over: pop after an inner call, step by 2 and back by 2: r10 comes back as it went in", callg + "mov.l @r15+,r10\nadd #2,r10\nadd #-2,r10\n"),
 ("s2", "over: post-increment load, then a step back", callg + "mov.l @r15+,r10\nmov.l @r10+,r2\nadd #-4,r10\n"),
 ("s3", "over: the step is on a copy that is not r10 (r1 = r10 + 4), r10 itself untouched", callg + "mov.l @r15+,r10\nmov r10,r1\nadd #4,r1\nmov.l r1,@r4\n"),
 ("s4", "over: the popped word stepped in r11 (saved and restored by F2): r10 not stepped", "mov.l r11,@-r15\n" + callg + "mov.l @r15+,r11\nadd #2,r11\nadd #-2,r11\nmov.l @r15+,r10\n"),
 ("s5", "over: stepped by 2 on one path, back on the other path's join (two adds of +2 and -2 on different paths do not restore)", callg + "mov.l @r15+,r10\ntst r3,r3\nbf K1\nadd #2,r10\nK1:\nadd #-2,r10\n"),
 ("j1", "over: save A, change on a path, save B (a join), call, pop B then A: the entry value", "tst r3,r3\nbf K1\nmov #9,r10\nK1:\nmov.l r10,@-r15\n" + callg + "mov.l @r15+,r10\nmov.l @r15+,r10\n"),
 ("j2", "r10 changed on a path, saved, popped into r2, r10 restored from A", "mov.l r11,@-r15\ntst r3,r3\nbf K1\nmov #9,r10\nK1:\nmov.l r10,@-r15\n" + callg + "mov.l @r15+,r2\nmov.l @r15+,r11\nmov.l @r15+,r10\n"),
 ("j3", "the joined word popped to r11 (changed) while r10 is restored by the epilogue: r11 changes", "mov.l r11,@-r15\ntst r3,r3\nbf K1\nmov #9,r10\nK1:\nmov.l r10,@-r15\n" + callg + "mov.l @r15+,r11\nmov.l @r15+,r2\nmov.l @r15+,r10\n"),
 ("u1", "under: pop after the inner call, then not r10", callg + "mov.l @r15+,r10\nnot r10,r10\n"),
 ("u2", "under: shll2", callg + "mov.l @r15+,r10\nshll2 r10\n"),
 ("u3", "under: add of a register that holds a constant", callg + "mov.l @r15+,r10\nmov #8,r1\nadd r1,r10\n"),
 ("u4", "under: dt", callg + "mov.l @r15+,r10\ndt r10\n"),
 ("u5", "under: neg", callg + "mov.l @r15+,r10\nneg r10,r10\n"),
 ("u6", "under: xor with a register holding a constant", callg + "mov.l @r15+,r10\nmov #3,r1\nxor r1,r10\n"),
 ("u7", "under: sub of a register holding a constant", callg + "mov.l @r15+,r10\nmov #8,r1\nsub r1,r10\n"),
 ("u8", "under: or with a constant register", callg + "mov.l @r15+,r10\nmov #8,r1\nor r1,r10\n"),
 ("u9", "under: pre-decrement store through the popped pointer", callg + "mov.l @r15+,r10\nmov.l r2,@-r10\n"),
 ("u10", "under: add through a register that holds a pool constant", callg + "mov.l @r15+,r10\nmov.l =0x00000100,r1\nadd r1,r10\n"),
 ("u11", "under: step before the pop is read by r10's later use: add #2 on a copy moved back", callg + "mov.l @r15+,r10\nmov r10,r1\nadd #2,r1\nmov r1,r10\n"),
]
ptr = head.replace("mov #5,r10\n", "mov r4,r10\nadd #8,r10\n")
for n, why, body in [("s2p", "over: post-increment load through a real pointer, then a step back", callg + "mov.l @r15+,r10\nmov.l @r10+,r2\nadd #-4,r10\n"),
                     ("u9p", "under: pre-decrement store through the popped pointer", callg + "mov.l @r15+,r10\nmov.l r2,@-r10\n"),
                     ("u12", "under: post-increment store-free read, pointer left stepped", callg + "mov.l @r15+,r10\nmov.l @r10+,r2\n")]:
    out_extra.append("# %s: %s\nname %s\nret 0,0,0\nenv SE_GENERATED_CLOBBERS=0,400,0\n%s" % (n, why, n, ptr + body + tail.rstrip("\n")))
out = ["# Area 3 round 34 review (mk.py): A3-243's change. Run: W=<SaturnExplorer> S=<scratch> run.sh [name]"]
for n, why, body in fx:
    out.append("# %s: %s" % (n, why))
    out.append("name %s\nret 0,0,0\nenv SE_GENERATED_CLOBBERS=0,400,0" % n)
    out.append(head + body + tail.rstrip("\n"))
out += out_extra
open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "fixtures.src"), "w").write("\n".join(out) + "\n")
