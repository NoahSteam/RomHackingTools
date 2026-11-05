empty_cases|06070000|
sts.l pr,@-r15
mov.l =0x0603AC00,r5
mov #4,r1
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
.word A3-T1
.word A4-T1
A0: bra Lend
nop
A1: bra Lmid
nop
A2: nop
A3: mov #3,r2
mov.l r2,@r5
A4: mov.l @r5,r3
add #1,r3
mov.l r3,@r5
Lmid: mov #5,r2
mov.l r2,@(4,r5)
Lend: lds.l @r15+,pr
rts
nop
==
rtscase|06070000|
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
A0: rts
mov #10,r0
A1: rts
mov #11,r0
A2: mov #12,r0
Lend: rts
nop
==
shared_dest_tables|06070000|
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
A0: mov #10,r2
mov.l r2,@r5
bra L2
nop
A1: mov #11,r2
mov.l r2,@r5
bra L2
nop
A2: mov #12,r2
mov.l r2,@r5
L2: mov r6,r1
mov #2,r3
cmp/hi r3,r6
bt/s Lend
add r1,r1
mova T2,r0
mov.w @(r0,r1),r1
add r1,r0
jmp @r0
nop
.align4
T2: .word A1-T2
.word A2-T2
.word A0-T2
Lend: rts
nop
