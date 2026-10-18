#!/usr/bin/env python3
"""hf2.py seed count : hidden-input fuzzer v2 (area 3 round 11). Programs with loops, calls to lent leaves, branches, stack arguments, frame slots,
MACH/MACL/GBR/T/r12-r14 reads. Each lent function is run as is and once per perturbed input the signature does not hold; r0 must not change."""
import random, sys, os, re, subprocess, resource
seed=int(sys.argv[1]); count=int(sys.argv[2]); mode=sys.argv[3] if len(sys.argv)>3 else 'run'
D=os.path.dirname(os.path.abspath(__file__))
rnd=random.Random(seed)
HID=[0,1,2,3,8,9,10,11,12,13,14]
CALLEES={
 'keep':   (['rts','nop'], 'keeps r0'),
 'const7': (['mov #7,r0','rts','nop'], 'r0=7'),
 'argr4':  (['mov r4,r0','rts','nop'], 'r0=r4'),
 'sum56':  (['mov r5,r0','add r6,r0','rts','nop'], 'r0=r5+r6'),
 'clob8':  (['mov.l r8,@-r15','mov #9,r8','mov.l @r15+,r8','mov #3,r0','rts','nop'], 'r0=3, r8 kept'),
 'chg9':   (['mov #4,r9','rts','nop'], 'changes r9'),
 'ldr4':   (['mov.l @r4,r0','rts','nop'], 'r0=*r4'),
}
def gen_prog():
    L=[]; lbl=[0]; uses_call=[]
    def newl():
        lbl[0]+=1; return 'X%d'%lbl[0]
    def reg(): return rnd.choice(HID+[4,5,6,7])
    def hid(): return rnd.choice(HID)
    def work(): return rnd.choice([0,1,2,3,8,9,10,11,4])
    frame = rnd.random()<0.5
    if frame: L.append('add #-16,r15')
    n=rnd.randint(2,8)
    for _ in range(n):
        c=rnd.randrange(30)
        d=work(); s=reg()
        if c<3: L.append('mov r%d,r%d'%(s,d))
        elif c<6: L.append(rnd.choice(['add','sub','and','or','xor'])+' r%d,r%d'%(s,d))
        elif c<8: L.append(rnd.choice(['extu.b','extu.w','exts.b','exts.w','swap.b','swap.w','neg','not'])+' r%d,r%d'%(s,d))
        elif c<10: L.append(rnd.choice(['shll','shlr','shll2','shlr2','shll8','shlr8','shll16','shlr16','rotl','rotr','shar'])+' r%d'%d)
        elif c<11: L.append('add #%d,r%d'%(rnd.choice([1,-1,5,127,-128]),d))
        elif c<12: L.append('mov #%d,r%d'%(rnd.choice([0,1,5,-1,100]),d))
        elif c<13: L.append(rnd.choice(['and #1,r0','or #2,r0','xor #240,r0','or #240,r0','and #15,r0','xor #0,r0','or #0,r0']))
        elif c<15: L.append('cmp/eq r%d,r%d'%(s,d)); L.append('movt r%d'%rnd.choice([0,1,2,3]))
        elif c<16: L.append('tst r%d,r%d'%(s,d)); L.append('movt r%d'%rnd.choice([0,1,2,3]))
        elif c<17: L.append('stc sr,r%d'%rnd.choice([0,1,2,3]))
        elif c<18: L.append('stc gbr,r%d'%rnd.choice([0,1,2,3]))
        elif c<19: L.append(rnd.choice(['clrt','sett']))
        elif c<20: L.append(rnd.choice(['addc','subc','negc'])+' r%d,r%d'%(s,d))
        elif c<21: L.append('mul.l r%d,r%d'%(s,d)); L.append('sts macl,r%d'%rnd.choice([0,1,2,3]))
        elif c<22: L.append('sts mach,r%d'%rnd.choice([0,1,2,3])); 
        elif c<23 and frame:
            off=rnd.choice([0,4,8,12]); sz=rnd.choice('lwb')
            L.append('mov r15,r4' if False else 'mov r15,r2'); L.append('add #%d,r2'%off)
            L.append('mov.%s r%d,@r2'%(sz,s)); L.append('mov.%s @r2,r%d'%(rnd.choice('lwb'),rnd.choice([0,1,3])))
        elif c<24:
            k=rnd.choice([0,4,8,12]); off=k+(16 if frame else 0)
            L.append('mov.l @(%d,r15),r%d'%(off,rnd.choice([0,1,2,3])))   # a stack argument
        elif c<26:
            lab=newl(); L.append(rnd.choice(['tst r%d,r%d'%(s,d),'cmp/eq r%d,r%d'%(s,d),'cmp/pl r%d'%d])); L.append(rnd.choice(['bt','bf'])+' '+lab)
            for _ in range(rnd.randint(1,2)): L.append(rnd.choice(['mov #5,r0','add #1,r0','mov r%d,r0'%hid(),'mov r%d,r1'%hid(),'add r%d,r0'%hid(),'mov #2,r0','xor r%d,r0'%hid()]))
            L.append(lab+':')
        elif c<27:
            # counted loop; count constant or from a hidden register's low bits
            lab=newl()
            if rnd.random()<0.5: L.append('mov #%d,r7'%rnd.choice([2,3,4]))
            else: L += ['mov r%d,r0'%hid(),'and #3,r0','add #1,r0','mov r0,r7']
            L.append(lab+':')
            for _ in range(rnd.randint(1,2)): L.append(rnd.choice(['add #1,r%d'%d,'add r%d,r%d'%(s,d),'mov #5,r%d'%d,'xor r%d,r%d'%(s,d)]))
            L += ['dt r7','bf '+lab]
        elif c<29:
            name=rnd.choice(list(CALLEES)); uses_call.append(name)
            L += ['.word 0x4F22','mov.l =CALL_%s,r1'%name,'jsr @r1','nop','.word 0x4F26']
        else: L.append('mov r%d,r0'%hid())
    if rnd.random()<0.7: L.append('mov r%d,r0'%rnd.choice(HID+[0]))  if rnd.random()<0.4 else None
    # an extra use of r0 so the engine may drop the return
    L += ['or r0,r1']
    if frame: L.append('add #16,r15')
    ret=rnd.choice(['plain','delay'])
    if ret=='plain': L += ['rts','nop']
    else: L += ['rts','mov r%d,r0'%rnd.choice(HID)]
    return L,uses_call
