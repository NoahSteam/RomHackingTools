#!/usr/bin/env python3
"""Area 3, A3-197: two token dumps (a7_round2/corpus.sh's <tag><img>_<mode>.tsv) compared function by function: for each function whose text
differs, whether it differs only in the A3-197 caution, only in comments, or in code, and the diff. usage: cmp.py <baseline tsv> <new tsv> [function ...]"""
import sys, difflib, re
def texts(p):
    out, cur = {}, None
    for l in open(p, encoding='utf-8', errors='replace'):
        t = l.rstrip('\n').split('\t')
        if t[0] == 'F': cur = t[2]; out[cur] = []
        elif t[0] == 'L' and cur: out[cur].append(t[3] if len(t) > 3 else '')
    return out
a, b = texts(sys.argv[1]), texts(sys.argv[2])
only = sys.argv[3:] 
for k in sorted(set(a)|set(b)):
    if a.get(k)==b.get(k): continue
    if only and k not in only: continue
    strip=lambda L:[x for x in re.sub(r'/\*.*?\*/', '', '\n'.join(L), flags=re.S).split('\n') if x.strip()]
    codeSame = strip(a.get(k,[]))==strip(b.get(k,[]))
    # ...and without A3-197's cautions only: whether any other comment (a frame note) changed too
    caut=lambda L:[x for x in re.sub(r'/\* this call may change .*?\*/', '', '\n'.join(L), flags=re.S).split('\n') if x.strip()]
    onlyCaution = caut(a.get(k,[]))==caut(b.get(k,[]))
    print('==',k,'only the caution' if onlyCaution else 'code same, other comments differ' if codeSame else 'CODE DIFFERS')
    for d in difflib.unified_diff(a.get(k,[]),b.get(k,[]),lineterm='',n=1):
        if d.startswith(('+','-','@')): print('  ',d[:200])
