#!/usr/bin/env python3
"""Area 3 round 27: the cautions ("this call may change ...") and frame notes ("a frame address ...", "a local / a saved register's word ...
read after this call") of a token dump (a7_round2/corpus.sh's <tag>_lent.tsv), each with its function, its words and the line printed after it
(the call it is said at, with that line's address). usage: notes.py <dump.tsv> [function ...]   (default: every function with one)"""
import re, sys
want = set(a.upper() for a in sys.argv[2:])
fn, lines = None, []
def flush():
    if fn is None or (want and fn not in want): return
    text = [t for _, t in lines]
    i = 0
    while i < len(lines):
        a, t = lines[i]
        if '/*' in t:
            c, j = t, i
            while '*/' not in c and j + 1 < len(lines): j += 1; c += ' ' + lines[j][1].strip()
            c = re.sub(r'\s+', ' ', c.split('/*', 1)[1].split('*/', 1)[0]).strip()
            if c.startswith(('this call may change', 'a frame address', 'a local at or above', "a saved register's word")):
                k = j + 1
                while k < len(lines) and lines[k][0] == '00000000' and lines[k][1].strip() == '': k += 1
                nxt = lines[k] if k < len(lines) else ('-', '')
                print('%s\t%s\t%s\t%s' % (fn, c, nxt[0], nxt[1].strip()))
            i = j + 1
        else: i += 1
for l in open(sys.argv[1], encoding='utf-8', errors='replace'):
    t = l.rstrip('\n').split('\t')
    if t[0] == 'F': flush(); fn, lines = t[2], []
    elif t[0] == 'L': lines.append((t[2], t[3] if len(t) > 3 else ''))
flush()
