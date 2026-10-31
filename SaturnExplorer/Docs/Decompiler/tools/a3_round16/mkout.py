out=[]
for kind in ('lent','unlent'):
  for slotted in (0,1,2):
    a=['.word 0x4F22','add #-32,r15','mov r15,r1','add #8,r1']
    # S = sp+8..sp+24 ; fill
    for o,r in ((0,'r4'),(4,'r6'),(8,'r7'),(12,'r4')): a+=['mov r15,r2','add #%d,r2'%(8+o),'mov.l %s,@r2'%r]
    a+=['mov r15,r4','add #8,r4','mov.l =0x%08X,r2'%(0x0600A000 if kind=='lent' else 0x0600A080),'jsr @r2','nop']      # r0 = &S+4 from the callee
    if slotted==1: a+=['mov.l r0,@(0,r15)','mov.l @(0,r15),r1']
    elif slotted==2: a+=['mov.l r0,@(0,r15)','mov r15,r1','mov.l r1,@(4,r15)','mov.l @(4,r15),r1','mov.l @r1,r1']   # pointer through a slot holding the slot's address
    else: a+=['mov r0,r1']
    a+=['mov.l r5,@r1']                                  # S+4 = r5 through the callee's output
    a+=['mov.l @(12,r15),r0','mov.l @(16,r15),r3','add r3,r0','add #32,r15','.word 0x4F26','rts','nop']
    out.append('name OUT %s s%d\n%sasm 06009000\n%s\nendasm\nasm 0600A000\nmov r4,r0\nadd #4,r0\nrts\nnop\nendasm\nasm 0600A080\nmov r4,r0\nadd #4,r0\nrts\nnop\nendasm\nargs 1 22 3 4\nargs 7 99 5 6\nend\n'%(kind,slotted,'lend 0600A000\n' if kind=='lent' else '','\n'.join(a)))
print('\n'.join(out))
