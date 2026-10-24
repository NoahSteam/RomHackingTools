import re,collections,sys
t=open(sys.argv[1]).read().split('==== ')[1:]
c=collections.Counter(); tot=collections.Counter(); fl=collections.Counter(); ex={}
for b in t:
    name=b.splitlines()[0]
    k=re.search(r'obtain=(\w+) channel=(\w+)',name).groups()
    ps=re.findall(r"PROTO known=(\d) .*? preserved=([0-9A-F]+)",b)
    fl_lent=ps[-2][0]=='1'; fpres=int(ps[-2][1],16)
    ent=b.split('ENTRY',1)[-1]
    rets=[l.strip().split(';')[0].strip() for l in ent.splitlines() if re.match(r'\s+return',l)]
    run=re.findall(r'-> \w+ r0=(\w+)',b)
    stale=bool(rets) and run and (not re.fullmatch(r'return (0x)?[0-9a-fA-F]+',rets[-1]) and 'in_r14' in rets[-1] or (re.fullmatch(r'return (0x)?[0-9a-fA-F]+',rets[-1]) and int(rets[-1].split()[1],0)!=int(run[0],16)))
    tot[k[1]]+=1
    if stale: c[k[1]]+=1; ex.setdefault(k[1],(name,rets[-1:],run))
    if fl_lent and (fpres>>14)&1: fl[k[1]]+=1
for k in sorted(tot): print(k,'fixtures',tot[k],'stale text',c[k],'f lent with r14 preserved',fl[k])
print(ex)
