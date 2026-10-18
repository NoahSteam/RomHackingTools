#!/usr/bin/env python3
"""walkfuzz2.py seed count : A3-80 shapes -- one function keeps a frame address in r6 through 1-3 laundering steps (spills of every width, a global,
push/pop, masks...) and optionally a call, then walks it down; the sentinel at r15+0 is read back. Stale when the text returns the 5."""
import random, sys, os, re, subprocess, resource
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
seed=int(sys.argv[1]); n=int(sys.argv[2])
rnd=random.Random(seed)
import walkfuzz_lib as wl
out=[]
for i in range(n):
    reg='r9'; tmp=rnd.choice(['r2','r3'])
    steps=[]; names=[]
    for _ in range(1 if os.environ.get("ONESTEP") else rnd.randint(1,3)):
        wl.rnd=rnd
        k,L=wl.callee_transform(reg,tmp)
        names.append(k)
        steps+=L
        if rnd.random()<(0.5 if os.environ.get("ONESTEP") else 0.35):
            names.append('call'); steps+=['.word 0x4F22','mov.l =0x06009100,r1','jsr @r1','nop','.word 0x4F26']
    wk=rnd.choice(['loop_l','loop_b','single_neg','nested'])
    if wk=='loop_l': w=['mov #4,r1','L:','mov.l r1,@-%s'%reg,'dt r1','bf L']
    elif wk=='loop_b': w=['mov #16,r1','L:','mov.b r1,@-%s'%reg,'dt r1','bf L']
    elif wk=='single_neg': w=['mov #1,r1','add #-16,%s'%reg,'mov.l r1,@%s'%reg]
    else: w=['mov #2,r5','O:','mov #2,r1','L:','mov.l r1,@-%s'%reg,'dt r1','bf L','dt r5','bf O']
    # nested needs 4 stores: r5=2 outer * 2 inner = 4
    code=['add #-32,r15','mov #5,r0','mov.l r0,@r15','mov r15,%s'%reg,'add #16,%s'%reg]+steps+w+['mov.l @r15,r0','add #32,r15','rts','nop']
    out.append('name WF2 seed=%d i=%d steps=%s walk=%s'%(seed,i,'+'.join(names),wk))
    out.append('asm 06009000'); out+=code; out.append('endasm')
    out.append('asm 06009100'); out+=['rts','nop']; out.append('endasm')
    out.append('lend 06009100' if rnd.random()<0.5 else '')
    out.append('quiet'); out.append('args 0 0 0 0'); out.append('end')
D=os.path.dirname(os.path.abspath(__file__))
open(D+'/wf2_%d.src'%seed,'w').write('\n'.join(out)+'\n')
sp=subprocess.run(['python3',D+'/mk.py',D+'/wf2_%d.src'%seed],capture_output=True,text=True)
if sp.returncode: print(sp.stderr[-300:]); sys.exit(1)
open(D+'/wf2_%d.spec'%seed,'w').write(sp.stdout)
def lim(): resource.setrlimit(resource.RLIMIT_AS,(4_000_000_000,4_000_000_000))
pr=subprocess.run([D+'/drv',D+'/scr_wf2',D+'/wf2_%d.spec'%seed],capture_output=True,text=True,preexec_fn=lim,timeout=1500)
o=pr.stdout; open(D+'/wf2_%d.out'%seed,'w').write(o)
stale=0; tot=0; reach=0
for b in o.split('\n====  ')[1:]:
    name=b.split('\n')[0]
    runs=re.findall(r'RUN .*?-> (?:ret|NORET) r0=([0-9A-F]+)',b)
    if not runs: continue
    r0=int(runs[0],16); tot+=1
    if r0==5: continue
    reach+=1
    rets=[l.strip().split(';')[0].strip() for l in b.split('\n') if re.match(r'\s+return\b',l)]
    ent=[r for r in rets[-1:] if re.fullmatch(r'return (0x)?[0-9a-f]+',r)]
    ps=[l for l in b.split('\n') if l.strip().startswith('PROTO')]
    lent='known=1' in ps[-1] if ps else False
    if ent and int(ent[0].split()[1],0)!=r0:
        stale+=1; print('STALE','LENT' if lent else 'withheld',name,'text',ent[0],'run',hex(r0))
print('programs',tot,'walk reaches the sentinel',reach,'stale',stale)
