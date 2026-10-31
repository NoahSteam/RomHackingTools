import sys
var=sys.argv[1]; L=int(sys.argv[2])
a=['.word 0x4F22','add #-64,r15','mov r15,r1','add #4,r1','mov.l r1,@(32,r15)']
for i in range(1,L):
    a+=['mov r15,r1','add #%d,r1'%(32+4*(i-1)),'mov.l r1,@(%d,r15)'%(32+4*i)]
rl=['mov.l @(%d,r15),r1'%(32+4*(L-1))]+['mov.l @r1,r1']*(L-1)
if var=='if':
    a+=['tst r6,r6','bt SK']+rl+['mov.l r5,@r1','SK:']
if var=='ifelse':
    a+=['tst r6,r6','bt E1']+rl+['mov.l r5,@r1','bra J1','nop','E1:']+rl+['add #4,r1','mov.l r7,@r1','J1:']
if var=='two':
    a+=rl+['mov.l r5,@r1']+rl+['add #4,r1','mov.l r7,@r1']
a+=['mov.l @(4,r15),r0','mov.l @(8,r15),r2','add r2,r0','add #64,r15','.word 0x4F26','rts','nop']
print('name BR %s L%d\nasm 06009000'%(var,L)); print('\n'.join(a)); print('endasm\nargs 11223344 55 6677 80000001\nargs 11223344 55 0 80000001\nend\n')
