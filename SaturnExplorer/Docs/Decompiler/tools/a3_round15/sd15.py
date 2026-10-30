#!/usr/bin/env python3
"""sd15.py base_dir head_dir addr... : statement-level diff of the lend_after texts, address comments dropped."""
import sys, os, re, difflib
A, B = sys.argv[1:3]
def norm(t):
    t = re.sub(r'/\*.*?\*/', '', t, flags=re.S)
    out = []
    for l in t.splitlines():
        l = re.sub(r'\s*;\s*[0-9A-Fa-f]{8}\s*$', ';', l).rstrip()
        if l.strip() and not l.startswith('//'): out.append(l)
    return out
for a in sys.argv[3:]:
    f = a.upper() + '.c'
    x = norm(open(os.path.join(A, f)).read()); y = norm(open(os.path.join(B, f)).read())
    print('=====', a.upper())
    for l in difflib.unified_diff(x, y, lineterm='', n=0):
        if l.startswith(('---', '+++', '@@')): continue
        print(l[:150])
