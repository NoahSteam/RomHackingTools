#!/usr/bin/env python3
"""Area 3, A3-210 and A3-211: the function texts of two token dumps (a7_round2/corpus.sh's <tag>_alone.tsv / _lent.tsv) that differ, with
each caution ("this call may change ...") of either side and the call printed after it, and whether any other line differs.
usage: cautions.py <baseline .tsv> <new .tsv>"""
import re, sys

def texts(p):
    out, cur = {}, None
    for l in open(p, encoding='utf-8', errors='replace'):
        t = l.rstrip('\n').split('\t')
        if t[0] == 'F': cur = t[2]; out[cur] = []
        elif t[0] == 'L' and cur: out[cur].append(t[3] if len(t) > 3 else '')
    return out

def notes(lines):
    """(caution text, the call printed after it) for each caution, and the lines that are no comment."""
    out, code, cur = [], [], None
    for i, l in enumerate(lines):
        s = l.strip()
        if cur is not None:
            cur += ' ' + s
            if '*/' in s:
                if 'this call may change' in cur:
                    call = next((re.search(r'func_0x[0-9a-f]{8}', x).group(0) for x in lines[i + 1:] if re.search(r'func_0x[0-9a-f]{8}', x)), '?')
                    out.append((re.sub(r'\s+', ' ', cur), call))
                cur = None
        elif s.startswith('/*'):
            cur = s
            if '*/' in s:
                if 'this call may change' in s:
                    call = next((re.search(r'func_0x[0-9a-f]{8}', x).group(0) for x in lines[i + 1:] if re.search(r'func_0x[0-9a-f]{8}', x)), '?')
                    out.append((s, call))
                cur = None
        else: code.append(l)
    return out, code

a, b = texts(sys.argv[1]), texts(sys.argv[2])
n = 0
for k in sorted(set(a) | set(b)):
    if a.get(k) == b.get(k): continue
    n += 1
    na, ca = notes(a.get(k, [])); nb, cb = notes(b.get(k, []))
    print('%s%s' % (k, '' if ca == cb else '  (OTHER LINES DIFFER)'))
    short = lambda t: re.match(r'/\* this call may change (.*?), read after', t).group(1) + (' +likewise' if 'likewise' in t else '')
    for t, c in na:
        if (t, c) not in nb: print('   - %s @%s' % (short(t), c))
    for t, c in nb:
        if (t, c) not in na: print('   + %s @%s' % (short(t), c))
print('%d differ' % n)
