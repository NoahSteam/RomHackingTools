#!/usr/bin/env python3
"""escsum.py out [-v] : per fixture of a driver output, the entry's lent/withheld state, whether the oracle rejects, and a text whose single constant
return differs from the run's r0. Prints the flagged ones (name, state, texts' returns, run r0)."""
import sys, re
from collections import Counter
t = open(sys.argv[1]).read().split('==== ')[1:]
flag = Counter(); tot = Counter(); shown = 0
for b in t:
    lines = b.splitlines(); name = lines[0].strip()
    ps = re.findall(r"PROTO known=(\d).*?withheld='([^']*)'", b)
    lent = bool(ps) and ps[-1][0] == '1'
    runs = re.findall(r'RUN (.*?) args=(.*?) -> (\w+) r0=(\w+) r1=(\w+) ORACLE (\w+) ?(.*)', b)
    ent = b.split('ENTRY', 1)[-1]
    rets = [l.strip().split(';')[0].strip() for l in ent.splitlines() if re.match(r'\s+return\b', l)]
    bad = any(r[5] == 'REJECTS' and 'cannot evaluate' not in r[6] for r in runs)
    const = [r for r in rets if re.fullmatch(r'return (0x)?[0-9a-fA-F]+', r)]
    stale = False
    if len(rets) == 1 and const and runs:
        v = int(const[0].split()[1], 0)
        stale = any(int(r[3], 16) != v for r in runs[:1])
    key = 'lent' if lent else 'withheld'
    tot[key] += 1
    if bad or stale:
        flag[key] += 1
        print('FLAG', 'REJECT' if bad else 'STALE', key, name, rets[-2:], [r[3] for r in runs])
print('fixtures', dict(tot), 'flagged', dict(flag))
