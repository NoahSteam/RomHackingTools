#!/usr/bin/env python3
"""walkfuzz3.py seed count : area 3 round 12. A frame address handed to a call is PERSISTED by a callee (a global, an out-parameter slot of the
caller's frame) and used by a LATER step to store below the address it was handed: in the caller (reload and walk down), or in a second callee
(reads the global and walks down). The sentinel 5 at r15+0 is read back; a text that returns the 5 while the run returns another value is stale.
usage: OUT=<dir with drv, asm.py, mk.py> python3 walkfuzz3.py seed count"""
import random, sys, os, re, subprocess, resource
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import walkfuzz_lib as wl
seed=int(sys.argv[1]); n=int(sys.argv[2])
rnd=random.Random(seed); wl.rnd=rnd
D=os.environ.get('OUT') or os.path.dirname(os.path.abspath(__file__))
GLOB='0x0600F100'
def walk(reg):
    wk=rnd.choice(['loop_l','loop_b','single_neg','nested'])
    if wk=='loop_l': return wk,['mov #4,r1','L:','mov.l r1,@-%s'%reg,'dt r1','bf L']
    if wk=='loop_b': return wk,['mov #16,r1','L:','mov.b r1,@-%s'%reg,'dt r1','bf L']
    if wk=='single_neg': return wk,['mov #1,r1','add #-16,%s'%reg,'mov.l r1,@%s'%reg]
    return wk,['mov #2,r6','O:','mov #2,r1','L:','mov.l r1,@-%s'%reg,'dt r1','bf L','dt r6','bf O']
out=[]
for i in range(n):
    chan=rnd.choice(['global_callee','global_caller','out_caller','out_caller_unlent','global_callee_unlent']+(['control'] if os.environ.get('CONTROL') else []))
    tk,tl=wl.callee_transform('r2','r3') if rnd.random()<0.5 else ('none',[])
    wk,wl_=walk('r2')
    pre=['.word 0x082A','add #-32,r15','mov #5,r0','mov.l r0,@r15']      # sts pr,r8 : PR kept in r8, so the walk's stores cannot hit a saved PR
    post=['mov.l @r15,r0','add #32,r15','.word 0x482A','rts','nop']
    src=[]; lend=[]
    if chan.startswith('global_callee'):
        cal=pre+['mov r15,r4','add #16,r4','mov.l =0x06009100,r1','jsr @r1','nop','mov.l =0x06009200,r1','jsr @r1','nop']+post
        g1=['mov.l =%s,r1'%GLOB,'rts','mov.l r4,@r1']
        g2=['mov.l =%s,r2'%GLOB,'mov.l @r2,r2']+tl+wl_+['rts','nop']
        src+=[('06009000',cal),('06009100',g1),('06009200',g2)]
        lend=['06009100','06009200'] if chan=='global_callee' else ['06009200']
    elif chan=='control':   # the frame address handed straight to a lent callee that walks down from it (rounds 10 and 11 fixed this: right)
        cal=pre+['mov r15,r4','add #16,r4','mov.l =0x06009100,r1','jsr @r1','nop']+post
        g1=['mov r4,r2']+tl+wl_+['rts','nop']
        src+=[('06009000',cal),('06009100',g1)]; lend=['06009100']
    elif chan=='global_caller':
        cal=pre+['mov r15,r4','add #16,r4','mov.l =0x06009100,r1','jsr @r1','nop','mov.l =%s,r2'%GLOB,'mov.l @r2,r2']+tl+wl_+post
        g1=['mov.l =%s,r1'%GLOB,'rts','mov.l r4,@r1']
        src+=[('06009000',cal),('06009100',g1)]; lend=['06009100']
    else:
        # out-parameter: g stores its second argument through its first (a slot at r15+24 of the caller)
        cal=pre+['mov r15,r4','add #24,r4','mov r15,r5','add #16,r5','mov.l =0x06009100,r1','jsr @r1','nop','mov.l @(24,r15),r2']+tl+wl_+post
        g1=['rts','mov.l r5,@r4']
        src+=[('06009000',cal),('06009100',g1)]; lend=['06009100'] if chan=='out_caller' else []
    out.append('name WF3 seed=%d i=%d chan=%s transform=%s walk=%s'%(seed,i,chan,tk,wk))
    for a,b in src: out.append('asm '+a); out+=b; out.append('endasm')
    if lend: out.append('lend '+' '.join(lend))
    out+=['quiet','args 0 0 0 0','end']
open(D+'/wf3_%d.src'%seed,'w').write('\n'.join(out)+'\n')
sp=subprocess.run(['python3',D+'/mk.py',D+'/wf3_%d.src'%seed],capture_output=True,text=True,cwd=D)
if sp.returncode: print(sp.stderr[-300:]); sys.exit(1)
open(D+'/wf3_%d.spec'%seed,'w').write(sp.stdout)
def lim(): resource.setrlimit(resource.RLIMIT_AS,(4_000_000_000,4_000_000_000))
pr=subprocess.run([D+'/drv',D+'/scr_wf3',D+'/wf3_%d.spec'%seed],capture_output=True,text=True,preexec_fn=lim,timeout=1500)
o=pr.stdout; open(D+'/wf3_%d.out'%seed,'w').write(o)
stale=0; tot=0; reach=0; lentc=0; staleLent=0
from collections import Counter
byc=Counter()
for b in o.split('\n====  ')[1:]:
    name=b.split('\n')[0]
    runs=re.findall(r'RUN .*?-> (?:ret|NORET) r0=([0-9A-F]+)',b)
    if not runs: continue
    r0=int(runs[0],16); tot+=1
    ps=[l for l in b.split('\n') if l.strip().startswith('PROTO')]
    lent='known=1' in ps[-1] if ps else False
    lentc+=lent
    if r0==5: continue
    reach+=1
    rets=[l.strip().split(';')[0].strip() for l in b.split('\n') if re.match(r'\s+return\b',l)]
    ent=[r for r in rets[-1:] if re.fullmatch(r'return (0x)?[0-9a-f]+',r)]
    if ent and int(ent[0].split()[1],0)!=r0:
        stale+=1; staleLent+=lent
        byc[(re.search(r'chan=(\w+)',name).group(1),'LENT' if lent else 'withheld')]+=1
        print('STALE','LENT' if lent else 'withheld',name,'text',ent[0],'run',hex(r0))
ctl=sum(1 for b in o.split('\n====  ')[1:] if 'chan=control' in b.split('\n')[0])
print('programs',tot,'lent',lentc,'walk reaches the sentinel',reach,'stale',stale,'stale lent',staleLent,'control programs',ctl)
print(dict(byc))
