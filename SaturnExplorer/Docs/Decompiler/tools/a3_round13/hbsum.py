#!/usr/bin/env python3
"""hbsum.py out : per fixture, g (the first PROTO) lent or withheld, P's last return, and the run's r0; flags g lent with the stale return, and any stale return."""
import sys, re
from collections import Counter
t = open(sys.argv[1]).read().split('==== ')[1:]
c = Counter(); bad = []
for b in t:
    name = b.splitlines()[0].strip()
    ps = re.findall(r"PROTO known=(\d).*?withheld='([^']*)'", b)
    gl = ps[0][0] == '1'
    ent = b.split('ENTRY', 1)[-1]
    rets = [l.strip().split(';')[0].strip() for l in ent.splitlines() if re.match(r'\s+return', l)]
    run = re.findall(r'-> \w+ r0=(\w+)', b)
    stale = bool(rets) and re.fullmatch(r'return (0x)?[0-9a-fA-F]+', rets[-1]) and run and int(rets[-1].split()[1], 0) != int(run[0], 16)
    k = re.search(r'obtain=(\w+) channel=(\w+) write=(\w+)', name).groups()
    c[('g lent', gl)] += 1
    if stale or (gl and 'mov' not in k[2] and False):
        bad.append((name, 'g lent' if gl else 'g withheld', rets[-1:], run))
print(dict(c))
for x in bad: print('STALE', x)
