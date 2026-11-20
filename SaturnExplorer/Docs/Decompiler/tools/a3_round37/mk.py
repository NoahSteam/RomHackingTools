#!/usr/bin/env python3
"""Area 3 round 36 (round 35's fixtures plus the r36 block): writes fixtures.src (A3-243's change under review). Each fixture: F0 pushes r10, sets it to 5, calls F2 (the callee
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
  ("p6", "under: marked word pushed, part store to the neighbour word at r15+4 (a spare word, not the saved PR: round 36 added it), popped (through a copy of r15, as x7)", callg + "mov.l @r15+,r10\nnot r10,r10\nadd #-4,r15\nmov.l r10,@-r15\nmov #1,r1\nmov r15,r2\nadd #4,r2\nmov.b r1,@r2\nmov.l @r15+,r10\nadd #4,r15\n"),
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
  ("x7", "push r10, byte store of r10's own low byte over it, pop (the byte is the same only in the low position) (through a copy of r15: `mov.b Rm,@(disp,Rn)` stores only r0, so the first version never assembled)", "mov r15,r1\nadd #3,r1\nmov.b r10,@r1\nmov.l @r15+,r10\n"),
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
# --- round 36: A3-250 (Load's viaAddress, StoreThrough's mOwnLeaks, mLeakChanged) and A3-251 (Call reading r0, r1, r3) under review ---
# F0 pushes rN, sets rN = r4 + 8 with r4 stored at @rN (a pointer the callee can chase), calls F2 (not lent), stores rN through r4. F2 pushes
# rN first ("push") or not ("none"); G (last function) sets rN to 4. Registers other than r10 by the whole-text substitution.
import re
def sub(t, reg): return re.sub(r'\br10\b', reg, t)
CLOB = {"r8": 0x100, "r9": 0x200, "r10": 0x400, "r11": 0x800, "r12": 0x1000, "r13": 0x2000, "r14": 0x4000, "gbr": 0x20000, "vbr": 0x40000}
def r36(n, why, body, reg="r10", pre="none", ret=None, clob=None):
    h = ptr3
    if pre == "none": h = h.replace("F2:\n.word 0x4F22\nmov.l r10,@-r15\n", "F2:\n.word 0x4F22\n")
    t = sub(h + body + tail.rstrip("\n"), reg)
    m = CLOB[reg] if clob is None else clob
    out_extra.append("# %s: %s\nname %s\nret %s\nenv SE_GENERATED_CLOBBERS=0,%x,%x\n%s" % (n, why, n, ret or "0,0,0", m, m, t))
callg2 = "bsr G\nnop\n"
# a*: the stated residual and its flow-insensitive cousins (does a store the walk made elsewhere hide the mark?)
r36("a1", "residual: F2 stores r10 through r4, then `mov.l @r10,r10` (no push)", "mov.l r10,@r4\nmov.l @r10,r10\n")
r36("a2", "residual through a copy: r1 = r10 stored through r4, then `mov.l @r10,r10`", "mov r10,r1\nmov.l r1,@r4\nmov.l @r10,r10\n")
r36("a3", "store on one path (r3 = 0), the chase on the other, the store's branch first", "tst r3,r3\nbf K1\nmov.l r10,@r4\nbra K2\nnop\nK1:\nmov.l @r10,r10\nK2:\n")
r36("a4", "the chase on one path (r3 != 0), the store on the other, the chase's branch first", "tst r3,r3\nbt K1\nmov.l @r10,r10\nbra K2\nnop\nK1:\nmov.l r10,@r4\nK2:\n")
r36("a5", "the chase, then a store of r10 (changed), then a second chase", "mov.l @r10,r10\nmov.l r10,@r4\nmov.l @r10,r10\n")
r36("a6", "a store of r10 into the F2 frame (not untracked memory), then the chase", "add #-4,r15\nmov.l r10,@r15\nmov.l @r10,r10\nadd #4,r15\n")
r36("a7", "push, store of r10 through r4, chase, frame word dropped without a pop", "mov.l r10,@r4\nmov.l @r10,r10\nadd #4,r15\n", pre="push")
r36("a8", "the store of r10 after an inner call (the call's model lets the pushed word out), then the chase", callg2 + "mov.l r10,@r4\nmov.l @r10,r10\n")
# d*: displacement, post-increment and indexed forms of the chase, other registers
for reg in ("r8", "r9", "r11", "r12", "r13", "r14"):
    r36("d_" + reg, "`mov.l @(4,%s),%s`, no push" % (reg, reg), "mov.l @(4,r10),r10\n", reg=reg)
r36("d_inc", "`mov.l @r10+,r10` no push, r11", "mov.l @r10+,r10\n", reg="r11")
r36("d_idx", "`mov.l @(r0,r10),r10`, r0 = 0", "mov #0,r0\nmov.l @(r0,r10),r10\n")
r36("d_idx2", "`mov.l @(r0,r10),r10`, r0 = 4, push and no pop", "mov #4,r0\nmov.l @(r0,r10),r10\nadd #4,r15\n", pre="push")
r36("d_b", "`mov.b @r10,r10` (a byte load into the pointer register)", "mov.b @r10,r10\n")
r36("d_w", "`mov.w @r10,r10`", "mov.w @r10,r10\n")
r36("d_bi", "`mov.b @r10+,r10`", "mov.b @r10+,r10\n")
r36("d_two", "two chases", "mov.l @r10,r10\nmov.l @r10,r10\n")
r36("d_op", "chase then `add #4,r10` (the mark must ride the op)", "mov.l @r10,r10\nadd #4,r10\n")
r36("d_ext", "chase then `extu.b r10,r10`", "mov.l @r10,r10\nextu.b r10,r10\n")
r36("d_join", "chase on one path, r10 untouched on the other", "tst r3,r3\nbt K1\nmov.l @r10,r10\nK1:\n")
r36("d_call", "chase after an inner call (no push)", callg2 + "mov.l @r10,r10\n")
r36("d_call2", "chase, then an inner call that sets r10, then nothing (G's own change)", "mov.l @r10,r10\n" + callg2)
r36("d_mac", "control: two post-increment loads into r2 leave r10 stepped (a step is said)", "mov.l @r10+,r2\nmov.l @r10+,r2\n")
# f*: a load through a pointer that may be the frame into the register (pushed, not popped)
r36("f1", "push r10; r1 = r15 + r6; `mov.l @r1,r10` (an unknown frame word)", "mov r15,r1\nadd r6,r1\nmov.l @r1,r10\nadd #4,r15\n", pre="push")
r36("f2", "push r10; `mov.l @(r0,r15),r10` with r0 = r6", "mov r6,r0\nmov.l @(r0,r15),r10\nadd #4,r15\n", pre="push")
r36("f3", "push r10; r15 published through r4, read back, loaded through", "mov r15,r1\nmov.l r1,@r4\nmov.l @r4,r2\nmov.l @r2,r10\nadd #4,r15\n", pre="push")
r36("f4", "push r10; `mov r15,r10 ; add r6,r10 ; mov.l @r10,r10`", "mov r15,r10\nadd r6,r10\nmov.l @r10,r10\nadd #4,r15\n", pre="push")
r36("f5", "control: push r10; pop (an exact restore)", "mov.l @r15+,r10\n", pre="push")
r36("f6", "push r10; `mov.l @(r0,r15),r10` with r0 = 0 (the exact slot, an index the walk cannot place)", "mov #0,r0\nmov.l @(r0,r15),r10\nadd #4,r15\n", pre="push")
# g*, v*: GBR and VBR
headg = """.word 0x4F22
bsr F2
nop
stc gbr,r0
mov.l r0,@r4
.word 0x4F26
rts
mov #0,r0
fn
F2:
"""
tailg = """rts
nop
fn
G:
rts
nop
fn
G2:
rts
nop
"""
def r36g(n, why, body, reg="gbr", rd=None):
    rd = rd or ("stc %s,r0" % reg)
    t = headg.replace("stc gbr,r0", rd) + body + tailg
    out_extra.append("# %s: %s\nname %s\nret 0,0,0\nenv SE_GENERATED_CLOBBERS=0,%x,%x\n%s" % (n, why, n, CLOB[reg], CLOB[reg], t.rstrip("\n")))
r36g("g1", "F2: `mov.l @(4,gbr),r0 ; ldc r0,gbr`", "mov.l @(4,gbr),r0\nldc r0,gbr\n")
r36g("g2", "F2: `stc gbr,r1 ; mov.l @r1,r1 ; ldc r1,gbr`", "stc gbr,r1\nmov.l @r1,r1\nldc r1,gbr\n")
r36g("g3", "F2: `stc gbr,r0 ; mov.l @r0,r0 ; ldc r0,gbr`", "stc gbr,r0\nmov.l @r0,r0\nldc r0,gbr\n")
r36g("g4", "F2: `mov.l @(4,gbr),r0 ; mov.l @r0,r0 ; ldc r0,gbr`", "mov.l @(4,gbr),r0\nmov.l @r0,r0\nldc r0,gbr\n")
r36g("g5", "control: F2 saves gbr in r1, chases and restores (r1 kept)", "stc gbr,r1\nstc gbr,r0\nmov.l @r0,r0\nldc r0,gbr\nldc r1,gbr\n")
r36g("g6", "F2 chases gbr into r0 and stores it through r4, gbr untouched", "mov.l @(4,gbr),r0\nmov.l r0,@r4\n")
r36g("g7", "F2: `stc gbr,r1 ; mov.l r1,@r4 ; mov.l @r4,r1 ; ldc r1,gbr` (restored from memory it stored to)", "stc gbr,r1\nmov.l r1,@r4\nmov.l @r4,r1\nldc r1,gbr\n")
r36g("v1", "F2: `stc vbr,r0 ; mov.l @r0,r0 ; ldc r0,vbr`", "stc vbr,r0\nmov.l @r0,r0\nldc r0,vbr\n", reg="vbr", rd="stc vbr,r0")
r36g("v2", "F2: `stc vbr,r1 ; mov.l @r1,r1 ; ldc r1,vbr`", "stc vbr,r1\nmov.l @r1,r1\nldc r1,vbr\n", reg="vbr", rd="stc vbr,r0")
r36g("v3", "control: F2 saves vbr in r1, chases, restores", "stc vbr,r1\nstc vbr,r0\nmov.l @r0,r0\nldc r0,vbr\nldc r1,vbr\n", reg="vbr", rd="stc vbr,r0")
r36g("v4", "F2: `stc vbr,r0 ; add #4,r0 ; ldc r0,vbr` (a step)", "stc vbr,r0\nadd #4,r0\nldc r0,vbr\n", reg="vbr", rd="stc vbr,r0")
# c*: the changed register copied to r0, r1, r3 (A3-251) and a later callee that reads it
hh2 = """.word 0x4F22
mov.l r10,@-r15
mov r4,r10
add #8,r10
mov.l r4,@r10
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
%s
.word 0x4F26
rts
mov r3,r0
fn
H:
%s
fn
G:
mov #4,r10
rts
mov r3,r0
"""
Hs = {
 "t3": "mov #0,r2\ntst r3,r3\nbt HK\nmov #1,r2\nHK:\nmov.l r2,@r4\nrts\nnop\n",
 "t0": "mov #0,r2\ntst r0,r0\nbt HK\nmov #1,r2\nHK:\nmov.l r2,@r4\nrts\nnop\n",
 "t1": "mov #0,r2\ntst r1,r1\nbt HK\nmov #1,r2\nHK:\nmov.l r2,@r4\nrts\nnop\n",
 "m3": "mov.l r3,@r4\nrts\nnop\n",
 "m1": "mov.l r1,@r4\nrts\nnop\n",
 "m0": "mov.l r0,@r4\nrts\nnop\n",
 "ld3": "mov.l @r3,r2\nrts\nnop\n",
 "ld1": "mov.l @r1,r2\nrts\nnop\n",
 "ld0": "mov.l @r0,r2\nrts\nnop\n",
 "j3": "jmp @r3\nnop\n",
 "a3": "add r3,r4\nrts\nmov r4,r0\n",
 "r3": "rts\nmov r3,r0\n",
 "r1": "rts\nmov r1,r0\n",
 "n3": ".word 0x4F22\nbsr K\nnop\n.word 0x4F26\nrts\nnop\nfn\nK:\nmov.l r3,@r4\nrts\nnop\n",
 "ovw": "mov #1,r3\ntst r3,r3\nrts\nnop\n",
}
def r36c(n, why, pre, H, f2="mov.l @r10,r10\n"):
    t = hh2 % (pre, f2.rstrip("\n"), Hs[H].rstrip("\n"))
    out_extra.append("# %s: %s\nname %s\nret 0,0,0\nenv SE_GENERATED_CLOBBERS=0,400,400\n%s" % (n, why, n, t.rstrip("\n")))
r36c("c1", "chase in F2, r10 copied to r3, H tests r3", "mov r10,r3", "m3")
r36c("c2", "r10 copied to r1, H tests r1", "mov r10,r1", "m1")
r36c("c3", "r10 copied to r0, H tests r0", "mov r10,r0", "m0")
r36c("c4", "r10 to r1 to r3, H tests r3", "mov r10,r1\nmov r1,r3", "m3")
r36c("c5", "r10 pushed and popped into r3, H tests r3", "mov.l r10,@-r15\nmov.l @r15+,r3", "m3")
r36c("c6", "r10 stepped into r3, H tests r3", "mov r10,r3\nadd #1,r3", "m3")
r36c("c7", "r10 to r3, H loads through r3", "mov r10,r3", "ld3")
r36c("c8", "r10 to r1, H loads through r1", "mov r10,r1", "ld1")
r36c("c9", "r10 to r0, H loads through r0", "mov r10,r0", "ld0")
r36c("c11", "r10 to r3, H stores r3 through r4", "mov r10,r3", "m3")
r36c("c12", "r10 to r0, H stores r0 through r4", "mov r10,r0", "m0")
r36c("c13", "r10 to r1, H stores r1 through r4", "mov r10,r1", "m1")
r36c("c14", "r10 to r3, H calls K that tests r3 (a nested read)", "mov r10,r3", "n3")
r36c("c15", "control: r10 to r3, H overwrites r3 first", "mov r10,r3", "ovw")
r36c("c16", "r10 to r2, H tests r3 (r3 never set: no chain)", "mov r10,r2", "m3")
r36c("c17", "r10 to r3 by a byte load of a stored copy: r10 stored in the frame, loaded as a byte (a SUBPIECE)", "mov.l r10,@-r15\nmov.b @r15,r3\nadd #4,r15", "m3")
r36c("c18", "r10 to r3 through a pointer store and a load: stored through r4, loaded into r3", "mov.l r10,@r4\nmov.l @r4,r3", "t3")
r36c("c19", "r10 to r3 through the frame at a pushed word, read back after an inner call (G)", "mov.l r10,@-r15\nbsr G\nnop\nmov.l @r15+,r3", "m3")
r36c("c20", "pop-based change (A3-243 shape), r10 copied to r3, H tests r3", "mov r10,r3", "m3", f2="mov.l r10,@-r15\nbsr G\nnop\nmov.l @r15+,r10\nnot r10,r10")
r36c("c21", "chase, then swap.w to r3", "swap.w r10,r3", "m3")
r36c("c22", "chase, then neg to r3", "neg r10,r3", "m3")
r36c("c23", "chase, then `mov r10,r3`, then `shll r3`", "mov r10,r3\nshll r3", "m3")
# w*: over-warning checks
r36("a9", "control: r10 stored through r4, loaded back through r4 (restored from memory it stored to)", "mov.l r10,@r4\nmov.l @r4,r10\n")
r36("a10", "control: r10 stored through itself, loaded back (q4 without the push)", "mov.l r10,@r10\nmov.l @r10,r10\n")
r36("a11", "the same store on both paths, then the chase through the same address", "tst r3,r3\nbt K1\nmov.l r10,@r10\nbra K2\nnop\nK1:\nmov.l r10,@r10\nK2:\nmov.l @r10,r10\n")
r36("a12", "store through r10 on one path only, then the chase after the join (the other path changes r10)", "tst r3,r3\nbt K1\nmov.l r10,@r10\nK1:\nmov.l @r10,r10\n")
r36("a13", "store through r10 on the r3 != 0 path, chase on the r3 = 0 path", "tst r3,r3\nbf K1\nmov.l @r10,r10\nbra K2\nnop\nK1:\nmov.l r10,@r10\nK2:\n")
r36("a14", "store through r10, then `add #4,r10`, chase through the new address", "mov.l r10,@r10\nadd #4,r10\nmov.l @r10,r10\n")
r36("a15", "store of r10 through r4, then r4 stepped, chase through r4 (a different value now)", "mov.l r10,@r4\nadd #4,r4\nmov.l @r4,r10\n")
r36("a16", "push r10; inner call G (its model lets the pushed word out); `mov.l @r4,r10` (r10 loaded from a pointer argument, no restore), frame word dropped", callg2 + "mov.l @r4,r10\nadd #4,r15\n", pre="push")
r36("a17", "no push; r10 stored through r5 (own leak), then `mov.l @r4,r10`", "mov.l r10,@r5\nmov.l @r4,r10\n")
r36("a18", "control: push; inner call; `mov.l @r15+,r10` (the restore)", callg2 + "mov.l @r15+,r10\n", pre="push")
r36("a19", "control: no push; r10 stored through r5, loaded back through r5 after an inner call", "mov.l r10,@r5\n" + callg2 + "mov.l @r5,r10\n")
r36("a20", "control: r10 stored at @(4,r4), loaded back from @(4,r4) (two ops make the address)", "mov.l r10,@(4,r4)\nmov.l @(4,r4),r10\n")
r36("a21", "own store of r10 on the r3 = 0 path (through r5), an unrelated load into r10 on the other (store's branch first)", "tst r3,r3\nbf K1\nmov.l r10,@r5\nbra K2\nnop\nK1:\nmov.l @r4,r10\nK2:\n")
r36("a22", "the same, the load's branch first", "tst r3,r3\nbt K1\nmov.l @r4,r10\nbra K2\nnop\nK1:\nmov.l r10,@r5\nK2:\n")
r36("a23", "control: r10 stored through r5 and loaded back through a copy of r5", "mov.l r10,@r5\nmov r5,r6\nmov.l @r6,r10\n")
r36("a24", "control: r10 stored at @(4,r5), loaded back through r5 + 4 made with an add", "mov.l r10,@(4,r5)\nmov r5,r6\nadd #4,r6\nmov.l @r6,r10\n")
def r36cj(n, why, pre, H, f2="mov.l @r10,r10\n"):
    t = (hh2 % (pre, f2.rstrip("\n"), Hs[H].rstrip("\n"))).replace("bsr H\nnop\n", "mov.l =@H,r2\njsr @r2\nnop\n", 1)
    out_extra.append("# %s: %s\nname %s\nret 0,0,0\nenv SE_GENERATED_CLOBBERS=0,400,400\n%s" % (n, why, n, t.rstrip("\n")))
r36cj("c24", "r10 to r3, H called through `jsr @r2` (target from the literal pool), H stores r3", "mov r10,r3", "m3")
r36cj("c25", "r10 to r1, H called through `jsr @r2`, H stores r1", "mov r10,r1", "m1")
r36("d_loop", "chase in a loop of two, no restore", "mov #2,r5\nK1:\nmov.l @r10,r10\ndt r5\nbf K1\n")
r36("d_loop2", "store of r10 through r4 and a chase, in a loop of two", "mov #2,r5\nK1:\nmov.l r10,@r4\nmov.l @r10,r10\ndt r5\nbf K1\n")
r36("d_loop3", "control: save through r4 and restore through r4 in a loop of two", "mov #2,r5\nK1:\nmov.l r10,@r4\nmov.l @r4,r10\ndt r5\nbf K1\n")
r36("d_pr", "chase through r10 by `mov.l @(0,r10),r10` after a store of r1 through r10 (an own store of another register)", "mov.l r1,@r10\nmov.l @r10,r10\n")
r36("w1", "push r10; chase; pop (restored from the exact slot)", "mov.l @r10,r10\nmov.l @r15+,r10\n", pre="push")
r36("w2", "no push; r1 = r10; chase; r10 = r1 (restored from a copy)", "mov r10,r1\nmov.l @r10,r10\nmov r1,r10\n")
r36("w3", "push r10; r2 = r10; two chases through r2; pop", "mov r10,r2\nmov #2,r5\nK1:\nmov.l @r2,r2\ndt r5\nbf K1\nmov.l @r15+,r10\n", pre="push")
r36("w4", "r1 = chase through r10, stored through r4+4; r10 stored through r4, r10 loaded back from r4 (restored from memory it stored to)", "mov.l @r10,r1\nmov r4,r2\nadd #4,r2\nmov.l r1,@r2\nmov.l r10,@r4\nmov.l @r4,r10\n")
r36("w5", "push r10; chase to r1; pop; chase through r1 again", "mov.l @r10,r1\nmov.l @r15+,r10\nmov.l @r1,r2\n", pre="push")
r36("w6", "push r10; chase into r10; inner call G; pop (restored after an inner call, A3-243's may-entry word)", "mov.l @r10,r10\nbsr G\nnop\nmov.l @r15+,r10\n", pre="push")
r36("w7", "push r10; chase into r10 in a loop of two; pop", "mov #2,r5\nK1:\nmov.l @r10,r10\ndt r5\nbf K1\nmov.l @r15+,r10\n", pre="push")
r36("w8", "push r10; chase into r10 on one path, pop on both", "tst r3,r3\nbt K1\nmov.l @r10,r10\nK1:\nmov.l @r15+,r10\n", pre="push")
r36("w9", "push r10; chase to r1, store through r4, pop, load r1 from r4 again", "mov.l @r10,r1\nmov.l r1,@r4\nmov.l @r15+,r10\nmov.l @r4,r1\n", pre="push")
r36("w10", "push r10; chase, store marked value through r4, pop, load into r10 from r4 (a change)", "mov.l @r10,r1\nmov.l r1,@r4\nmov.l @r15+,r10\nmov.l @r4,r10\n", pre="push")
r36("w11", "r10 saved to r12 (kept by F2's own push); chase; r10 = r12; r12 popped", "mov.l r12,@-r15\nmov r10,r12\nmov.l @r10,r10\nmov r12,r10\nmov.l @r15+,r12\n")
r36("w12", "r10 saved in the F2 frame; chase into r10; restore r10 from the frame slot (frame pointer r14, pushed)", "mov.l r14,@-r15\nmov r15,r14\nadd #-4,r15\nmov.l r10,@(0,r15)\nmov.l @r10,r10\nmov.l @(0,r15),r10\nadd #4,r15\nmov.l @r15+,r14\n")
r36("w13", "push r10; chase through r10 into r1 and test it, then pop", "mov.l @r10,r1\ntst r1,r1\nmov.l @r15+,r10\n", pre="push")
# --- round 37: A3-255 / A3-256 (State::own, OwnKey, SameAddress, Join, the 8-entry cap, mOwnStored, the load rule) under review ---
# r37(): as r36, with extra functions (before G) for an inner callee that writes memory: `extra` is the text of functions separated by `fn`, and
# `clobs` the per-function clobber list (F0, F2, extras..., G).
def r37(n, why, body, reg="r10", pre="none", extra=None, clobs=None, setup=None):
    h = ptr3
    if pre == "none": h = h.replace("F2:\n.word 0x4F22\nmov.l r10,@-r15\n", "F2:\n.word 0x4F22\n")
    t = sub(h + body + tail.rstrip("\n"), reg)
    nex = 0
    if extra:
        t = t.replace("fn\nG:", "fn\n" + extra.rstrip("\n") + "\nfn\nG:")
        nex = extra.count("fn\n") + 1
    m = CLOB[reg]
    ret = ",".join("0" for _ in range(3 + nex))
    cl = clobs or ",".join(["0", "%x" % m] + ["0"] * nex + ["%x" % m])
    out_extra.append("# %s: %s\nname %s\nret %s\nenv SE_GENERATED_CLOBBERS=%s\n%s" % (n, why, n, ret, cl, t))
# b*: a store through an address that is overwritten (by another register, a constant, a narrower store) before the load through the same address
r37("b1", "r10 then r11 stored through r4, `mov.l @r4,r10` (r10 gets r11's entry value)", "mov.l r10,@r4\nmov.l r11,@r4\nmov.l @r4,r10\n")
r37("b1c", "control: r10 then r11 stored through r4, `mov.l @r4,r11` (r11 restored; r10 unchanged)", "mov.l r10,@r4\nmov.l r11,@r4\nmov.l @r4,r11\n")
r37("b2", "r10 stored through r4, then r2 (not callee-saved, = 7) stored through r4, `mov.l @r4,r10`", "mov.l r10,@r4\nmov #7,r2\nmov.l r2,@r4\nmov.l @r4,r10\n")
r37("b3", "r10 stored through r4, then a constant register r1 = 0 through r4, load", "mov.l r10,@r4\nmov #0,r1\nmov.l r1,@r4\nmov.l @r4,r10\n")
r37("b4", "r10 stored through r4, then `mov.b r10,@r4` (top byte overwritten), load word", "mov.l r10,@r4\nmov.b r10,@r4\nmov.l @r4,r10\n")
r37("b5", "r10 stored through r4, `mov.b @r4,r10` (narrow load of the saved word)", "mov.l r10,@r4\nmov.b @r4,r10\n")
r37("b6", "r10 stored through r4, `mov.w @r4,r10`", "mov.l r10,@r4\nmov.w @r4,r10\n")
r37("b7", "r10 stepped by 1, stored through r4, loaded back into r10 (a changed value stored)", "add #1,r10\nmov.l r10,@r4\nmov.l @r4,r10\n")
r37("b8", "r2 = r10 + 1 stored through r4, loaded into r10", "mov r10,r2\nadd #1,r2\nmov.l r2,@r4\nmov.l @r4,r10\n")
r37("b9", "r2 = r10 + r11 stored through r4, loaded into r10", "mov r10,r2\nadd r11,r2\nmov.l r2,@r4\nmov.l @r4,r10\n")
r37("b10", "r10 stored through r4; an inner call G3 (unlent: stores r3 through r4); load into r10", "mov.l r10,@r4\nbsr G3\nnop\nmov.l @r4,r10\n", extra="G3:\nmov.l r3,@r4\nrts\nnop\n")
r37("b10c", "control: the same with G3 not writing memory (stores through r5, r5 = 0x06030000)", "mov.l r10,@r4\nbsr G3\nnop\nmov.l @r4,r10\n", extra="G3:\nmov.l r3,@-r15\nmov.l @r15+,r3\nrts\nnop\n")
r37("b11", "r10 stored through r4, `mov #1,r0 ; mov.w r0,@(2,r4)` (an overlapping store through another key), load word", "mov.l r10,@r4\nmov #1,r0\nmov.w r0,@(2,r4)\nmov.l @r4,r10\n")
r37("b12", "r10 stored through r4, r2 stored through r5 (r5 may alias r4), load through r4", "mov.l r10,@r4\nmov #7,r2\nmov.l r2,@r5\nmov.l @r4,r10\n")
r37("b13", "r10 stored through r4, `mov #1,r0 ; mov.b r0,@(3,r4)` (overlapping byte), load word", "mov.l r10,@r4\nmov #1,r0\nmov.b r0,@(3,r4)\nmov.l @r4,r10\n")
r37("b14", "r10 stored at @(4,r4) twice around a store of 7 at the same address (the update between)", "mov.l r10,@(4,r4)\nmov #7,r2\nmov.l r2,@(4,r4)\nmov.l @(4,r4),r10\n")
r37("b15", "b1 in a loop of two", "mov #2,r5\nK1:\nmov.l r10,@r4\nmov.l r11,@r4\nmov.l @r4,r10\ndt r5\nbf K1\n")
r37("b16", "r10 stored through r4 before the loop; in the loop a constant is stored through r4; load after", "mov.l r10,@r4\nmov #2,r5\nK1:\nmov #7,r2\nmov.l r2,@r4\ndt r5\nbf K1\nmov.l @r4,r10\n")
r37("b17", "store on one path (r3 = 0) of r10 through r4; on the other a constant stored; load after the join", "tst r3,r3\nbf K1\nmov.l r10,@r4\nbra K2\nnop\nK1:\nmov #7,r2\nmov.l r2,@r4\nK2:\nmov.l @r4,r10\n")
r37("b18", "r10 stored through r4 on both paths, one path then stores a constant through r4; load after the join", "mov.l r10,@r4\ntst r3,r3\nbt K1\nmov #7,r2\nmov.l r2,@r4\nK1:\nmov.l @r4,r10\n")
r37("b19", "r10 stored through r4, an unlent inner call writing through r4 on one path only, load after the join", "mov.l r10,@r4\ntst r3,r3\nbt K1\nbsr G3\nnop\nK1:\nmov.l @r4,r10\n", extra="G3:\nmov.l r3,@r4\nrts\nnop\n")
# k*: the base register rewritten between the store and the load; the 8-entry cap
r37("k1", "r10 stored through r4, r4 stepped by 4 and back, load through r4 (restore)", "mov.l r10,@r4\nadd #4,r4\nadd #-4,r4\nmov.l @r4,r10\n")
r37("k2", "r10 stored through r4, r4 reloaded from @r5 (another pointer), load through r4", "mov.l r10,@r4\nmov.l @r5,r4\nmov.l @r4,r10\n")
r37("k3", "r10 stored through r4, r4 = r5, load through r4 (r5 may be another address)", "mov.l r10,@r4\nmov r5,r4\nmov.l @r4,r10\n")
r37("k4", "r10 stored through r4, r6 = r4 (copy), r4 reloaded, load through r6 (restore)", "mov.l r10,@r4\nmov r4,r6\nmov.l @r5,r4\nmov.l @r6,r10\n")
r37("k5", "r10 stored at @(0,r4); eight other stores of r11 at @(4..32,r4) (the cap); load of r10 through r4", "mov.l r10,@r4\n" + "".join("mov.l r11,@(%d,r4)\n" % d for d in range(4, 36, 4)) + "mov.l @r4,r10\n")
r37("k6", "r10 stored at @(0,r4); eight stores of r11 at @(4..32,r4); load r10 through r4 with r11 first stored through r4 too", "mov.l r10,@r4\nmov.l r11,@r4\n" + "".join("mov.l r11,@(%d,r4)\n" % d for d in range(4, 36, 4)) + "mov.l @r4,r10\n")
r37("k7", "r10 stored at 9 distinct addresses; load from the first (evicted)", "".join("mov.l r10,@(%d,r4)\n" % d for d in range(0, 36, 4)) + "mov.l @(0,r4),r10\n")
r37("k8", "r10 stored through r4 and r5; load through r5, after a store of r11 through r5", "mov.l r10,@r5\nmov.l r11,@r5\nmov.l @r5,r10\n")
r37("k9", "r10 stored through r4 with r4 = r5 + 4 made by an add and later loaded through r5 + 4 made again", "mov r5,r1\nadd #4,r1\nmov.l r10,@r1\nmov r5,r2\nadd #4,r2\nmov.l @r2,r10\n")
r37("k10", "loop: r1 loaded from @r5+ each pass; r10 stored through r1 in pass 1, loaded through r1 in pass 2 (same op, another address)", "mov #2,r6\nmov.l r10,@r4\nK1:\nmov.l @r5+,r1\nmov.l @r1,r10\nmov.l r10,@r1\ndt r6\nbf K1\n")
r37("k11", "r1 = loaded pointer; r10 stored through r1; then r1 reloaded by the same op in a loop; load through r1", "mov #2,r6\nK1:\nmov.l @r5+,r1\nmov.l @r1,r10\ntst r3,r3\nbt K2\nmov.l r10,@r1\nK2:\ndt r6\nbf K1\n")
# m*: the frame forms of the load rule (maybe-frame pointers), variable-size frames: over-warning shapes of real code
r37("m1", "variable frame: push r14, r14 = r15, r15 -= r6 (a stack array), restore with `mov r14,r15 ; pop r14`", "mov.l r14,@-r15\nmov r15,r14\nsub r6,r15\nmov r14,r15\nmov.l @r15+,r14\n", reg="r14")
r37("m2", "variable frame: push r14, r14 = r15, `add r6,r15` (a negative size), epilogue `mov r14,r15 ; pop r14`", "mov.l r14,@-r15\nmov r15,r14\nadd r6,r15\nmov r14,r15\nmov.l @r15+,r14\n", reg="r14")
r37("m3", "m1 with a store into the array between", "mov.l r14,@-r15\nmov r15,r14\nsub r6,r15\nmov.l r2,@r15\nmov r14,r15\nmov.l @r15+,r14\n", reg="r14")
r37("m4", "m1 with r10 also pushed and popped around it", "mov.l r10,@-r15\nmov.l r14,@-r15\nmov r15,r14\nsub r6,r15\nmov.l r2,@r15\nmov r14,r15\nmov.l @r15+,r14\nmov.l @r15+,r10\n")
r37("m5", "variable frame, pop through r14 after a load from the array: `mov.l @r15,r1`, restore, pop", "mov.l r14,@-r15\nmov r15,r14\nsub r6,r15\nmov.l @r15,r1\nmov r14,r15\nmov.l @r15+,r14\n", reg="r14")
r37("m6", "r10 and r14 pushed, r15 -= r6, array read at r15 + r0, then `mov r14,r15 ; pops`", "mov.l r10,@-r15\nmov.l r14,@-r15\nmov r15,r14\nsub r6,r15\nmov r6,r0\nmov.l @(r0,r15),r1\nmov r14,r15\nmov.l @r15+,r14\nmov.l @r15+,r10\n")
r37("m7", "r10 pushed; array load through r15 + r6 into r1 (not r10); pop r10", "mov.l r10,@-r15\nmov r15,r1\nadd r6,r1\nmov.l @r1,r2\nmov.l @r15+,r10\n")
r37("m8", "r10 stored through r4, a load through a maybe-frame pointer of r2 (not r10), load r10 back through r4", "mov.l r10,@r4\nmov r15,r1\nadd r6,r1\nmov.l @r1,r2\nmov.l @r4,r10\n")
r37("m9", "r10 and r11 stored through r4; `mov.l @r4,r11` (restore r11); load through a maybe-frame pointer into r1 (r1 only)", "mov.l r10,@r4\nmov.l r11,@r4\nmov r15,r1\nadd r6,r1\nmov.l @r1,r1\n")
r37("b20", "r11 stored through r4, `mov.l @r4,r10` (r10 takes r11's entry value)", "mov.l r11,@r4\nmov.l @r4,r10\n")
r37("b21", "r10 stored through r4, `mov #0,r0 ; mov.l r2,@(r0,r4)` (an index from the same register), load through r4", "mov.l r10,@r4\nmov #0,r0\nmov #7,r2\nmov.l r2,@(r0,r4)\nmov.l @r4,r10\n")
r37("b22", "r10 stored through r4, then through a pointer joined of r4 and r5 (r3 picks), load through r4", "mov.l r10,@r4\nmov r4,r1\ntst r3,r3\nbt K1\nmov r5,r1\nK1:\nmov #7,r2\nmov.l r2,@r1\nmov.l @r4,r10\n")
r37("b23", "r10 stored at an absolute address (literal), a constant stored there, loaded back", "mov.l =0x06030000,r1\nmov.l r10,@r1\nmov #7,r2\nmov.l r2,@r1\nmov.l @r1,r10\n")
r37("b24", "control: r10 stored at an absolute address (literal), loaded back (a restore)", "mov.l =0x06030000,r1\nmov.l r10,@r1\nmov.l @r1,r10\n")
r37("b25", "r10 stored at an absolute address, a byte stored one byte in (overlap, another register holds the same literal), load back", "mov.l =0x06030000,r1\nmov.l r10,@r1\nmov.l =0x06030001,r2\nmov #7,r0\nmov.b r0,@r2\nmov.l @r1,r10\n")
r37("b26", "control: r10 stored through r4 and r11 through r4+4, both loaded back (distinct words)", "mov.l r10,@r4\nmov.l r11,@(4,r4)\nmov.l @(4,r4),r11\nmov.l @r4,r10\n")
r37("b27", "r10 stored through r4, r11 stored through r4+4, load r10 through r4 (restore) then `mov.l @(4,r4),r10` (r10 gets r11's)", "mov.l r10,@r4\nmov.l r11,@(4,r4)\nmov.l @r4,r10\nmov.l @(4,r4),r10\n")
r37("b28", "control: r10 stored via post-decrement `mov.l r10,@-r4`, loaded back via `mov.l @r4+,r10`", "mov.l r10,@-r4\nmov.l @r4+,r10\n")
r37("b29", "control: save r10 at @r4, inner call G that touches no memory (register-only), load back (a19's shape: stays noted by design)", "mov.l r10,@r4\nbsr G\nnop\nmov.l @r4,r10\n")
ctx = ["r8", "r9", "r10", "r11", "r12", "r13", "r14"]
r37("k12", "control: context save of r8-r14 through r4, then restore of all of them through r4 (hand-written context code)", "".join("mov.l %s,@(%d,r4)\n" % (r, 4 * i) for i, r in enumerate(ctx)) + "".join("mov.l @(%d,r4),%s\n" % (4 * i, r) for i, r in enumerate(ctx)))
r37("k13", "control: save of r8-r14, gbr and vbr (nine words), restore of all, through r4", "".join("mov.l %s,@(%d,r4)\n" % (r, 4 * i) for i, r in enumerate(ctx)) + "stc gbr,r1\nmov.l r1,@(28,r4)\nstc vbr,r1\nmov.l r1,@(32,r4)\n" + "".join("mov.l @(%d,r4),%s\n" % (4 * i, r) for i, r in enumerate(ctx)) + "mov.l @(28,r4),r1\nldc r1,gbr\nmov.l @(32,r4),r1\nldc r1,vbr\n")
r37("k14", "control: save r8-r14 by `mov.l rN,@-r4`, restore by `mov.l @r4+,rN` in reverse", "".join("mov.l %s,@-r4\n" % r for r in ctx) + "".join("mov.l @r4+,%s\n" % r for r in reversed(ctx)))
r37("m10", "variable frame given back by an add: push r14, `sub r6,r15 ; add r6,r15`, pop r14 (r15 is a lost copy of the frame pointer)", "mov.l r14,@-r15\nsub r6,r15\nadd r6,r15\nmov.l @r15+,r14\n", reg="r14")
r37("m11", "m10 with r10 pushed too (both popped through the lost r15)", "mov.l r10,@-r15\nmov.l r14,@-r15\nsub r6,r15\nadd r6,r15\nmov.l @r15+,r14\nmov.l @r15+,r10\n")
r37("m12", "r15 saved in r1 around an alloca, restored from r1, pops", "mov.l r10,@-r15\nmov r15,r1\nsub r6,r15\nmov r1,r15\nmov.l @r15+,r10\n")
r37("m13", "alloca by `add r6,r15` with r6 a negative size built in the function (r6 = -16), `add #16,r15`, pop", "mov.l r10,@-r15\nmov #-16,r2\nadd r2,r15\nadd #16,r15\nmov.l @r15+,r10\n")
r37("m14", "`mov.l @(4,r14),r10` restore through the frame pointer after a lowered r15 (r14 = r15 after the push)", "mov.l r14,@-r15\nmov.l r10,@-r15\nmov r15,r14\nsub r6,r15\nmov.l @(0,r14),r10\nmov r14,r15\nadd #4,r15\nmov.l @r15+,r14\n")
r37("b30", "r10 changed on one path (r3 picks), the join stored through r4 and loaded back (a join is the entry value on one path only)", "tst r3,r3\nbt K1\nmov #9,r10\nK1:\nmov.l r10,@r4\nmov.l @r4,r10\n")
r37("b31", "control: push r10, inner call G, pop (a word an inner call's model may have written), store through r4, load back", "bsr G\nnop\nmov.l @r15+,r10\nmov.l r10,@r4\nmov.l @r4,r10\n", pre="push")
r37("b32", "push r10, inner call G, pop, `add #1,r10` stored through r4 and loaded back (a stepped may-entry word)", "bsr G\nnop\nmov.l @r15+,r10\nadd #1,r10\nmov.l r10,@r4\nmov.l @r4,r10\n", pre="push")
r37("b33", "r10 stored through r4 before a loop; in the loop it is loaded back and a constant is stored over it (the second pass loads 7)", "mov.l r10,@r4\nmov #2,r5\nK1:\nmov.l @r4,r10\nmov #7,r2\nmov.l r2,@r4\ndt r5\nbf K1\n")
r37("b34", "a loop: r10 stepped by 1, stored through r4, loaded back, twice (a changed value on every pass)", "mov #2,r5\nK1:\nadd #1,r10\nmov.l r10,@r4\nmov.l @r4,r10\ndt r5\nbf K1\n")
r37("b35", "a loop of the save and restore (d_loop3 again) with an inner stepping of r4 by 4 and back", "mov #2,r5\nK1:\nmov.l r10,@r4\nadd #4,r4\nadd #-4,r4\nmov.l @r4,r10\ndt r5\nbf K1\n")
out = ["# Area 3 round 37 review (mk.py): A3-255 and A3-256's change. Run: W=<SaturnExplorer> S=<scratch> run.sh [name]"]
for n, why, body in fx:
    out.append("# %s: %s" % (n, why))
    out.append("name %s\nret 0,0,0\nenv SE_GENERATED_CLOBBERS=0,400,400" % n)
    out.append(head + body + tail.rstrip("\n"))
out += out_extra
open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "fixtures.src"), "w").write("\n".join(out) + "\n")
