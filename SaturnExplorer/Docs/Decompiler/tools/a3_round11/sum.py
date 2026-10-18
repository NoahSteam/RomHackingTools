#!/usr/bin/env python3
import sys,re
t=open(sys.argv[1]).read().split('==== ')
for blk in t[1:]:
    lines=blk.splitlines()
    name=lines[0].strip()
    ret=[l.strip().split(';')[0].strip() for l in lines if re.match(r'\s+return',l)]
    wh=''
    m=re.search(r"known=(\d).*withheld='([^']*)'",blk)
    # last PROTO is the entry's
    ps=re.findall(r"PROTO known=(\d).*?withheld='([^']*)'",blk)
    lent=ps[-1][0]=='1' if ps else None
    runs=re.findall(r'RUN (.*?) args=(.*?) -> (\w+) r0=(\w+)',blk)
    note=[l.strip() for l in lines if 'frame address' in l or 'may write' in l or 'note' in l.lower()][:2]
    print('%-60s %s ret=%s runs=%s %s'%(name[:60],'LENT' if lent else 'withheld('+(ps[-1][1][:40] if ps else '')+')',ret,[(r[3]) for r in runs],'' ))
