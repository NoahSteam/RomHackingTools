import sys
L=2; var=sys.argv[1]
a=['.word 0x4F22','add #-64,r15','mov r15,r1','add #4,r1','mov.l r1,@(32,r15)','mov r15,r1','add #32,r1','mov.l r1,@(36,r15)']
a+=['mov #3,r3','LP:','mov.l @(36,r15),r1','mov.l @r1,r1']
if var=='nowb':   a+=['mov.l r5,@r1','add r6,r5']
if var=='wb':     a+=['mov.l r5,@r1','add #4,r1','mov.l r1,@(32,r15)','add r6,r5']
if var=='wbdirect': a+=['mov.l r5,@r1','add #4,r1','mov r15,r2','add #32,r2','mov.l r1,@r2','add r6,r5']
if var=='wb1':    a+=['mov.l r5,@r1','add r6,r5','mov r15,r2','add #4,r2','mov.l r2,@(32,r15)']
a+=['dt r3','bf LP']
a+=['mov.l @(4,r15),r0','mov.l @(8,r15),r2','add r2,r0','add #64,r15','.word 0x4F26','rts','nop']
print('name LOOP2 %s\nasm 06009000'%var); print('\n'.join(a)); print('endasm\nargs 11223344 55 6677 80000001\nend\n')
