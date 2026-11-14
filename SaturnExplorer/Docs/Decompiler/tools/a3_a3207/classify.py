#!/usr/bin/env python3
"""Area 3, A3-207 (and A3-208, A3-209): the function texts of two sets of token dumps (a7_round2/corpus.sh's <tag><img>_alone.tsv / _lent.tsv)
that differ, by kind of change: a caution added or changed ("this call may change ..."), the slot note's wording ("a local ..." to "a saved
register's word ..."), the stack-argument note ("a frame address is/may be in a stack argument slot ..."), or other (any other line: code).
usage: classify.py <baseline dump dir> <baseline tag> <new dump dir> <new tag>   (e.g. master's dumps, tag m; this branch's, tag r)"""
import difflib, os, re, sys

def texts(p):
    out, cur = {}, None
    for l in open(p, encoding='utf-8', errors='replace'):
        t = l.rstrip('\n').split('\t')
        if t[0] == 'F': cur = t[2]; out[cur] = []
        elif t[0] == 'L' and cur: out[cur].append(t[3] if len(t) > 3 else '')
    return out

def comments(lines):
    """The comments of a text, each as one line (line breaks and indentation as one space), and the code lines."""
    notes, code, cur = [], [], None
    for l in lines:
        s = l.strip()
        if cur is not None:
            cur += ' ' + s
            if '*/' in s: notes.append(re.sub(r'\s+', ' ', cur)); cur = None
        elif s.startswith('/*'):
            if '*/' in s: notes.append(re.sub(r'\s+', ' ', s))
            else: cur = s
        else: code.append(l)
    return notes, code

def kind(a, b):
    na, ca = comments(a)
    nb, cb = comments(b)
    if ca != cb: return 'other'
    gone = [n for n in na if n not in nb]
    new = [n for n in nb if n not in na]
    kinds = set()
    for n in gone + new:
        if 'this call may change' in n: kinds.add('caution')
        elif 'is read after this call, which may write it as one of its stack arguments' in n: kinds.add('slot')
        elif 'stack argument slot here' in n: kinds.add('stack')
        else: kinds.add('other')
    return '+'.join(sorted(kinds)) or 'order'

base, btag, new, ntag = sys.argv[1:5]
for img in ('007', '008'):
    for mode in ('alone', 'lent'):
        a = texts(os.path.join(base, '%s%s_%s.tsv' % (btag, img, mode)))
        b = texts(os.path.join(new, '%s%s_%s.tsv' % (ntag, img, mode)))
        by = {}
        for k in sorted(set(a) | set(b)):
            if a.get(k) != b.get(k): by.setdefault(kind(a.get(k, []), b.get(k, [])), []).append(k)
        print('%s %s: %d differ; %s' % (img, mode, sum(len(v) for v in by.values()),
                                        '; '.join('%s %d: %s' % (k, len(v), ' '.join(v)) for k, v in sorted(by.items())) or 'none'))
