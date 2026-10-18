import asm
def W(src): return asm.assemble(src.strip().split('\n'))
def fmt(w): return '{ '+', '.join('0x%04X'%x for x in w)+' }'
CALLER=open('mkwords.py').read().split('CALLER="""')[1].split('"""')[0]
out=[]
def callee_pin(name, body):
    out.append('        withCallee("%s", %s,\n                   %s, true, &known, &text, &agrees);\n        CHECK(known && agrees && !Has(text, "return 5;"));'%(name, fmt(W(CALLER)), fmt(W(body))))
G1="""add #-4,r15
mov.l r4,@r15
mov.l @r15,r5
mov #4,r1
L:
mov.l r1,@-r5
dt r1
bf L
add #4,r15
rts
nop"""
PP="""mov.l r4,@-r15
mov.l @r15+,r4
mov #4,r1
L:
mov.l r1,@-r4
dt r1
bf L
rts
nop"""
M14="""mov.l r14,@-r15
mov r4,r14
mov r14,r5
mov.l @r15+,r14
mov #4,r1
L:
mov.l r1,@-r5
dt r1
bf L
rts
nop"""
AS="""mov #8,r3
neg r3,r3
add r3,r4
sub r3,r4
mov #4,r1
L:
mov.l r1,@-r4
dt r1
bf L
rts
nop"""
callee_pin("a3_82 control: the callee spills the pointer whole to its frame and walks the reload", G1)
callee_pin("a3_82 control: the callee pushes and pops the pointer", PP)
callee_pin("a3_82 control: the callee moves the pointer through r14", M14)
callee_pin("a3_82 control: the callee adds and subtracts a negated constant", AS)
open('pins_callee.txt','w').write('\n'.join(out)+'\n')
walks=[]
def walk(name, src, want):
    walks.append('    walkLent("%s",\n             %s, "%s");'%(name, fmt(W(src)), want))
def wl(pre, frame=24, ptr=16, after=''):
    return f"""add #-{frame},r15
mov r15,r2
add #{ptr},r2
mov #5,r0
mov.l r0,@r15
{pre}
mov #4,r1
L:
mov.l r1,@-r2
dt r1
bf L
mov.l @r15,r0
add #{frame},r15
rts
nop"""
walk("a3_85 control: the pointer through two locals in a chain", """add #-28,r15
mov r15,r2
add #20,r2
mov #5,r0
mov.l r0,@r15
mov.l r2,@(20,r15)
mov.l @(20,r15),r3
mov.l r3,@(24,r15)
mov.l @(24,r15),r2
mov #5,r1
L:
mov.l r1,@-r2
dt r1
bf L
mov.l @r15,r0
add #28,r15
rts
nop""", "return xStack_1c;")
walk("a3_85 control: the pointer in a slot indexed by r0", wl("mov #20,r0\nmov.l r2,@(r0,r15)\nmov.l @(r0,r15),r2",frame=32), "return xStack_20;")
walk("a3_85 control: the pointer pushed and popped", wl("mov.l r2,@-r15\nmov.l @r15+,r2"), "return xStack_18;")
walks_txt='\n'.join(walks)
open('pins_walk.txt','w').write(walks_txt+'\n')
print(open('pins_callee.txt').read()[:600])
