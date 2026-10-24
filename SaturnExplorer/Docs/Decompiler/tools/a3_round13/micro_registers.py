def fx(name, reg, indirect):
    P=['.word 0x4F22','add #-16,r15','mov #5,r0','mov.l r0,@(0,r15)','mov #6,r0','mov.l r0,@(4,r15)','mov r15,r4','add #8,r4']
    if reg=='gbr': P+=['ldc r4,gbr']
    elif reg=='mach': P+=['lds r4,mach']
    elif reg=='r9': P+=['mov r4,r9']
    elif reg=='r1': P+=['mov r4,r1']
    elif reg=='r3': P+=['mov r4,r3']
    P+=['mov #0,r4']
    if indirect: P+=['mov.l =0x0600F400,r2','mov.l @r2,r2','jsr @r2','nop']
    else: P+=['mov.l =0x06009100,r2','jsr @r2','nop']
    P+=['mov.l @(4,r15),r0','add #16,r15','.word 0x4F26','rts','nop']
    if reg=='gbr': g=['stc gbr,r1']
    elif reg=='mach': g=['sts mach,r1']
    elif reg=='r9': g=['mov r9,r1']
    elif reg=='r1': g=[]
    elif reg=='r3': g=['mov r3,r1']
    g+=['mov #0x55,r2','mov.l r2,@-r1','mov.l r2,@-r1','rts','nop']
    L=['name '+name,'asm 06009000']+P+['endasm','asm 06009100']+g+['endasm']
    if indirect: L+=['long 0600F400 06009100']
    return L+['args 0 0 0 0','end']
out=[]
for reg in ('gbr','mach','r9','r1','r3'):
    for ind in (0,1):
        out+=fx('REG %s indirect=%d'%(reg,ind),reg,ind)
print('\n'.join(out))
