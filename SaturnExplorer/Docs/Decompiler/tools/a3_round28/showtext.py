#!/usr/bin/env python3
"""Area 3 round 28: print the text of one function from a corpus token dump (a7_round2/corpus.sh's <tag>_alone.tsv / _lent.tsv), each line
with its first address. usage: showtext.py <dump.tsv> <function hex>"""
import sys
want = int(sys.argv[2], 16)
on = False
for l in open(sys.argv[1], encoding='utf-8', errors='replace'):
    t = l.rstrip('\n').split('\t')
    if t[0] == 'F':
        if on: break
        on = int(t[2], 16) == want
        continue
    if on and t[0] == 'L':
        print('%-9s %s' % (t[2] if t[2] != '00000000' else '', t[3] if len(t) > 3 else ''))
