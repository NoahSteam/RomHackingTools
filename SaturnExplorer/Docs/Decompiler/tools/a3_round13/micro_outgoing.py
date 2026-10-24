def fx(name, nargs, init0, nst=3, T=8, R=4, gdef=None, mid=None):
    L=['name '+name,'asm 06009000','.word 0x4F22','add #-16,r15']
    for off in range(0,16,4):
        if off==0 and not init0: continue
        L+=['mov #%d,r0'%(off//4+1),'mov.l r0,@(%d,r15)'%off]
    L+=['mov r15,r4','add #%d,r4'%T]
    for k in range(1,nargs): L+=['mov.l =0x0600F2%02X,r%d'%(k*16+0,4+k)] if k==1 else ['mov #%d,r%d'%(k,4+k)]
    L+=['mov.l =0x06009100,r1','jsr @r1','nop']
    L+=['mov.l =0x0600F200,r6','mov.l @(4,r6),r6','mov #0x55,r2']
    L+=['mov.l r2,@-r6']*nst
    L+=['mov.l @(%d,r15),r0'%R,'add #16,r15','.word 0x4F26','rts','nop','endasm']
    L+=['asm 06009100']+(gdef or ['mov.l =0x0600F200,r1','mov.l r4,@(4,r1)'])+['rts','nop','endasm']
    L+=['quiet','args 0 0 0 0','end']
    return L
out=[]
for nargs in (1,2,3,4):
    for init0 in (0,1):
        out+=fx('nargs=%d init0=%d'%(nargs,init0),nargs,init0)
# higher: T=12 (p above two locals) walking 2 down, R=4
for nargs in (2,):
    out+=fx('nargs=2 T=12 nst=2 R=4',nargs,1,2,12,4)
    out+=fx('nargs=2 T=16 nst=4 R=0',nargs,1,4,16,0)
print('\n'.join(out))
