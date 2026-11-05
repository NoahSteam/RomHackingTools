three_paths_copies|06070000|06071000
sts.l pr,@-r15
mov r4,r0
cmp/eq #1,r0
bt/s L
mov #0,r8
cmp/eq #2,r0
bt P3
cmp/eq #3,r0
bf/s X
mov #0,r8
bra L
nop
P3: mov #0,r1
mov r1,r2
mov r2,r8
L: mov #0,r12
mov.l =0x06071000,r11
mov.l =0x0603AC00,r10
mov.l r12,@r10
mov #7,r9
M: jsr @r11
nop
add #1,r8
cmp/gt r9,r8
bf M
X: lds.l @r15+,pr
rts
mov r8,r0
.org 0x06071000
rts
nop
==
wliteral_paths|06070000|06071000
sts.l pr,@-r15
mov r4,r0
cmp/eq #1,r0
bt/s L
mov.w =0x1234,r8
cmp/eq #2,r0
bf X
mov.w =0x1234,r8
L: mov.w =0x1234,r12
mov.l =0x06071000,r11
mov.l =0x0603AC00,r10
mov.l r12,@r10
mov.w =0x2000,r9
M: jsr @r11
nop
add #1,r8
cmp/gt r9,r8
bf M
X: lds.l @r15+,pr
rts
mov r8,r0
.org 0x06071000
rts
nop
==
lliteral_paths|06070000|06071000
sts.l pr,@-r15
mov r4,r0
cmp/eq #1,r0
bt/s L
mov.l =0x12345678,r8
cmp/eq #2,r0
bf X
mov.l =0x12345678,r8
L: mov.l =0x12345678,r12
mov.l =0x06071000,r11
mov.l =0x0603AC00,r10
mov.l r12,@r10
mov.l =0x20000000,r9
M: jsr @r11
nop
add #1,r8
cmp/gt r9,r8
bf M
X: lds.l @r15+,pr
rts
mov r8,r0
.org 0x06071000
rts
nop
==
moved_then_remade|06070000|06071000
sts.l pr,@-r15
mov r4,r0
cmp/eq #1,r0
bt/s L
mov #0,r1
cmp/eq #2,r0
bf X
mov #0,r1
L: mov r1,r8
mov #0,r1
mov.l =0x06071000,r11
mov.l =0x0603AC00,r10
mov.l r1,@r10
mov #7,r9
M: jsr @r11
nop
add #1,r8
cmp/gt r9,r8
bf M
X: lds.l @r15+,pr
rts
mov r8,r0
.org 0x06071000
rts
nop
==
loop_head_zero|06070000|06071000
sts.l pr,@-r15
mov.l =0x06071000,r11
mov.l =0x0603AC00,r10
mov #0,r8
H: mov #0,r12
mov.l r12,@r10
jsr @r11
nop
add #1,r8
mov #5,r1
cmp/gt r1,r8
bf H
mov.l @r10,r0
cmp/eq #3,r0
bf/s H
mov #0,r8
lds.l @r15+,pr
rts
mov r8,r0
.org 0x06071000
rts
nop
==
mem_join_low|06070000|06071000
sts.l pr,@-r15
mov r4,r0
cmp/eq #1,r0
bt P1
bra P2
nop
J: mov #0,r1
mov #3,r4
mov #4,r5
mov #5,r6
mov.l =0x06071000,r0
jsr @r0
mov #6,r7
add #8,r15
lds.l @r15+,pr
rts
nop
P1: mov #0,r1
mov.l r1,@-r15
mov.l r1,@-r15
bra J
nop
P2: mov #0,r1
mov.l r1,@-r15
mov.l r1,@-r15
bra J
nop
.org 0x06071000
mov.l @(0,r15),r0
mov.l @(4,r15),r1
add r1,r0
add r4,r0
rts
nop
==
mem_join_low_r5|06070000|06071000
sts.l pr,@-r15
mov r4,r0
cmp/eq #1,r0
bt P1
bra P2
nop
J: mov #0,r5
mov #3,r4
mov #5,r6
mov.l =0x06071000,r0
jsr @r0
mov #6,r7
add #8,r15
lds.l @r15+,pr
rts
nop
P1: mov #0,r1
mov.l r1,@-r15
mov.l r1,@-r15
bra J
nop
P2: mov #0,r1
mov.l r1,@-r15
mov.l r1,@-r15
bra J
nop
.org 0x06071000
mov.l @(0,r15),r0
mov.l @(4,r15),r1
add r1,r0
add r4,r0
rts
nop
