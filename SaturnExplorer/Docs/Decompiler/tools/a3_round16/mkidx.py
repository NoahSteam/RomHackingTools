import sys
# L-level chain to S (frame+8), S filled by 4 words, one indexed store through the reloaded pointer, S read back through the chain
out=[]
for L in (1,2,3,4):
  for rd in ('chain','direct'):
    a=['.word 0x4F22','add #-64,r15']
    a+=['mov r15,r1','add #8,r1','mov.l r1,@(32,r15)']
    for i in range(1,L): a+=['mov r15,r1','add #%d,r1'%(32+4*(i-1)),'mov.l r1,@(%d,r15)'%(32+4*i)]
    rl=['mov.l @(%d,r15),r1'%(32+4*(L-1))]+['mov.l @r1,r1']*(L-1)
    for o,r in ((0,'r4'),(4,'r5'),(8,'r6'),(12,'r7')): a+=['mov r15,r1','add #%d,r1'%(8+o),'mov.l %s,@r1'%r]
    a+=rl+['mov r6,r0','and #12,r0','mov.l r5,@(r0,r1)']
    if rd=='chain': a+=rl+['mov.l @r1,r0','add #4,r1','mov.l @r1,r3','xor r3,r0']
    else: a+=['mov.l @(8,r15),r0','mov.l @(12,r15),r3','xor r3,r0','mov.l @(16,r15),r3','add r3,r0']
    a+=['add #64,r15','.word 0x4F26','rts','nop']
    out.append('name IDX L%d %s\nasm 06009000\n%s\nendasm\nend\n'%(L,rd,'\n'.join(a)))
print('\n'.join(out))