out=[]; names=[]
CALLADDR={}
for i,k in enumerate(CALLEES): CALLADDR[k]=0x06009100+0x40*i
for p in range(count):
    L,uc=gen_prog()
    L=[x for x in L if x is not None]
    txt=[]
    for x in L:
        m=re.fullmatch(r'mov\.l =CALL_(\w+),r1',x)
        txt.append('mov.l =0x%08X,r1'%CALLADDR[m.group(1)] if m else x)
    # literal pool distance: fine for short programs
    name='HF seed=%d p=%d calls=%s'%(seed,p,','.join(uc))
    out.append('name '+name); out.append('asm 06009000'); out+=txt; out.append('endasm')
    lend=[]
    for k in sorted(set(uc)):
        out.append('asm 0x%08X'%CALLADDR[k] if False else 'asm %08X'%CALLADDR[k]); out+=CALLEES[k][0]; out.append('endasm'); lend.append('%08X'%CALLADDR[k])
    if lend: out.append('lend '+' '.join(lend))
    out.append('quiet')
    base=['0600F100','0','0','0']
    out.append('args '+' '.join(base))
    pert=[]
    for h in [0,1,2,3,8,9,10,11,12,13,14]: pert.append('r%d=%X'%(h,0x7000+0x11*h))
    for a in range(4): pert.append('r%d=%X'%(4+a,0x0600F200+0x10*a))
    pert.append('sr=1'); pert.append('gbr=600F300'); pert.append('mach=77'); pert.append('macl=88')
    for k in range(4): pert.append('M%X=%X'%(0x0600F000+4*k,0x0600F400+k))
    for q in pert: out.append('args '+' '.join(base)+' '+q)
    out.append('end')
open(D+'/hf_%d.src'%seed,'w').write('\n'.join(out)+'\n')
spec=subprocess.run(['python3',D+'/mk.py',D+'/hf_%d.src'%seed],capture_output=True,text=True)
if spec.returncode: print(spec.stderr[-300:]); sys.exit(1)
open(D+'/hf_%d.spec'%seed,'w').write(spec.stdout)
if mode=='gen': sys.exit(0)
def lim(): resource.setrlimit(resource.RLIMIT_AS,(4_000_000_000,4_000_000_000))
pr=subprocess.run([D+'/drv',D+'/scr_hf',D+'/hf_%d.spec'%seed],capture_output=True,text=True,preexec_fn=lim,timeout=3000)
o=pr.stdout; open(D+'/hf_%d.out'%seed,'w').write(o)
blocks=o.split('\n====  ')[1:]
lent=0; bad=0; progs=len(blocks)
for b in blocks:
    name=b.split('\n')[0]
    ps=[l for l in b.split('\n') if l.strip().startswith('PROTO')]
    if not ps or 'known=1' not in ps[-1]: continue
    lent+=1
    nparams=int(re.search(r'params=(\d+)',ps[-1]).group(1))
    runs=re.findall(r'RUN (.*?)\s*args=(.*?)-> (ret|NORET) r0=([0-9A-F]+)',b)
    if len(runs)<len(pert)+1 or runs[0][2]!='ret': continue
    base0=int(runs[0][3],16)
    for k,(rg,a,st,v) in enumerate(runs[1:]):
        if st!='ret': continue
        v=int(v,16)
        if v==base0: continue
        pk=pert[k]
        mm=re.match(r'r(\d+)=',pk)
        if mm:
            r=int(mm.group(1))
            if 4<=r<=7 and r-4<nparams: continue          # a parameter the signature holds
            if r==0 and v==int(pk.split('=')[1],16): continue   # r0 left as it came in
        if pk.startswith('sr') and 'in_sr' in b: continue          # SR returned whole is the text's own (A3-5)
        if pk.startswith('M'):
            addr=int(pk[1:].split('=')[0],16); w=(addr-0x0600F000)//4
            if 4+w<nparams: continue                       # a stack parameter it holds
        print('FINDING',name,'perturb',pk,'base',hex(base0),'now',hex(v),'params',nparams); bad+=1; break
print('seed',seed,'programs',progs,'lent',lent,'findings',bad)
