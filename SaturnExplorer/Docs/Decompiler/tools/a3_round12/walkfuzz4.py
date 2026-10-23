#!/usr/bin/env python3
"""walkfuzz4.py seed count : the probe's nested walk. P hands a frame address (r15 + 16) and a hidden counter (r3) to U1, a chain of 1 to 5 unlent
captured functions each passing r4 on (a call, a tail jump, a call through a callback in r5, or LENT for the middle ones), the last filling backwards
from r4. The sentinel at r15 + 0 is read back; a text that returns the 5 while the run returns another value is stale. Chains of length 1 are the control
(A3-86a holds them).  usage: OUT=<dir with drv, asm.py, mk.py> python3 walkfuzz4.py seed count"""
import random, sys, os, re, subprocess, resource
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
seed=int(sys.argv[1]); n=int(sys.argv[2])
rnd=random.Random(seed)
D=os.environ.get('OUT') or os.path.dirname(os.path.abspath(__file__))
out=[]
for i in range(n):
    L=rnd.choice([1,2,2,3,3,4,5])
    kinds=[rnd.choice(['jsr','jmp','callback','lent_jsr']) for _ in range(L-1)]
    base=0x06009040
    addr=[base+0x40*k for k in range(L)]
    P=['.word 0x082A','add #-24,r15','mov #5,r0','mov.l r0,@r15','mov r15,r4','add #16,r4','mov #4,r3']
    # callbacks: r5 carries the next function's address into U_k
    P+=['mov.l =%d,r1'%addr[0],'jsr @r1','nop','mov.l @r15,r0','add #24,r15','.word 0x482A','rts','nop']
    blocks=[('06009000',P)]
    lend=[]
    for k in range(L):
        a='%08X'%addr[k]
        if k==L-1:
            body=['mov r3,r1','L:','mov.l r1,@-r4','dt r1','bf L','rts','nop']
        else:
            kind=kinds[k]; nxt=addr[k+1]
            if kind=='jsr' or kind=='lent_jsr': body=['.word 0x4F22','mov.l =%d,r1'%nxt,'jsr @r1','nop','.word 0x4F26','rts','nop']
            elif kind=='jmp': body=['mov.l =%d,r1'%nxt,'jmp @r1','nop']
            else: body=['.word 0x4F22','mov.l =%d,r5'%nxt,'jsr @r5','nop','.word 0x4F26','rts','nop']
        blocks.append((a,body))
    # a hidden-input read in the last function keeps it unlent; 'lent_jsr' middles are lent only if they can be (their callee is unlent, so they are not)
    out.append('name WF4 seed=%d i=%d len=%d kinds=%s'%(seed,i,L,'+'.join(kinds) or 'none'))
    for a,b in blocks: out.append('asm '+a); out+=b; out.append('endasm')
    out+=['quiet','args 0 0 0 0','end']
open(D+'/wf4_%d.src'%seed,'w').write('\n'.join(out)+'\n')
sp=subprocess.run(['python3',D+'/mk.py',D+'/wf4_%d.src'%seed],capture_output=True,text=True,cwd=D)
if sp.returncode: print(sp.stderr[-300:]); sys.exit(1)
open(D+'/wf4_%d.spec'%seed,'w').write(sp.stdout)
def lim(): resource.setrlimit(resource.RLIMIT_AS,(4_000_000_000,4_000_000_000))
pr=subprocess.run([D+'/drv',D+'/scr_wf4',D+'/wf4_%d.spec'%seed],capture_output=True,text=True,preexec_fn=lim,timeout=1500)
o=pr.stdout; open(D+'/wf4_%d.out'%seed,'w').write(o)
from collections import Counter
tot=Counter(); stale=Counter()
for b in o.split('\n====  ')[1:]:
    name=b.split('\n')[0]
    runs=re.findall(r'RUN .*?-> (?:ret|NORET) r0=([0-9A-F]+)',b)
    if not runs: continue
    r0=int(runs[0],16)
    L=int(re.search(r'len=(\d)',name).group(1))
    tot[L]+=1
    if r0==5: continue
    rets=[l.strip().split(';')[0].strip() for l in b.split('\n') if re.match(r'\s+return\b',l)]
    ent=[r for r in rets[-1:] if re.fullmatch(r'return (0x)?[0-9a-f]+',r)]
    if ent and int(ent[0].split()[1],0)!=r0:
        stale[L]+=1
        if os.environ.get('SHOW'): print('STALE',name,ent[0],hex(r0))
print('chains by length',dict(sorted(tot.items())),'stale',dict(sorted(stale.items())))
