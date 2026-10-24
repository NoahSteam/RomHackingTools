import os
import os,re,sys,collections
H=os.environ.get('A3R13_IMG','.')   # the directory with hwram007.bin and hwram008.bin; run in the output directory of corp.sh
def prev_is_push(img,addr):
    d=open('%s/hwram%s.bin'%(H,img),'rb').read()
    off=addr-0x06000000-2
    if off<0: return False
    w=(d[off]<<8)|d[off+1]
    return (w&0xF00F)==0x2006 and ((w>>8)&0xF)==15   # mov.l Rm,@-r15 : 0x2F m 6
def params(t):
    head=t.split('{',1)[0]; head=re.sub(r'//[^\n]*\n','',head)
    m=re.search(r'\((.*)\)',head,re.S)
    if not m: return []
    ps=[p.strip() for p in m.group(1).replace('\n',' ').split(',') if p.strip() and p.strip()!='void']
    return [re.findall(r'(param_\d+)\s*$',p)[0] for p in ps if re.findall(r'(param_\d+)\s*$',p)]
def body(t): return t.split('{',1)[1] if '{' in t else ''
KN={'r14':'a frame address is in r14 here','slot':'a local at or above the stack pointer is read after this call','stackarg':'a frame address is in a stack argument slot here','r2':'a frame address is in r2 here'}
for img in ('007','008'):
    A='out_base_%s/lend_after'%img; B='out_new_%s/lend_after'%img
    c=collections.Counter(); notes=collections.Counter(); nfun=collections.Counter()
    for f in sorted(os.listdir(B)):
        addr=int(f[:8],16)
        mid=prev_is_push(img,addr)
        a=open(os.path.join(A,f)).read(); b=open(os.path.join(B,f)).read()
        key='mid' if mid else 'real'
        c[(key,'functions')]+=1
        if a!=b: c[(key,'changed')]+=1
        pa,pb=params(a),params(b)
        if len(pb)>len(pa):
            extra=[p for p in pb if p not in pa]; used=[p for p in extra if re.search(r'\b%s\b'%p,body(b))]
            c[(key,'texts with more params')]+=1; c[(key,'extra params')]+=len(extra); c[(key,'extra unused')]+=len(extra)-len(used)
        for k,p in KN.items():
            n=b.count(p)-a.count(p)
            if n>0: notes[(key,k)]+=n; nfun[(key,k)]+=1
    print(img); 
    for k,v in sorted(c.items()): print('  ',k,v)
    for k,v in sorted(notes.items()): print('  notes',k,v,'in',nfun[k],'functions')
