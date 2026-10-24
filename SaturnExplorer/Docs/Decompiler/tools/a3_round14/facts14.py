#!/usr/bin/env python3
"""facts14.py img base_lend_after new_lend_after hwram_dir addr... : for real entries, what changed in the text and the mechanical checks of the reading:
the multiset of calls (equal or not), the returned expressions, the new arrays (declared only or used), the notes gained and lost."""
import sys, os, re, collections
img, A, B, H = sys.argv[1:5]
def calls(t): return collections.Counter(re.findall(r'(func_0x[0-9a-f]+|\(\*[A-Za-z_0-9]+\))\(', t))
def rets(t): return [re.sub(r'\s*;.*', '', l.strip()) for l in t.splitlines() if re.match(r'\s+return\b', l)]
def notes(t): return [re.sub(r'\s+', ' ', n) for n in re.findall(r'/\*.*?\*/', t, re.S)]
for a in sys.argv[5:]:
    f = a.upper() + '.c'
    x = open(os.path.join(A, f)).read(); y = open(os.path.join(B, f)).read()
    cx, cy = calls(x), calls(y)
    arrs = re.findall(r'\n\s+\w[\w ]*\s\**(\w+) \[(\d+)\];', y)
    newarr = [(n, s) for n, s in arrs if not re.search(r'\b%s\b' % n, x)]
    used = [(n, s, len(re.findall(r'\b%s\b' % n, y.split('{', 1)[1])) - 1) for n, s in newarr]
    nx, ny = collections.Counter(notes(x)), collections.Counter(notes(y))
    gain = ny - nx; lost = nx - ny
    print(a.upper(), 'calls', 'EQUAL' if cx == cy else 'DIFFER %s' % ((cx - cy) + (cy - cx)), '| returns', rets(x), '->', rets(y), '| new arrays (name,size,uses beyond decl)', used)
    for k, v in gain.items(): print('    +%d %s' % (v, k[3:90] if k.startswith('/* ') else k[:90]))
    for k, v in lost.items(): print('    -%d %s' % (v, k[3:90]))
