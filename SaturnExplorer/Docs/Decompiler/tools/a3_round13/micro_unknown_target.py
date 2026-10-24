def fx(name, indirect, glent=False, tail=False):
    P=['.word 0x4F22','add #-16,r15','mov #5,r0','mov.l r0,@r15']
    if indirect: P+=['mov.l =0x0600F400,r1','mov.l @r1,r1','jsr @r1','nop']
    else: P+=['mov.l =0x06009100,r1','jsr @r1','nop']
    P+=['mov.l @r15,r0','add #16,r15','.word 0x4F26','rts','nop']
    g=['mov #7,r0','mov.l r0,@r15','rts','nop']
    L=['name '+name,'asm 06009000']+P+['endasm','asm 06009100']+g+['endasm']
    if indirect: L+=['long 0600F400 06009100']
    return L+['args 0 0 0 0','end']
out=fx('K1 direct call of an unlent captured g that writes its entry-r15 word',0)+fx('K2 indirect call through a pointer loaded from RAM (target unknown) of a g that writes the word',1)
print('\n'.join(out))
