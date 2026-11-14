#!/usr/bin/env python3
"""Area 3, A3-203 and A3-204: the function texts of two token dumps (a7_round2/corpus.sh's <tag>_alone.tsv / _lent.tsv) that differ, with a
unified diff of each. usage: difftexts.py <baseline.tsv> <new.tsv> [function ...]   (default: every function that differs)"""
import difflib, sys
def texts(p):
    out, cur = {}, None
    for l in open(p, encoding='utf-8', errors='replace'):
        t = l.rstrip('\n').split('\t')
        if t[0] == 'F': cur = t[2]; out[cur] = []
        elif t[0] == 'L' and cur: out[cur].append(t[3] if len(t) > 3 else '')
    return out
a, b = texts(sys.argv[1]), texts(sys.argv[2])
names = sys.argv[3:] or sorted(k for k in set(a) | set(b) if a.get(k) != b.get(k))
for k in names:
    print('=== ' + k)
    for l in difflib.unified_diff(a.get(k, []), b.get(k, []), lineterm='', n=2):
        if not l.startswith(('---', '+++')): print(l)
