#!/usr/bin/env python3
"""Area 7 round 13: the finding classes of each failing seed in generator logs.
usage: classes.py [-a] <log>...  -- prints `log grammar seed classes` for each seed with a class other than folded-store-after-access
(area 5's), or for every failing seed with -a."""
import re, sys
every = '-a' in sys.argv
for fn in [a for a in sys.argv[1:] if a != '-a']:
    seed, out = None, {}
    for line in open(fn, errors='replace'):
        m = re.match(r'FINDING seed (\d+) \(grammar (\d)\)', line)
        if m:
            seed = (m.group(1), m.group(2))
            out.setdefault(seed, set())
            continue
        if not line.startswith('  ') or line.startswith('  shrunk'):
            if not line.startswith('  '):
                seed = None
            continue
        m = re.match(r'\s+\[([a-z]:[a-z0-9:_-]+)\]', line)
        if m and seed:
            out[seed].add(m.group(1))
    for s, c in sorted(out.items()):
        if every or [x for x in c if 'folded-store-after-access' not in x]:
            print(fn.split('/')[-1], 'g' + s[1], s[0], ' '.join(sorted(c)))
