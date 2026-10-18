import asm, sys
from mkwords import words, CALLER, FILL4
def W(src): return asm.assemble(src.strip().split('\n'))
CALLER2=CALLER  # callee at 06009040
fx=[]   # (id, name, [(addr, src)], lend list, entry)
def add(i,name,parts,lend,args="args 0 0 0 0"):
    fx.append((i,name,parts,lend,args))
def caller_with(pre, frame=20, ptroff=16, reg='r4', post=""):
    return f"""
.word 0x4F22
add #-{frame},r15
mov #5,r0
mov.l r0,@r15
mov r15,{reg}
add #{ptroff},{reg}
{pre}
mov.l =0x06009040,r1
jsr @r1
nop
mov.l @r15,r0
add #{frame},r15
.word 0x4F26
rts
nop"""
WRAP_JSR="""
.word 0x4F22
mov.l =0x06009080,r1
jsr @r1
nop
.word 0x4F26
rts
nop"""
WRAP_TAIL="""
mov.l =0x06009080,r1
jmp @r1
nop"""
FILL="""
mov #4,r1
L:
mov.l r1,@-r4
dt r1
bf L
rts
nop"""
# A3-82
add('82a','wrapper: g forwards r4 to a lent fill-backwards k (jsr)',[(0x06009000,CALLER),(0x06009040,WRAP_JSR),(0x06009080,FILL)],[0x06009080,0x06009040])
add('82b','thunk: g jumps to a lent fill-backwards k',[(0x06009000,CALLER),(0x06009040,WRAP_TAIL),(0x06009080,FILL)],[0x06009080,0x06009040])
add('82c','control: direct fill-backwards (A3-71a)',[(0x06009000,CALLER),(0x06009040,FILL)],[0x06009040])
# A3-83: unmodelled pointer ops in the callee
def callee(body): return body+"\nrts\nnop"
add('83a','callee masks the pointer with shll2/shlr2',[(0x06009000,CALLER),(0x06009040,"mov r4,r5\nshll2 r5\nshlr2 r5\nmov #4,r1\nL:\nmov.l r1,@-r5\ndt r1\nbf L\nrts\nnop")],[0x06009040])
add('83b','callee spills the pointer as two halves',[(0x06009000,CALLER),(0x06009040,"add #-4,r15\nmov r15,r3\nadd #2,r3\nmov r4,r6\nmov.w r6,@r3\nshlr16 r6\nadd #-2,r3\nmov.w r6,@r3\nmov.l @r15,r5\nmov #4,r1\nL:\nmov.l r1,@-r5\ndt r1\nbf L\nadd #4,r15\nrts\nnop")],[0x06009040])
add('83c','callee stores the pointer to a global and walks the reload',[(0x06009000,CALLER),(0x06009040,"mov.l =0x0600F100,r3\nmov.l r4,@r3\nmov.l @r3,r5\nmov #4,r1\nL:\nmov.l r1,@-r5\ndt r1\nbf L\nrts\nnop")],[0x06009040])
add('83d','callee adds a negated constant (mov #16,r3 ; neg r3,r3)',[(0x06009000,CALLER),(0x06009040,"mov #16,r3\nneg r3,r3\nadd r3,r4\nmov #1,r1\nmov.l r1,@r4\nrts\nnop")],[0x06009040])
add('83e','callee adds a shifted constant (mov #-4,r3 ; shll2 r3)',[(0x06009000,CALLER),(0x06009040,"mov #-4,r3\nshll2 r3\nadd r3,r4\nmov #1,r1\nmov.l r1,@r4\nrts\nnop")],[0x06009040])
add('83f','callee swaps the pointer twice (swap.w r4,r4 x2)',[(0x06009000,CALLER),(0x06009040,"swap.w r4,r4\nswap.w r4,r4\nmov #4,r1\nL:\nmov.l r1,@-r4\ndt r1\nbf L\nrts\nnop")],[0x06009040])
CALLER_SLOT="""
.word 0x4F22
add #-24,r15
mov #5,r0
mov.l r0,@r15
mov r15,r2
add #16,r2
mov.l r2,@(16,r15)
mov r15,r4
add #16,r4
mov.l =0x06009040,r1
jsr @r1
nop
mov.l @r15,r0
add #24,r15
.word 0x4F26
rts
nop"""
add('83g','callee loads the pointer from the word its argument points to',[(0x06009000,CALLER_SLOT),(0x06009040,"mov.l @r4,r5\nmov #4,r1\nL:\nmov.l r1,@-r5\ndt r1\nbf L\nrts\nnop")],[0x06009040])
# A3-84: caller launders the address
add('84a','caller passes the frame address masked (and #-4)',[(0x06009000,caller_with("mov #-4,r3\nand r3,r4")),(0x06009040,FILL)],[0x06009040])
add('84b','caller passes the address through a global',[(0x06009000,caller_with("mov.l =0x0600F100,r3\nmov.l r4,@r3\nmov #0,r4\nmov.l @r3,r4")),(0x06009040,FILL)],[0x06009040])
add('84c','caller passes the address as two halves',[(0x06009000,caller_with("mov r15,r3\nadd #18,r3\nmov.w r4,@r3\nshlr16 r4\nadd #-2,r3\nmov.w r4,@r3\nmov.l @(16,r15),r4",frame=24)),(0x06009040,FILL)],[0x06009040])
add('84d','caller passes the address swapped twice',[(0x06009000,caller_with("swap.w r4,r4\nswap.w r4,r4")),(0x06009040,FILL)],[0x06009040])
# A3-85: patch 0060's walk
def walk(pre, frame=24, ptr=16):
    return f"""
add #-{frame},r15
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
add('85a','pointer stored to a global and reloaded',[(0x06009000,walk("mov.l =0x0600F100,r3\nmov.l r2,@r3\nmov.l @r3,r2"))],[])
add('85b','pointer spilled as two halves, reloaded whole',[(0x06009000,walk("mov r15,r3\nadd #18,r3\nmov.w r2,@r3\nshlr16 r2\nadd #-2,r3\nmov.w r2,@r3\nmov.l @(16,r15),r2"))],[])
add('85c','pointer spilled whole, reloaded as two halves',[(0x06009000,walk("mov.l r2,@(16,r15)\nmov r15,r3\nadd #16,r3\nmov.w @r3,r4\nadd #2,r3\nmov.w @r3,r5\nshll16 r4\nextu.w r5,r5\nor r5,r4\nmov r4,r2"))],[])
add('85d','pointer spilled whole, reloaded by four bytes',[(0x06009000,walk("mov.l r2,@(16,r15)\nmov r15,r3\nadd #16,r3\nmov.b @r3+,r4\nmov.b @r3+,r5\nmov.b @r3+,r6\nmov.b @r3+,r7\nextu.b r4,r4\nextu.b r5,r5\nextu.b r6,r6\nextu.b r7,r7\nshll8 r4\nor r5,r4\nshll8 r4\nor r6,r4\nshll8 r4\nor r7,r4\nmov r4,r2"))],[])
add('85e','control: spilled whole to a local and reloaded (A3-80h)',[(0x06009000,walk("mov.l r2,@(16,r15)\nmov.l @(16,r15),r2"))],[])
# A3-86: unlent callee
CALLER3="""
.word 0x4F22
add #-20,r15
mov #5,r0
mov.l r0,@r15
mov #4,r3
mov r15,r4
add #16,r4
mov.l =0x06009040,r1
jsr @r1
nop
mov.l @r15,r0
add #20,r15
.word 0x4F26
rts
nop"""
add('86a','callee withheld (reads r3 on entry) fills backwards through r4',[(0x06009000,CALLER3),(0x06009040,"mov r3,r1\nL:\nmov.l r1,@-r4\ndt r1\nbf L\nrts\nnop")],[0x06009040])
CALLER4="""
.word 0x4F22
add #-24,r15
mov #5,r0
mov.l r0,@r15
mov r15,r4
add #16,r4
mov.l =0x06009040,r1
jsr @r1
nop
mov r0,r2
mov #4,r1
L:
mov.l r1,@-r2
dt r1
bf L
mov.l @r15,r0
add #24,r15
.word 0x4F26
rts
nop"""
add('86b','callee returns its pointer argument; the caller walks the result down',[(0x06009000,CALLER4),(0x06009040,"mov r4,r0\nrts\nnop")],[0x06009040])
# A3-87: lent A3-79
add('87a','A3-79: the walk from the frame pointer (lent, raw register)',[(0x06009000,"")],[])
out=[]
for i,name,parts,lend,args in fx:
    out.append('name %s %s'%(i,name))
    for a,s in parts:
        if i=='87a':
            out.append('code 06009000 2FE6 7FF8 6EF3 E005 2E02 62E3 7208 E102 2216 4110 8BFC 60E2 6FE3 7F08 000B 6EF6')
            break
        out.append('asm %08X'%a); out.append(s.strip()); out.append('endasm')
    if lend: out.append('lend '+' '.join('%08X'%x for x in lend))
    out.append(args); out.append('end')
open('rowsfix.src','w').write('\n'.join(out)+'\n')
import json
json.dump([(i,name,[(a,W(s) if s else []) for a,s in parts],lend) for i,name,parts,lend,args in fx],open('rowsfix.json','w'))
