slot_case|06070000|
sts.l pr,@-r15
mov.l =0x0603AC00,r5
mov #2,r1
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
A0: mov.l =0x06071000,r3
jsr @r3
mov #5,r4
bra Lend
mov #1,r6
A1: mov #2,r2
bra Lend
mov.l r2,@r5
A2: mov.l =0x06071000,r3
jsr @r3
mov r6,r4
mov.l r0,@(4,r5)
Lend: lds.l @r15+,pr
rts
mov r6,r0
