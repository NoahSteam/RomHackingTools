#!/usr/bin/env python3
"""Area 3, A3-214 fix: the cautions ("this call may change ...") of a token dump (a7_round2/corpus.sh's <tag>_alone.tsv / _lent.tsv): how many,
how many of them in A3-214's table wording ("jumps through a table the analysis cannot bound"), and in how many function texts.
usage: counts.py <.tsv> ..."""
import re, sys
for p in sys.argv[1:]:
    texts, cur = {}, None
    for l in open(p, encoding='utf-8', errors='replace'):
        t = l.rstrip('\n').split('\t')
        if t[0] == 'F': cur = t[2]; texts[cur] = []
        elif t[0] == 'L' and cur: texts[cur].append(t[3] if len(t) > 3 else '')
    n = table = fns = 0
    for k, lines in texts.items():
        flat = re.sub(r'\s+', ' ', ' '.join(lines))
        c = re.findall(r'/\* this call may change .*?\*/', flat)
        n += len(c); table += sum('jumps through a table the analysis cannot bound' in x for x in c); fns += bool(c)
    print('%s: %d caution(s) in %d text(s), %d in the table wording' % (p.split('/')[-1], n, fns, table))
