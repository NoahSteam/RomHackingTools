out=[]
for L in (1,2,3,5):
  for kind in ('lent','unlent','nocall'):
    a=['.word 0x4F22','mov.l r8,@-r15','add #-48,r15']
    # slot0 at sp+0 holds &save(r8) = sp+48; slot i holds &slot(i-1)
    a+=['mov r15,r1','add #48,r1','mov.l r1,@(0,r15)']
    for i in range(1,L): a+=['mov r15,r1','add #%d,r1'%(4*(i-1)),'mov.l r1,@(%d,r15)'%(4*i)]
    rl=['mov.l @(%d,r15),r1'%(4*(L-1))]+['mov.l @r1,r1']*(L-1)
    a+=rl+['mov.l r5,@r1']                   # the save slot overwritten with data
    if kind!='nocall':
        a+=rl+['mov r1,r4','mov.l =0x%08X,r2'%(0x0600A000 if kind=='lent' else 0x0600A080),'jsr @r2','nop']
    else: a+=['mov.l @(48,r15),r0']
    a+=['add #48,r15','mov.l @r15+,r8','add r8,r0','.word 0x4F26','rts','nop']
    out.append('name SV2 L%d %s\n%sasm 06009000\n%s\nendasm\nasm 0600A000\nmov.l @r4,r0\nrts\nnop\nendasm\nasm 0600A080\nmov.l @r4,r0\nrts\nnop\nendasm\nargs 0 1234 0 0 r8=5a5a5a5a\nargs 7 5678 0 0 r8=77777777\nend\n'%(L,kind,'lend 0600A000\n' if kind=='lent' else '','\n'.join(a)))
print('\n'.join(out))
