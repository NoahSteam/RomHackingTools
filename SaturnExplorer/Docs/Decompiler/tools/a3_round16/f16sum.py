#!/usr/bin/env python3
"""f16sum.py out : per-fixture verdicts of the driver's output (drvmain16.inc): LEDGER BAD, ORACLE REJECTS (a real mismatch of a return), and
PRESERVE-LIE (the lent `preserved` mask claims r2..r14 equal to their entry values at every return and a run left one different; or `spDelta` differs
from the run's r15 change)."""
import sys, re
t = open(sys.argv[1]).read().split('==== ')[1:]
n = 0; led = 0; wrong = 0; cant = 0; lies = 0; lent = 0; rows = []
def regs(s): return {m.group(1): int(m.group(2), 16) for m in re.finditer(r'(r\d+)=([0-9A-F]{8})', s)}
for b in t:
    n += 1
    name = b.splitlines()[0].strip()
    ps = re.findall(r"PROTO known=(\d).*?preserved=([0-9A-F]+) spDelta=(-?\d+).*?withheld='([^']*)'", b)
    isl = bool(ps) and ps[-1][0] == '1'
    lent += isl
    runs = re.findall(r'ORACLE (agrees|REJECTS) ?(.*)', b)
    bad = [r[1].strip() for r in runs if r[0] == 'REJECTS']
    real = [x for x in bad if x.startswith('returns ') and x != 'returns nothing']
    cn = [x for x in bad if x.startswith('cannot evaluate')]
    ledbad = 'LEDGER BAD' in b
    err = 'RUN error' in b or 'status 0' not in b.split('PROTO')[0]
    lie = []
    if isl:
        pres = int(ps[-1][1], 16); sd = int(ps[-1][2])
        fin = re.findall(r'FINAL (.*)', b); ent = re.findall(r'ENTRYREGS (.*)', b)
        for f_, e_ in zip(fin, ent):
            F, E = regs(f_), regs(e_)
            for i in range(2, 15):
                if pres >> i & 1 and F.get('r%d' % i) != E.get('r%d' % i): lie.append('r%d' % i)
            if sd != -2147483648 and (F['r15'] - E['r15']) & 0xFFFFFFFF != sd & 0xFFFFFFFF: lie.append('sp')
    led += ledbad; wrong += bool(real); cant += bool(cn); lies += bool(lie)
    if ledbad or real or err or lie:
        ret = [l.strip().split(';')[0].strip() for l in b.splitlines() if re.match(r'\s+return', l)]
        rows.append('%s | %s | %s | ledger=%s | %s%s%s' % (name[:90], 'LENT' if isl else 'withheld', ret[-1:], 'BAD' if ledbad else 'ok', real[:1], ' ERR' if err else '', (' PRESERVE-LIE ' + ','.join(sorted(set(lie)))) if lie else ''))
print('fixtures %d, lent %d, ledger BAD %d, oracle mismatch %d, cannot-evaluate %d, preserve-lie %d' % (n, lent, led, wrong, cant, lies))
for r in rows: print(r)
