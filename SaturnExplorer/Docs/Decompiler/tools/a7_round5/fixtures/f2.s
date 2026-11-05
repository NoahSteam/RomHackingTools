braf_table|06070000|
sts.l pr,@-r15
mov.l =0x0603AC00,r5
mov #3,r1
cmp/hi r1,r4
bt/s Lend
mov r4,r1
add r1,r1
mova T1,r0
mov.w @(r0,r1),r0
braf r0
nop
Bb: .align4
T1: .word A0-Bb
.word A1-Bb
.word A2-Bb
.word A0-Bb
A0: mov #1,r2
mov.l r2,@r5
bra Lend
nop
A1: mov #2,r2
mov.l r2,@(4,r5)
bra Lend
nop
A2: mov.l =0x06071000,r3
jsr @r3
nop
Lend: lds.l @r15+,pr
rts
nop
