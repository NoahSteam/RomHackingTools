def P(name, g, call_extra=None):
    Pm=['.word 0x4F22','add #-16,r15','mov #5,r0','mov.l r0,@r15','mov.l =0x06009100,r1','jsr @r1','nop','mov.l @r15,r0','add #16,r15','.word 0x4F26','rts','nop']
    L=['name '+name,'asm 06009000']+Pm+['endasm','asm 06009100']+g+['endasm','lend 06009100','args 0 0 0 0','end']
    return L
base=['add #-4,r15','mov r15,r1','add #4,r1','mov.l r1,@r15']
tail=['mov #7,r0','mov.l r0,@r3','add #4,r15','rts','nop']
out=[]
out+=P('S0 control: g writes the caller word through an exact copy of r15',['add #-4,r15','mov r15,r3','add #4,r3','mov #7,r0','mov.l r0,@r3','add #4,r15','rts','nop'])
out+=P('S1 pointer spilled in a local, reloaded through an exact address of the local',base+['mov.l @r15,r3']+tail)
out+=P('S2 pointer spilled in a local, reloaded through a swap-pair laundered address of the local',base+['mov r15,r2','swap.w r2,r2','swap.w r2,r2','mov.l @r2,r3']+tail)
out+=P('S3 same, laundered by mask',base+['mov r15,r2','mov #-4,r4','and r4,r2','mov.l @r2,r3']+tail)
out+=P('S4 same, the local reloaded through r0 index',base+['mov #0,r0','mov.l @(r0,r15),r3']+tail)
out+=P('S5 same, through a pointer passed in a global (store then load)',base+['mov.l =0x0600F300,r2','mov.l r15,@r2','mov.l @r2,r2','mov.l @r2,r3']+tail)
print('\n'.join(out))
