#!/usr/bin/env python3
"""f15sum.py out : per-fixture verdicts of the driver's output (LEDGER lines, ORACLE mismatches)."""
import sys, re
t = open(sys.argv[1]).read().split('==== ')[1:]
n = 0; led = 0; wrong = 0; cant = 0; rows = []
for b in t:
    n += 1
    name = b.splitlines()[0].strip()
    ps = re.findall(r"PROTO known=(\d).*?withheld='([^']*)'", b)
    lent = bool(ps) and ps[-1][0] == '1'
    runs = re.findall(r'ORACLE (agrees|REJECTS) ?(.*)', b)
    bad = [r[1].strip() for r in runs if r[0] == 'REJECTS']
    real = [x for x in bad if x.startswith('returns ') and x != 'returns nothing']
    cn = [x for x in bad if x.startswith('cannot evaluate')]
    ledbad = 'LEDGER BAD' in b
    err = 'RUN error' in b or 'status 0' not in b.split('PROTO')[0]
    led += ledbad; wrong += bool(real); cant += bool(cn)
    if ledbad or real or err:
        ret = [l.strip().split(';')[0].strip() for l in b.splitlines() if re.match(r'\s+return', l)]
        rows.append('%s | %s | %s | ledger=%s | %s%s' % (name[:70], 'LENT' if lent else 'withheld', ret[-1:], 'BAD' if ledbad else 'ok', real[:1], ' ERR' if err else ''))
print('fixtures %d, ledger BAD %d, oracle mismatch %d, cannot-evaluate %d' % (n, led, wrong, cant))
for r in rows: print(r)
