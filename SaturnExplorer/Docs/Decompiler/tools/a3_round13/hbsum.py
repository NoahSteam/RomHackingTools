import re,collections,sys
t=open(sys.argv[1]).read().split('==== ')[1:]
c=collections.Counter(); tot=collections.Counter(); lent=collections.Counter(); ex={}
for b in t:
    name=b.splitlines()[0]
    k=re.search(r'obtain=(\w+) channel=(\w+) write=(\w+)',name).groups()
    ps=re.findall(r"PROTO known=(\d).*?withheld='([^']*)'",b)
    # PROTO order: helper (if lent) then g then P ; g is the second to last, P the last
    gl=ps[-2][0]=='1'; pl=ps[-1][0]=='1'
    ent=b.split('ENTRY',1)[-1]
    rets=[l.strip().split(';')[0].strip() for l in ent.splitlines() if re.match(r'\s+return',l)]
    run=re.findall(r'-> \w+ r0=(\w+)',b)
    stale=bool(rets) and re.fullmatch(r'return (0x)?[0-9a-fA-F]+',rets[-1]) and run and int(rets[-1].split()[1],0)!=int(run[0],16)
    tot[k[1]]+=1
    if stale: c[k[1]]+=1; ex.setdefault(k[1],name)
    if stale and pl: lent['P lent+stale '+k[1]]+=1
    if stale and gl: lent['g lent+stale '+k[1]]+=1
for k in sorted(tot): print(k,'fixtures',tot[k],'stale',c[k])
print(dict(lent)); print(ex)
