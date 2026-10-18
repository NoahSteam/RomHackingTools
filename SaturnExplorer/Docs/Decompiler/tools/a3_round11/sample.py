#!/usr/bin/env python3
"""sample.py <hwram image> <addr>... : decompile each function with its callees lent (found from the text), print the text, the proto, the disassembly."""
import subprocess, sys, re, os, resource
D=os.path.dirname(os.path.abspath(__file__))
img=sys.argv[1]
def lim(): resource.setrlimit(resource.RLIMIT_AS,(4_000_000_000,4_000_000_000))
def run(spec):
    open(D+'/smp.spec','w').write(spec)
    return subprocess.run([D+'/drv',D+'/scr_smp',D+'/smp.spec'],capture_output=True,text=True,preexec_fn=lim,timeout=300).stdout
def callees(txt):
    return sorted(set(int(x,16) for x in re.findall(r'func_0x([0-9a-fA-F]{8})\(',txt)))
def chain(addr, depth=0, seen=None):
    seen=seen if seen is not None else set()
    order=[]
    spec='name probe\nimage %s\nentry %08X\nnoresult\nargs 0 0 0 0\nend\n'%(img,addr)
    out=run(spec)
    for c in callees(out):
        if c in seen or c==addr: continue
        seen.add(c)
        if depth<4: order+=chain(c,depth+1,seen)
        order.append(c)
    return order
for a in sys.argv[2:]:
    a=int(a,16)
    order=[]
    for c in chain(a): 
        if c not in order: order.append(c)
    spec='name sample %08X\nimage %s\nentry %08X\nlend %s\nnoresult\nargs 0 0 0 0\nend\n'%(a,img,a,' '.join('%08X'%c for c in order))
    out=run(spec)
    print('='*30,'%08X'%a,'lent callees tried:',' '.join('%08X'%c for c in order))
    keep=[l for l in out.split('\n') if not re.match(r'\s+(access|ended|RUN):?',l) and l.strip()]
    print('\n'.join(keep[-60:]))
    dis=subprocess.run([D+'/drv','x','--dis',img,'%08X'%a,'24'],capture_output=True,text=True,preexec_fn=lim)
    print(dis.stdout[:1800])
