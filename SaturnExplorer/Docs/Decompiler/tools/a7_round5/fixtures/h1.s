samenum_other_reg|06070000|06071000
sts.l pr,@-r15
mov r4,r0
cmp/eq #1,r0
bt/s L
mov #0,r8
cmp/eq #2,r0
bf/s X
mov #0,r8
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
