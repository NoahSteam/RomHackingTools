out=[]
for st in ('idxrel','idxdir','constrel','none'):
  for rd in ('rel','dir'):
    for fill in ('dir','rel'):
      a=['.word 0x4F22','add #-64,r15','mov r15,r1','add #8,r1','mov.l r1,@(32,r15)']
      for o,r in ((0,'r4'),(4,'r5'),(8,'r6'),(12,'r7')):
          if fill=='dir': a+=['mov r15,r1','add #%d,r1'%(8+o),'mov.l %s,@r1'%r]
          else: a+=['mov.l @(32,r15),r1','add #%d,r1'%o,'mov.l %s,@r1'%r]
      if st=='idxrel': a+=['mov.l @(32,r15),r1','mov r6,r0','and #12,r0','mov.l r5,@(r0,r1)']
      if st=='idxdir': a+=['mov r15,r1','add #8,r1','mov r6,r0','and #12,r0','mov.l r5,@(r0,r1)']
      if st=='constrel': a+=['mov.l @(32,r15),r1','add #4,r1','mov.l r5,@r1']
      if rd=='rel': a+=['mov.l @(32,r15),r1','mov.l @r1,r0','add #4,r1','mov.l @r1,r3','xor r3,r0']
      else: a+=['mov.l @(8,r15),r0','mov.l @(12,r15),r3','xor r3,r0']
      a+=['add #64,r15','.word 0x4F26','rts','nop']
      out.append('name IDX2 st=%s rd=%s fill=%s\nasm 06009000\n%s\nendasm\nend\n'%(st,rd,fill,'\n'.join(a)))
print('\n'.join(out))
