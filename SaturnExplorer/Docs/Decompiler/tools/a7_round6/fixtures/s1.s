slot_other_reg|06070000|
mov.l =0x0603AC00,r2
mov r4,r0
cmp/eq #1,r0
bf B
mov #4,r1
bra L
mov #4,r3
B: mov #5,r1
L: mov.w r1,@r2
mov.l r3,@(4,r2)
rts
nop
==
slot_same_reg|06070000|
mov.l =0x0603AC00,r2
mov r4,r0
cmp/eq #1,r0
bf B
mov #7,r3
bra L
mov #4,r1
B: mov #5,r1
L: mov.w r1,@r2
mov.l r3,@(4,r2)
rts
nop
==
slot_other_reg_nouse|06070000|
mov.l =0x0603AC00,r2
mov r4,r0
cmp/eq #1,r0
bf B
mov #4,r1
bra L
mov #4,r3
B: mov #5,r1
L: mov.w r1,@r2
rts
nop
