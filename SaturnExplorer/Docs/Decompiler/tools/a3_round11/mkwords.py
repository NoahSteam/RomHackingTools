import sys, asm
def words(src):
    w=asm.assemble([l for l in src.strip().split('\n')])
    return '{ '+', '.join('0x%04X'%x for x in w)+' }'
CALLER="""
.word 0x4F22
add #-20,r15
mov #5,r0
mov.l r0,@r15
mov r15,r4
add #16,r4
mov.l =0x06009040,r1
jsr @r1
nop
mov.l @r15,r0
add #20,r15
.word 0x4F26
rts
nop"""
FILL4="""
mov #4,r1
L:
mov.l r1,@-r4
dt r1
bf L
rts
nop"""
if __name__=='__main__':
    print('caller', words(CALLER))
    print('fill', words(FILL4))
