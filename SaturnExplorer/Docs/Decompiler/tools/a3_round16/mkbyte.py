out=[]
for var in ('byte','half','tbit'):
    a=['.word 0x4F22','add #-64,r15','mov r15,r1','add #8,r1','mov.l r1,@(32,r15)']
    for o,r in ((0,'r5'),(4,'r6'),(8,'r7'),(12,'r5')): a+=['mov r15,r1','add #%d,r1'%(8+o),'mov.l %s,@r1'%r]
    if var=='byte': a+=['mov r4,r0','mov.b r0,@(35,r15)' if False else 'mov r15,r2','add #35,r2','mov.b r0,@r2']      # low byte of the pointer slot = r4
    if var=='half': a+=['mov r4,r0','mov r15,r2','add #34,r2','mov.w r0,@r2']
    if var=='tbit': a+=['mov r4,r0','and #3,r0','mov r15,r2','add #35,r2','mov.l @(32,r15),r1','add r0,r1','mov.l r1,@(32,r15)']  # pointer += r4&3 : unaligned? use word steps
    a+=['mov.l @(32,r15),r1','mov.l r7,@r1']       # store through the reloaded pointer
    a+=['mov.l @(8,r15),r0','mov.l @(12,r15),r3','add r3,r0','mov.l @(16,r15),r3','xor r3,r0','mov.l @(20,r15),r3','add r3,r0']
    a+=['add #64,r15','.word 0x4F26','rts','nop']
    out.append('name BYTE %s\nasm 06009000\n%s\nendasm\nargs 000000c4 10 20 30\nargs 000000c8 10 20 30\nargs 0000efc4 10 20 30\nargs 0000efc8 10 20 30\nargs 00000000 10 20 30\nend\n'%(var,'\n'.join(a)))
print('\n'.join(out))
