#!/usr/bin/env python3
"""escsum14.py out [-v] : like escsum.py, but it also counts the texts the oracle rejects that return a register-like variable (xVar, iVar, in_...) or a
constant -- a local the engine took for unaliased -- which escsum.py's 'wrong constant' test does not see, and splits them by whether a call in the text
shows more than four arguments (a stack argument shown: A3-117) or not."""
import sys, re
from collections import Counter
t = open(sys.argv[1]).read().split('==== ')[1:]
cnt = Counter(); shown = 0
def nargs(line):
    m = re.search(r'func_0x[0-9a-f]+\((.*)\)\s*;', line)
    if not m: return 0
    s = m.group(1); d = 0; n = 1 if s.strip() else 0
    for ch in s:
        if ch in '([': d += 1
        elif ch in ')]': d -= 1
        elif ch == ',' and d == 0: n += 1
    return n
for b in t:
    name = b.splitlines()[0].strip()
    ps = re.findall(r"PROTO known=(\d).*?withheld='([^']*)'", b)
    lent = bool(ps) and ps[-1][0] == '1'
    runs = re.findall(r'RUN (.*?) args=(.*?) -> (\w+) r0=(\w+) r1=(\w+) ORACLE (\w+) ?(.*)', b)
    ent = b.split('ENTRY', 1)[-1]
    lines = ent.splitlines()
    rets = [l.strip().split(';')[0].strip() for l in lines if re.match(r'\s+return\b', l)]
    rej = any(r[5] == 'REJECTS' and 'cannot evaluate' not in r[6] for r in runs)
    const = [r for r in rets if re.fullmatch(r'return (0x)?[0-9a-fA-F]+', r)]
    stale = False
    if len(rets) == 1 and const and runs:
        stale = int(const[0].split()[1], 0) != int(runs[0][3], 16)
    regvar = len(rets) == 1 and re.fullmatch(r'return (xVar|iVar|uVar|pxVar|puVar|in_)\w*', rets[0]) is not None
    body = ' '.join(l.rstrip().strip() for l in lines)
    body = re.sub(r';\s+[0-9A-F]{8}\s+;', ';', body)    # a call that wraps: `...) ; 0600904A` then a line with only `;`
    body = re.sub(r';\s+[0-9A-F]{8}\b', ';', body)
    calls = [nargs(m.group(0)) for m in re.finditer(r'func_0x[0-9a-f]+\(.*?\)\s*;', body)]
    stackargs = any(n > 4 for n in calls)
    key = ('lent' if lent else 'withheld')
    if stale: cnt[key + ' STALE-const'] += 1; tag = 'STALE'
    elif rej and regvar: cnt[key + ' REG-var ' + ('stackarg-shown' if stackargs else 'NO-stackarg')] += 1; tag = 'REGVAR-' + ('SA' if stackargs else 'NOSA')
    else: tag = None
    if tag and (tag != 'REGVAR-SA' or '-v' in sys.argv):
        print('FLAG', tag, key, name, rets[-2:], [r[3] for r in runs][:1])
    cnt[key + ' total'] += 1
print(dict(cnt))
