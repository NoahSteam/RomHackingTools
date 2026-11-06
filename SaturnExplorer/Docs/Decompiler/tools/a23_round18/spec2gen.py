#!/usr/bin/env python3
"""spec2gen.py file.spec [name-substring] -> per fixture: 'NAME\tWORDS\tFUNCS' (functions ordered with the entry first? no: by address)"""
import sys
blocks = open(sys.argv[1]).read().split('name ')[1:]
for b in blocks:
    name = b.splitlines()[0].strip()
    if len(sys.argv) > 2 and sys.argv[2] not in name: continue
    codes = [l.split()[1:] for l in b.splitlines() if l.startswith('code ')]
    codes = [(int(c[0], 16), c[1:]) for c in codes]
    base = min(a for a, _ in codes)
    end = max(a + 2 * len(w) for a, w in codes)
    words = ['0009'] * ((end - base) // 2)
    for a, w in codes:
        for i, x in enumerate(w): words[(a - base) // 2 + i] = x
    codes.sort()
    funcs = []
    for i, (a, w) in enumerate(codes):
        e = codes[i + 1][0] if i + 1 < len(codes) else end
        funcs.append('%X/%X/%X' % (a - base, a - base, e - base))
    print('%s\t%s\t%s' % (name, ' '.join(words), ','.join(funcs)))
