#!/usr/bin/env python3
"""Area 3 round 35: writes fixtures.src (A3-243's change under review). Each fixture: F0 pushes r10, sets it to 5, calls F2 (the callee
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
    out_extra.append("# %s: %s\nname %s\nret 0,0,0\nenv SE_GENERATED_CLOBBERS=0,400,400\n%s" % (n, why, n, ptr + body + tail.rstrip("\n")))
big = head.replace("mov #5,r10\n", "mov.l =0x12345678,r10\n")
for n, why, body in [("e1", "extu.b after the pop (entry value with high bits)", callg + "mov.l @r15+,r10\nextu.b r10,r10\n"),
                     ("e2", "extu.w after the pop", callg + "mov.l @r15+,r10\nextu.w r10,r10\n"),
                     ("e3", "exts.b after the pop", callg + "mov.l @r15+,r10\nexts.b r10,r10\n"),
                     ("e4", "swap.w after the pop", callg + "mov.l @r15+,r10\nswap.w r10,r10\n"),
                     ("e5", "and of a constant register after the pop", callg + "mov.l @r15+,r10\nmov #15,r1\nand r1,r10\n"),
                     ("e6", "shlr16 after the pop", callg + "mov.l @r15+,r10\nshlr16 r10\n"),
                     ("e7", "control: nothing after the pop", callg + "mov.l @r15+,r10\n"),
                     ("e8", "extu.b after the pop, popped through a joined push (A3-243's shape)", "tst r3,r3\nbf K1\nmov #9,r10\nK1:\nmov.l r10,@-r15\n" + callg + "mov.l @r15+,r10\nextu.b r10,r10\nmov.l @r15+,r2\n")]:
    out_extra.append("# %s: %s\nname %s\nret 0,0,0\nenv SE_GENERATED_CLOBBERS=0,400,400\n%s" % (n, why, n, big + body + tail.rstrip("\n")))
# --- round 35: A3-246's change under review (over-warning, then the two spots round 34 read only from code) ---
# o*: r10 = 5 going in (high bits match for the extensions), an unknown operand (r3: a run input) for and/or/xor.
r35 = []
for n, why, body in [
  ("o1", "over: or with an unknown operand r3 (0 in some runs)", callg + "mov.l @r15+,r10\nor r3,r10\n"),
  ("o2", "over: xor with an unknown operand r3", callg + "mov.l @r15+,r10\nxor r3,r10\n"),
  ("o3", "over: and with an unknown operand r3 (all ones in some runs)", callg + "mov.l @r15+,r10\nand r3,r10\n"),
  ("o4", "over: extu.b of 5 (high bits already zero)", callg + "mov.l @r15+,r10\nextu.b r10,r10\n"),
  ("o5", "over: extu.w of 5", callg + "mov.l @r15+,r10\nextu.w r10,r10\n"),
  ("o6", "over: exts.b of 5", callg + "mov.l @r15+,r10\nexts.b r10,r10\n"),
  ("o7", "over: exts.w of 5", callg + "mov.l @r15+,r10\nexts.w r10,r10\n"),
  ("o8", "over: SUBPIECE in a store path: push the popped word, store its low byte over it, pop", callg + "mov.l @r15+,r10\nmov.l r10,@-r15\nmov.b r10,@r15\nmov.l @r15+,r10\n"),
  ("o9", "over: low half stored over the pushed word, popped", callg + "mov.l @r15+,r10\nmov.l r10,@-r15\nmov.w r10,@r15\nmov.l @r15+,r10\n"),
  ("o10", "over: byte of r10 stored through r4 only (nothing written back to r10)", callg + "mov.l @r15+,r10\nmov.b r10,@r4\n"),
  ("o11", "over: shll of 5 then shlr (two steps back)", callg + "mov.l @r15+,r10\nshll r10\nshlr r10\n"),
  ("o12", "over: not twice", callg + "mov.l @r15+,r10\nnot r10,r10\nnot r10,r10\n"),
  ("o13", "over: neg twice", callg + "mov.l @r15+,r10\nneg r10,r10\nneg r10,r10\n"),
  ("o14", "over: xor with a constant register twice", callg + "mov.l @r15+,r10\nmov #3,r1\nxor r1,r10\nxor r1,r10\n"),
  ("o15", "over: copy into r2, not, and the copy not read as r10 (r10 itself untouched)", callg + "mov.l @r15+,r10\nmov r10,r2\nnot r2,r2\nmov.l r2,@r4\n"),
  # p*: marks that may be lost
  ("p1", "under: part store (byte) into a pushed marked word, popped back into r10", callg + "mov.l @r15+,r10\nnot r10,r10\nmov.l r10,@-r15\nmov #1,r1\nmov.b r1,@r15\nmov.l @r15+,r10\n"),
  ("p2", "under: part store (word) into a pushed marked word, popped back", callg + "mov.l @r15+,r10\nnot r10,r10\nmov.l r10,@-r15\nmov #1,r1\nmov.w r1,@r15\nmov.l @r15+,r10\n"),
  ("p3", "under: part store of r10's own byte over the pushed marked word", callg + "mov.l @r15+,r10\nnot r10,r10\nmov.l r10,@-r15\nmov.b r10,@r15\nmov.l @r15+,r10\n"),
  ("p4", "under: part store of r3 over the pushed marked word (r3 unknown)", callg + "mov.l @r15+,r10\nneg r10,r10\nmov.l r10,@-r15\nmov.b r3,@r15\nmov.l @r15+,r10\n"),
  ("p5", "under: pushed marked word, a second inner call G over it, popped", callg + "mov.l @r15+,r10\nnot r10,r10\nmov.l r10,@-r15\nbsr G\nnop\nmov.l @r15+,r10\n"),
  ("p6", "under: marked word pushed, part store to the neighbour word at r15+4 (untouched), popped", callg + "mov.l @r15+,r10\nnot r10,r10\nmov.l r10,@-r15\nmov #1,r1\nmov.b r1,@(4,r15)\nmov.l @r15+,r10\n"),
  ("p7", "under: marked word stored to a frame slot with r15 offset, popped by a load at the displacement", callg + "mov.l @r15+,r10\nnot r10,r10\nadd #-8,r15\nmov.l r10,@(4,r15)\nmov.l @(4,r15),r10\nadd #8,r15\n"),
  ("p8", "under: marked word pushed, byte store over it with an unknown address (r4 +0 into the stack?) not applicable; byte into the word through r1 = r15", callg + "mov.l @r15+,r10\nnot r10,r10\nmov.l r10,@-r15\nmov r15,r1\nmov #1,r2\nmov.b r2,@r1\nmov.l @r15+,r10\n"),
  ]:
    r35.append((n, why, body))
# x*: no inner call at all: a part store into a pushed (saved) word, popped
for n, why, body in [
  ("x1", "push r10, byte store of 1 over it (big-endian: the top byte), pop: no inner call", "mov #1,r1\nmov.b r1,@r15\nmov.l @r15+,r10\n"),
  ("x2", "push r10, halfword store of 1 over it, pop", "mov #1,r1\nmov.w r1,@r15\nmov.l @r15+,r10\n"),
  ("x3", "push r10, byte store of r10's own low byte at the top, pop", "mov.b r10,@r15\nmov.l @r15+,r10\n"),
  ("x4", "push r10, a full-word store of 1 over it, pop (control: exact, said by A3-197)", "mov #1,r1\nmov.l r1,@r15\nmov.l @r15+,r10\n"),
  ("x5", "push r10, byte store through a copy of r15, pop", "mov r15,r1\nmov #1,r2\nmov.b r2,@r1\nmov.l @r15+,r10\n"),
  ("x6", "push r10, inner call, byte store of 1 over it, pop", callg + "mov #1,r1\nmov.b r1,@r15\nmov.l @r15+,r10\n"),
  ("x7", "push r10, byte store of r10's own low byte over it, pop (the byte is the same only in the low position)", "mov.b r10,@(3,r15)\nmov.l @r15+,r10\n"),
  ]:
    out_extra.append("# %s: %s\nname %s\nret 0,0,0\nenv SE_GENERATED_CLOBBERS=0,400,400\n%s" % (n, why, n, head + body + tail.rstrip("\n")))
# q*: a marked value through memory (pointer store, load); the pointer is r10's own entry value in q1..q3 (r10 = r4 + 8 going in)
for n, why, body in [
  ("q1", "under: pop; step r10 by 4; store r10 at @r10; load it back into r10 (address names r10)", callg + "mov.l @r15+,r10\nadd #4,r10\nmov.l r10,@r10\nmov.l @r10,r10\n"),
  ("q2", "under: pop; copy r10 to r1; step r10; store through r1; load through r1 into r10", callg + "mov.l @r15+,r10\nmov r10,r1\nadd #4,r10\nmov.l r10,@r1\nmov.l @r1,r10\n"),
  ("q3", "under: pop; not r10 (r10 is a pointer so ~r10 is not valid memory); control with a clean copy: copy r10 to r1, step r1, store r1 through r10, load r10 from it", callg + "mov.l @r15+,r10\nmov r10,r1\nadd #4,r1\nmov.l r1,@r10\nmov.l @r10,r10\n"),
  ("q5", "under: pop; step r10 by 4; store r10 through r4 (a pointer argument); load it back into r10 (address names r4 only)", callg + "mov.l @r15+,r10\nadd #4,r10\nmov.l r10,@r4\nmov.l @r4,r10\n"),
  ("q6", "under: the same with `not r10` (no pointer use of the marked value)", callg + "mov.l @r15+,r10\nnot r10,r10\nmov.l r10,@r4\nmov.l @r4,r10\n"),
  ("q7", "control, no mark: the callee overwrites r10 with 7 after the pop, stores it through r4, loads it back", callg + "mov.l @r15+,r10\nmov #7,r10\nmov.l r10,@r4\nmov.l @r4,r10\n"),
  ("q8", "control, no pop: r10 = r3 stored through r4 and loaded back", "mov r3,r10\nmov.l r10,@r4\nmov.l @r4,r10\nmov.l @r15+,r2\n"),
  ("q4", "control: pop; store r10 through r10; load it back (unchanged)", callg + "mov.l @r15+,r10\nmov.l r10,@r10\nmov.l @r10,r10\n"),
  ]:
    out_extra.append("# %s: %s\nname %s\nret 0,0,0\nenv SE_GENERATED_CLOBBERS=0,400,400\n%s" % (n, why, n, ptr + body + tail.rstrip("\n")))
ptr3 = ptr.replace('add #8,r10\n', 'add #8,r10\nmov.l r4,@r10\n')
# l*: no push at all in F2 (r10 is a pointer going in and the callee's code uses it): the load's address names r10
for n, why, body in [
  ("l1", "r10 = r4 + 8 going in; F2 walks `mov.l @r10,r10` (no push, no inner call)", "mov.l @r10,r10\n"),
  ("l2", "same, after an inner call", callg + "mov.l @r10,r10\n"),
  ("l3", "r10 stepped by 4 and loaded through (`mov.l @(4,r10)` style: post-increment)", "mov.l @r10+,r10\n"),
  ]:
    out_extra.append("# %s: %s\nname %s\nret 0,0,0\nenv SE_GENERATED_CLOBBERS=0,400,400\n%s" % (n, why, n, ptr3.replace("F2:\n.word 0x4F22\nmov.l r10,@-r15\n", "F2:\n.word 0x4F22\n") + body + tail.rstrip("\n")))
# h*: the 13102045 shape by hand: the caller reads the changed register into r3 (not a convention argument) and calls a later callee H that stores r3
hh = """.word 0x4F22
mov.l r10,@-r15
mov #5,r10
bsr F2
nop
%s
bsr H
nop
mov.l @r15+,r10
.word 0x4F26
rts
mov #0,r0
fn
F2:
.word 0x4F22
mov.l r10,@-r15
mov #4,r10
mov.l @r15+,r2
.word 0x4F26
rts
mov r3,r0
fn
H:
mov.l r3,@r4
rts
mov r3,r0
"""
for n, why, pre in [
  ("h1", "r10 (changed by F2 on every path, never restored) copied to r3, H stores r3", "mov r10,r3"),
  ("h2", "control: r10 copied to r0 (a plain read) and stored through r4 by the caller", "mov r10,r0\nmov.l r0,@r4"),
  ("h3", "r10 copied to r4 (a convention argument) and H stores r3 (the value the caller never set)", "mov r10,r4"),
  ]:
    out_extra.append("# %s: %s\nname %s\nret 0,0,0\nenv SE_GENERATED_CLOBBERS=0,400,400\n%s" % (n, why, n, hh % pre))
for n, why, pre in [
  ("h4", "gbr (loaded by F2 on every path, never restored) read into r3 by `stc gbr,r3`, H stores r3", "stc gbr,r3"),
  ("h5", "as h4 with r0 as the copy and the caller storing it (a plain read after the call)", "stc gbr,r0\nmov.l r0,@r4"),
  ]:
    out_extra.append("# %s: %s\nname %s\nret 0,0,0\nenv SE_GENERATED_CLOBBERS=0,400,400\n%s" % (n, why, n, (hh % pre).replace("mov #4,r10\n", "mov #4,r1\nldc r1,gbr\n")))
for n, why, pre, pre2 in [
  ("h6", "h4 with `tas.b @r4` in F2 before the ldc", "stc gbr,r3", "tas.b @r4\n"),
  ("h7", "h4 with `or #1,r0`-free control: a nop in place of the tas", "stc gbr,r3", "nop\n"),
  ]:
    out_extra.append("# %s: %s\nname %s\nret 0,0,0\nenv SE_GENERATED_CLOBBERS=0,400,400\n%s" % (n, why, n, (hh % pre).replace("mov #4,r10\n", pre2 + "mov #4,r1\nldc r1,gbr\n")))
for n, why, pre, pre2 in [
  ("h8", "F2: `tas.b @r4 ; mov r5,r10` (no pop, r10 overwritten with a parameter), the caller stores r10", "mov r10,r0\nmov.l r0,@r4", "tas.b @r4\nmov r5,r10\n"),
  ("h9", "control: the same without the tas.b", "mov r10,r0\nmov.l r0,@r4", "mov r5,r10\n"),
  ("h10", "tas.b @r4 then `ldc r5,gbr`, the caller reads gbr (stc gbr,r0 ; store)", "stc gbr,r0\nmov.l r0,@r4", "tas.b @r4\nldc r5,gbr\n"),
  ("h11", "control: `ldc r5,gbr` without the tas.b", "stc gbr,r0\nmov.l r0,@r4", "ldc r5,gbr\n"),
  ]:
    out_extra.append("# %s: %s\nname %s\nret 0,0,0\nenv SE_GENERATED_CLOBBERS=0,400,400\n%s" % (n, why, n, (hh % pre).replace("mov #4,r10\n", pre2)))
for n, why, body in r35:
    use = big if n.startswith("e") else head
    out_extra.append("# %s: %s\nname %s\nret 0,0,0\nenv SE_GENERATED_CLOBBERS=0,400,400\n%s" % (n, why, n, use + body + tail.rstrip("\n")))
out = ["# Area 3 round 35 review (mk.py): A3-243's change. Run: W=<SaturnExplorer> S=<scratch> run.sh [name]"]
for n, why, body in fx:
    out.append("# %s: %s" % (n, why))
    out.append("name %s\nret 0,0,0\nenv SE_GENERATED_CLOBBERS=0,400,400" % n)
    out.append(head + body + tail.rstrip("\n"))
out += out_extra
open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "fixtures.src"), "w").write("\n".join(out) + "\n")
