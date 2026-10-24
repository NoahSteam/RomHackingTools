import re,sys
t=open(sys.argv[1]).read().split('==== ')[1:]
for b in t:
    name=b.splitlines()[0].strip()
    rets=[l.strip().split(';')[0].strip() for l in b.split('ENTRY',1)[-1].splitlines() if re.match(r'\s+return',l)]
    r0=re.findall(r'-> \w+ r0=(\w+)',b)
    ps=re.findall(r"PROTO known=(\d).*?withheld='([^']*)'",b)
    print('%-45s entry_lent=%s rets=%s run=%s %s'%(name[:45],ps[-1][0],rets,r0,ps[-1][1][:50]))
