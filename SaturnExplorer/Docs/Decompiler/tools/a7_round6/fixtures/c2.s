wliteral_paths|06070000|06071000
sts.l pr,@-r15
mov r4,r0
cmp/eq #1,r0
mov.w =0x1234,r8
bt L
cmp/eq #2,r0
mov.w =0x1234,r8
bf X
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
mem_join_low|06070000|06071000
mov.l r14,@-r15
sts.l pr,@-r15
mov r15,r14
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
mov r14,r15
lds.l @r15+,pr
rts
mov.l @r15+,r14
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
mov.l r14,@-r15
sts.l pr,@-r15
mov r15,r14
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
mov r14,r15
lds.l @r15+,pr
rts
mov.l @r15+,r14
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
