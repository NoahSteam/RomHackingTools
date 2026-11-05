two_tables|06070000|
sts.l pr,@-r15
mov.l =0x0603AC00,r5
mov #3,r1
cmp/hi r1,r4
bt/s Lend
mov r4,r1
add r1,r1
mova T1,r0
mov.w @(r0,r1),r1
add r1,r0
jmp @r0
nop
.align4
T1: .word A0-T1
.word A1-T1
.word A2-T1
.word A0-T1
A0: mov #1,r2
mov.l r2,@r5
bra Lmid
nop
A1: mov #2,r2
mov.l r2,@r5
bra Lmid
nop
A2: mov.l =0x06071000,r3
jsr @r3
nop
Lmid: mov #2,r1
cmp/hi r1,r6
bt/s Lend
mov r6,r1
add r1,r1
mova T2,r0
mov.w @(r0,r1),r1
add r1,r0
jmp @r0
nop
.align4
T2: .word B0-T2
.word B1-T2
.word B0-T2
B0: mov #7,r2
mov.l r2,@(4,r5)
bra Lend
nop
B1: mov #9,r2
mov.l r2,@(8,r5)
Lend: lds.l @r15+,pr
rts
nop
