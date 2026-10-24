import re,collections,sys
t=open(sys.argv[1]).read().split('==== ')[1:]
tot=collections.Counter(); st=collections.Counter(); gl=collections.Counter(); ex={}
for b in t:
    name=b.splitlines()[0]
    k=re.search(r'below=(\w+) channel=(\w+)',name).groups()
    ps=re.findall(r"PROTO known=(\d)",b)
    g_lent=ps[0]=='1'
    ent=b.split('ENTRY',1)[-1]
    rets=[l.strip().split(';')[0].strip() for l in ent.splitlines() if re.match(r'\s+return',l)]
    run=re.findall(r'-> \w+ r0=(\w+)',b)
    stale=bool(rets) and re.fullmatch(r'return (0x)?[0-9a-fA-F]+',rets[-1]) and run and int(rets[-1].split()[1],0)!=int(run[0],16)
    tot[k]+=1
    if stale: st[k]+=1; ex.setdefault(k,(name,rets[-1:],run))
    if g_lent: gl[k]+=1
for k in sorted(tot): print(k,'fixtures',tot[k],'g lent',gl[k],'stale',st[k])
print(list(ex.items())[:3])
