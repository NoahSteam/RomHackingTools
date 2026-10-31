import sys
# pushes r8,r9 above a 16-byte S; an indexed store through a pointer (direct or reloaded, L slots) can reach the save slots; the epilogue pops them
out=[]
for L in (0,1,2,3):
  for idxm in ('and #20,r0',):
    a=['.word 0x4F22','mov.l r8,@-r15','mov.l r9,@-r15','add #-16,r15']
    # slots below S? put slots in a second allocation below S so the index (upward) reaches saves, not slots
    a+=['add #-16,r15']      # slots at sp+0.., S at sp+16.., saves at sp+32.., PR at sp+40
    for i in range(L):
        a+=['mov r15,r1','add #%d,r1'%(16 if i==0 else 4*(i-1)),'mov.l r1,@(%d,r15)'%(4*i)]
    if L: rl=['mov.l @(%d,r15),r1'%(4*(L-1))]+['mov.l @r1,r1']*(L-1)
    else: rl=['mov r15,r1','add #16,r1']
    a+=rl+['mov r4,r0',idxm,'mov.l r5,@(r0,r1)']
    a+=['mov.l @(16,r15),r0','add #32,r15','mov.l @r15+,r9','mov.l @r15+,r8','add r8,r0','add r9,r0','.word 0x4F26','rts','nop']
    out.append('name SV L%d\nasm 06009000\n%s\nendasm\nargs 0 1234 0 0\nargs 4 1234 0 0\nargs 10 1234 0 0\nargs 14 1234 0 0\nargs 8 1234 0 0\nend\n'%(L,'\n'.join(a)))
print('\n'.join(out))
