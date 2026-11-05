preserved_call|06070000|06071000
sts.l pr,@-r15
mov.l r8,@-r15
mov.l r10,@-r15
mov.l =0x0603AC00,r10
mov.l =0x06071000,r3
mov #5,r8
mov.l r8,@r10
jsr @r3
nop
mov r8,r2
mov.l r2,@(4,r10)
mov r0,r8
jsr @r3
nop
mov.l r8,@(8,r10)
mov.l @r15+,r10
mov.l @r15+,r8
lds.l @r15+,pr
rts
nop
.org 0x06071000
mov #9,r8
rts
mov r4,r0
==
preserved_call2|06070000|
sts.l pr,@-r15
mov.l r8,@-r15
mov.l r10,@-r15
mov.l =0x0603AC00,r10
mov.l =0x06071000,r3
mov #5,r8
mov.l r8,@r10
jsr @r3
nop
mov r8,r2
mov.l r2,@(4,r10)
mov r0,r8
jsr @r3
nop
mov.l r8,@(8,r10)
mov.l @r15+,r10
mov.l @r15+,r8
lds.l @r15+,pr
rts
nop
.org 0x06071000
rts
mov r4,r0
