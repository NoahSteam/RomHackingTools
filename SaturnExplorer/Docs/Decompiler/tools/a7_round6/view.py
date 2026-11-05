#!/usr/bin/env python3
# Area 7 round 6: a probe run's output (SE_A7R4_PROBE) as text with each line's tokens: drawn ones as addr:text, empty ones as +addr,
# and the materialised flag as *. usage: view.py <probe output> [function name]
import sys
want = sys.argv[2] if len(sys.argv) > 2 else None
cur = None; lines = {}; order = []
def flush():
    if cur and (want is None or cur == want):
        print('==== ' + cur)
        for li in order:
            text, first, toks = lines[li]
            parts = []
            for (b, e, a, mn, tx, m) in toks:
                if a == '00000000': continue
                parts.append(('+' + a[2:] if b == e else a[2:] + ':' + tx) + ('*' if m == '1' else ''))
            print('%-60s first %s  %s' % (text[:60], first[2:], ' '.join(parts)))
for raw in open(sys.argv[1]):
    raw = raw.rstrip('\n')
    if raw.startswith('F\t'):
        flush(); cur = raw.split('\t')[1]; lines = {}; order = []
    elif raw.startswith('L\t'):
        f = raw.split('\t'); lines[f[1]] = (f[3], f[2], []); order.append(f[1])
    elif raw.startswith('T\t'):
        f = raw.split('\t')
        lines[f[1]][2].append((f[3], f[4], f[6], f[9], f[10], f[11] if len(f) > 11 else '0'))
flush()
