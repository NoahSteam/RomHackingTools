out=[]
for var in ('chain','glob','globdirect'):
    a=['.word 0x4F22','add #-64,r15','mov r15,r1','add #8,r1','mov.l =0x0600F100,r2','mov.l r1,@r2']      # &S published in G
    if var=='chain': a+=['mov.l r1,@(32,r15)']
    if var=='chain': a+=['mov.l @(32,r15),r1']
    else: a+=['mov.l @r2,r1']                                                       # pointer read back from the global
    a+=['mov.l r5,@(4,r1)']                                                          # S[1] = r5 through the pointer
    a+=['mov.l r6,@r1' if var!='globdirect' else 'mov.l r6,@(8,r15)']
    a+=['mov.l =0x0600A000,r3','jsr @r3','nop']                                      # the callee reads *G, G[1] and G[1]+... 
    a+=['add #64,r15','.word 0x4F26','rts','nop']
    out.append('name GCALL %s\nlend 0600A000\nasm 06009000\n%s\nendasm\nasm 0600A000\nmov.l =0x0600F100,r1\nmov.l @r1,r1\nmov.l @(4,r1),r0\nmov.l @r1,r2\nadd r2,r0\nrts\nnop\nendasm\nargs 11223344 55 6677 80000001\nend\n'%(var,'\n'.join(a)))
print('\n'.join(out))
