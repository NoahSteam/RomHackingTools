two_call_vals|06070000|
sts.l pr,@-r15
mov.l r10,@-r15
mov.l =0x0603AC00,r10
mov.l =0x06071000,r3
jsr @r3
nop
mov.l r0,@r10
tst r4,r4
bt Lj
mov.l =0x06071010,r3
jsr @r3
nop
mov.l r0,@r10
Lj: mov.l =0x06071020,r3
jsr @r3
nop
mov.l @r10,r0
mov.l @r15+,r10
lds.l @r15+,pr
rts
nop
.org 0x06071000
rts
mov #1,r0
.org 0x06071010
rts
mov #2,r0
.org 0x06071020
rts
nop
==
two_call_vals_kept|06070000|
sts.l pr,@-r15
mov.l r10,@-r15
mov.l r9,@-r15
mov.l =0x0603AC00,r10
mov.l =0x06071000,r3
jsr @r3
nop
mov r0,r9
mov.l r9,@r10
tst r4,r4
bt Lj
mov.l =0x06071010,r3
jsr @r3
nop
mov r0,r9
mov.l r9,@r10
Lj: mov.l =0x06071020,r3
jsr @r3
nop
mov.l @r10,r0
add r9,r0
mov.l @r15+,r9
mov.l @r15+,r10
lds.l @r15+,pr
rts
nop
.org 0x06071000
rts
mov #1,r0
.org 0x06071010
rts
mov #2,r0
.org 0x06071020
rts
nop
