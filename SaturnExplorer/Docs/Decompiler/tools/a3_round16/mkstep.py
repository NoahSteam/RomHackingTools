import sys
# straight-line: p stepped and written back to slot0, L levels, k repetitions
L=int(sys.argv[1]); k=int(sys.argv[2])
a=['.word 0x4F22','add #-64,r15','mov r15,r1','add #4,r1','mov.l r1,@(32,r15)']
for i in range(1,L):
    a+=['mov r15,r1','add #%d,r1'%(32+4*(i-1)),'mov.l r1,@(%d,r15)'%(32+4*i)]
for j in range(k):
    a+=['mov.l @(%d,r15),r1'%(32+4*(L-1))]+['mov.l @r1,r1']*(L-1)
    a+=['mov.l r5,@r1','add #4,r1','mov.l r1,@(32,r15)','add r6,r5']
a+=['mov.l @(4,r15),r0','mov.l @(8,r15),r3','add r3,r0','add #64,r15','.word 0x4F26','rts','nop']
print('name STEP L%d k%d\nasm 06009000'%(L,k)); print('\n'.join(a)); print('endasm\nargs 11223344 55 6677 80000001\nend\n')
