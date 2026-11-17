#!/usr/bin/env python3
"""Area 3, A3-214 fix: the unified diff of each function text that differs between two token dumps (a7_round2/corpus.sh's <tag>_alone.tsv /
_lent.tsv), or only of the functions named.
usage: fndiff.py <baseline .tsv> <new .tsv> [function address ...]"""
import difflib, sys

def texts(p):
    out, cur = {}, None
    for l in open(p, encoding='utf-8', errors='replace'):
        t = l.rstrip('\n').split('\t')
        if t[0] == 'F': cur = t[2]; out[cur] = []
        elif t[0] == 'L' and cur: out[cur].append(t[3] if len(t) > 3 else '')
    return out

a, b = texts(sys.argv[1]), texts(sys.argv[2])
want = set(x.upper() for x in sys.argv[3:])
for k in sorted(set(a) | set(b)):
    if a.get(k) == b.get(k) or (want and k.upper() not in want): continue
    print('==== %s' % k)
    for l in difflib.unified_diff(a.get(k, []), b.get(k, []), lineterm='', n=1):
        if not l.startswith(('---', '+++')): print(l)
