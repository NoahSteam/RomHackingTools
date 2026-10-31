import sys
L=int(sys.argv[1]); n=int(sys.argv[2]); tail=sys.argv[3] if len(sys.argv)>3 else 'direct'
out=[]
for L_ in ([L] if L>0 else [1,2,3,4]):
    a=['.word 0x4F22','add #-64,r15','mov r15,r1','add #4,r1','mov.l r1,@(32,r15)']
    for i in range(1,L_):
        a+=['mov r15,r1','add #%d,r1'%(32+4*(i-1)),'mov.l r1,@(%d,r15)'%(32+4*i)]
    a+=['mov #%d,r3'%n,'LP:','mov.l @(%d,r15),r1'%(32+4*(L_-1))]+['mov.l @r1,r1']*(L_-1)
    a+=['mov.l r5,@r1','add #4,r1','mov.l r1,@(32,r15)','add r6,r5','dt r3','bf LP']
    if tail=='direct': a+=['mov.l @(4,r15),r0']   # S+0 = frame 4? S at 4
    elif tail=='sum': a+=['mov.l @(4,r15),r0','mov.l @(8,r15),r3','add r3,r0']
    a+=['add #64,r15','.word 0x4F26','rts','nop']
    out+=['name LOOP L%d n%d %s'%(L_,n,tail),'asm 06009000']+a+['endasm','args 11223344 55 6677 80000001','end','']
print('\n'.join(out))
