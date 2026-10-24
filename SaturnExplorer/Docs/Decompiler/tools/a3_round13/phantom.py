import os,re,sys,collections
def params(t):
    # signature: first non-comment line(s) up to '{'
    head=t.split('{',1)[0]
    head=re.sub(r'//[^\n]*\n','',head)
    m=re.search(r'\((.*)\)',head,re.S)
    if not m: return []
    ps=[p.strip() for p in m.group(1).replace('\n',' ').split(',') if p.strip() and p.strip()!='void']
    return [re.findall(r'(param_\d+)\s*$',p)[0] for p in ps if re.findall(r'(param_\d+)\s*$',p)]
def body(t): return t.split('{',1)[1] if '{' in t else ''
tot=collections.Counter()
ex=[]
for img in sys.argv[1:]:
    A='out_base_%s/lend_after'%img; B='out_new_%s/lend_after'%img
    for f in sorted(os.listdir(B)):
        a=open(os.path.join(A,f)).read(); b=open(os.path.join(B,f)).read()
        if a==b: continue
        pa,pb=params(a),params(b)
        withheld='signature withheld' in b
        if len(pb)>len(pa):
            extra=[p for p in pb if p not in pa]
            used=[p for p in extra if re.search(r'\b%s\b'%p,body(b))]
            tot[(img,'more-params',withheld)]+=1
            tot[(img,'extra-params-total')]+=len(extra)
            tot[(img,'extra-unused')]+=len(extra)-len(used)
            if len(extra)-len(used)>=4: ex.append((img,f,len(pa),len(pb)))
        elif len(pb)<len(pa): tot[(img,'fewer-params',withheld)]+=1
for k,v in sorted(tot.items(),key=str): print(k,v)
print(ex[:10], len(ex))
