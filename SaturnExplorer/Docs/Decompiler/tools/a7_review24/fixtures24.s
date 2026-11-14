a7_171_base|06009000|VOLATILE=26001000-2600100f
 mov.l =0x26001008,r6
 mov.w @r4,r3
 sub r3,r2
 tst r0,r5
 bt S
 cmp/eq r1,r3
 bf/s M
S: mov.b r3,@r6
 mov.b r0,@r4
M: tst r1,r4
 bf N
cmp/eq r3,r0
 bt/s E
K: mov.b r2,@r6
 mov.w @r4,r2
E: rts
 nop
N: bra K
 nop
==
a7_171_input_r3|06009000|VOLATILE=26001000-2600100f
 mov.l =0x26001008,r6
 nop
 sub r3,r2
 tst r0,r5
 bt S
 cmp/eq r1,r3
 bf/s M
S: mov.b r3,@r6
 mov.b r0,@r4
M: tst r1,r4
 bf N
cmp/eq r3,r0
 bt/s E
K: mov.b r2,@r6
 mov.w @r4,r2
E: rts
 nop
N: bra K
 nop
==
a7_171_param_r7|06009000|VOLATILE=26001000-2600100f
 mov.l =0x26001008,r6
 nop
 sub r7,r2
 tst r0,r5
 bt S
 cmp/eq r1,r7
 bf/s M
S: mov.b r7,@r6
 mov.b r0,@r4
M: tst r1,r4
 bf N
cmp/eq r7,r0
 bt/s E
K: mov.b r2,@r6
 mov.w @r4,r2
E: rts
 nop
N: bra K
 nop
==
a7_171_phi|06009000|VOLATILE=26001000-2600100f
 mov.l =0x26001008,r6
 tst r0,r7
 bt P
 mov.w @r4,r3
 bra Q
 nop
P: mov.b @r5,r3
Q: sub r3,r2
 tst r0,r5
 bt S
 cmp/eq r1,r3
 bf/s M
S: mov.b r3,@r6
 mov.b r0,@r4
M: tst r1,r4
 bf N
cmp/eq r3,r0
 bt/s E
K: mov.b r2,@r6
 mov.w @r4,r2
E: rts
 nop
N: bra K
 nop
==
a7_171_add|06009000|VOLATILE=26001000-2600100f
 mov.l =0x26001008,r6
 mov.w @r4,r3
 add r7,r3
 sub r3,r2
 tst r0,r5
 bt S
 cmp/eq r1,r3
 bf/s M
S: mov.b r3,@r6
 mov.b r0,@r4
M: tst r1,r4
 bf N
cmp/eq r3,r0
 bt/s E
K: mov.b r2,@r6
 mov.w @r4,r2
E: rts
 nop
N: bra K
 nop
==
a7_171_slot|06009000|VOLATILE=26001000-2600100f
 mov.l =0x26001008,r6
 bra X
 mov.w @r4,r3
X: sub r3,r2
 tst r0,r5
 bt S
 cmp/eq r1,r3
 bf/s M
S: mov.b r3,@r6
 mov.b r0,@r4
M: tst r1,r4
 bf N
cmp/eq r3,r0
 bt/s E
K: mov.b r2,@r6
 mov.w @r4,r2
E: rts
 nop
N: bra K
 nop
==
a7_171_move|06009000|VOLATILE=26001000-2600100f
 mov.l =0x26001008,r6
 mov.w @r4,r1
 mov r1,r3
 sub r3,r2
 tst r0,r5
 bt S
 cmp/eq r1,r3
 bf/s M
S: mov.b r3,@r6
 mov.b r0,@r4
M: tst r1,r4
 bf N
cmp/eq r3,r0
 bt/s E
K: mov.b r2,@r6
 mov.w @r4,r2
E: rts
 nop
N: bra K
 nop
==
a7_171_word|06009000|VOLATILE=26001000-2600100f
 mov.l =0x26001008,r6
 mov.l @r4,r3
 sub r3,r2
 tst r0,r5
 bt S
 cmp/eq r1,r3
 bf/s M
S: mov.w r3,@r6
 mov.b r0,@r4
M: tst r1,r4
 bf N
 cmp/eq r3,r0
 bt/s E
K: mov.w r2,@r6
 mov.w @r4,r2
E: rts
 nop
N: bra K
 nop
==
a7_171_bt|06009000|VOLATILE=26001000-2600100f
 mov.l =0x26001008,r6
 mov.w @r4,r3
 sub r3,r2
 tst r0,r5
 bt S
 cmp/eq r1,r3
 bf/s M
S: mov.b r3,@r6
 mov.b r0,@r4
M: tst r1,r4
 bt N
 cmp/eq r3,r0
 bt/s E
K: mov.b r2,@r6
 mov.w @r4,r2
E: rts
 nop
N: bra K
 nop
==
a7_171_nobra|06009000|VOLATILE=26001000-2600100f
 mov.l =0x26001008,r6
 mov.w @r4,r3
 sub r3,r2
 tst r0,r5
 bt S
 cmp/eq r1,r3
 bf/s M
S: mov.b r3,@r6
 mov.b r0,@r4
M: tst r1,r4
 bf E
 mov.b r2,@r6
 mov.w @r4,r2
E: rts
 nop
==
a7_171_loop|06009000|VOLATILE=26001000-2600100f
 mov.l =0x26001008,r6
 mov #3,r7
L: mov.w @r4,r3
 sub r3,r2
 tst r0,r5
 bt S
 cmp/eq r1,r3
 bf/s M
S: mov.b r3,@r6
 mov.b r0,@r4
M: tst r1,r4
 bf N
 cmp/eq r3,r0
 bt/s E
K: mov.b r2,@r6
 dt r7
 bf L
E: rts
 nop
N: bra K
 nop
