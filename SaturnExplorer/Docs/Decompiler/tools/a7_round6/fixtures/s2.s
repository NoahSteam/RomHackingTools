eq_known_slot|06070000|
mov.l =0x0603AC00,r2
mov r4,r0
cmp/eq #4,r0
bf B
bra L
mov #4,r3
B: mov #5,r0
mov #9,r3
L: mov.w r0,@r2
mov.l r3,@(4,r2)
rts
nop
==
eq_known_loop|06070000|06071000
sts.l pr,@-r15
mov.l =0x0603AC00,r10
mov.l =0x06071000,r11
mov r4,r0
cmp/eq #4,r0
bf B
bra L
mov #4,r3
B: mov #5,r0
mov #9,r3
L: mov.l r0,@r10
jsr @r11
mov r3,r4
lds.l @r15+,pr
rts
nop
.org 0x06071000
rts
nop
