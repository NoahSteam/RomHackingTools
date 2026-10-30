#!/usr/bin/env python3
"""sub15.py base_dir head_dir hwram img : real-entry texts that still differ once parentheses and shift-width casts and notes are ignored."""
import sys, os, re
A, B, H, img = sys.argv[1:5]
d = open('%s/hwram%s.bin' % (H, img), 'rb').read()
def mid(addr):
    off = addr - 0x06000000 - 2
    if off < 0: return False
    w = (d[off] << 8) | d[off + 1]
    return (w & 0xF00F) == 0x2006 and ((w >> 8) & 0xF) == 15
def norm(t):
    t = re.sub(r'/\*.*?\*/', '', t, flags=re.S)
    out = []
    for l in t.splitlines():
        l = re.sub(r'\s*;\s*[0-9A-Fa-f]{8}\s*$', ';', l).rstrip()
        if l.strip() and not l.startswith('//'): out.append(re.sub(r'[()\s]', '', l))
    return sorted(out)
n = 0
for f in sorted(os.listdir(B)):
    if not f.endswith('.c'): continue
    a = open(os.path.join(A, f)).read(); b = open(os.path.join(B, f)).read()
    if a == b or mid(int(f[:8], 16)): continue
    if norm(a) != norm(b):
        n += 1; print(f[:8])
print('real entries differing beyond parentheses/notes:', n, file=sys.stderr)
