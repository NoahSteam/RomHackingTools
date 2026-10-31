out=[]
for tgt in ('same','other'):
  for L in (1,2):
    a=['.word 0x4F22','add #-64,r15','mov r15,r1','add #8,r1','mov.l r1,@(32,r15)']
    if L==2: a+=['mov r15,r1','add #32,r1','mov.l r1,@(36,r15)']
    for o,r in ((0,'r4'),(4,'r5'),(8,'r6'),(12,'r7')): a+=['mov r15,r1','add #%d,r1'%(8+o),'mov.l %s,@r1'%r]
    base = 8 if tgt=='same' else 44
    a+=['mov r15,r1','add #%d,r1'%base,'mov r6,r0','and #12,r0','mov.l r5,@(r0,r1)']
    rl=['mov.l @(%d,r15),r1'%(32+4*(L-1))]+['mov.l @r1,r1']*(L-1)
    a+=rl+['mov.l @r1,r0','add #4,r1','mov.l @r1,r3','xor r3,r0']
    a+=['add #64,r15','.word 0x4F26','rts','nop']
    out.append('name IDX3 %s L%d\nasm 06009000\n%s\nendasm\nend\n'%(tgt,L,'\n'.join(a)))
print('\n'.join(out))
