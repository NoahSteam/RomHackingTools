a7_178_slot|06009000|VOLATILE=26001008-2600100f
 mov.l =0x26001008,r6
 tst r0,r5
 bt L
 mov r4,r2
 bra J
 mov #5,r1
L: mov #2,r1
J: mov.b r1,@r6
 mov.l @r4,r3
 cmp/eq r3,r0
 bf K
 mov.b r1,@r6
K: rts
 nop
==
a7_178_slot_call|06009000|
 tst r0,r5
 bt L
 nop
 bra J
 mov #5,r1
L: mov #2,r1
J: mov.l =0x06003000,r6
 mov.l r1,@r6
 mov.l r1,@r4
 rts
 nop
==
a7_179_read|06009000|
 mov.l =0x06003C10,r1
 mov.l =0x06003C14,r2
 mov.w @r1,r3
 cmp/eq r0,r5
 bf/s X
 mov.w r4,@r2
 mov.w r3,@r2
 rts
 nop
X: mov.w r3,@r2
 mov.w r4,@r2
 rts
 nop
==
a7_179_read2|06009000|VOLATILE=26001008-2600100f
 mov.l =0x06003C10,r1
 mov.l =0x26001008,r2
 mov.b @r1,r3
 tst r0,r5
 bt L
 cmp/eq r0,r4
 bf/s M
 mov.b r3,@r2
 mov.b r4,@r2
M: tst r1,r5
 bf N
L: mov.b r3,@r2
N: rts
 nop
