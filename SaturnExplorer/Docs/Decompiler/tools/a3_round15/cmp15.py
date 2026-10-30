#!/usr/bin/env python3
"""cmp15.py img base_dir head_dir hwram_dir : changed real entries by kind, and every signature or return-size change (all entries)."""
import sys, os, re, collections
T = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'a3_round13')
sys.path.insert(0, T)
from textdiff import kinds
img, A, B, H = sys.argv[1:5]
d = open('%s/hwram%s.bin' % (H, img), 'rb').read()
def mid(addr):
    off = addr - 0x06000000 - 2
    if off < 0: return False
    w = (d[off] << 8) | d[off + 1]
    return (w & 0xF00F) == 0x2006 and ((w >> 8) & 0xF) == 15
def sig(t):
    for l in t.splitlines():
        if re.match(r'\w.*\(.*\)\s*$', l) and not l.startswith('//'): return l.strip()
    return ''
def rets(t): return [re.sub(r'\s*;.*', '', l.strip()) for l in t.splitlines() if re.match(r'\s+return\b', l)]
cnt = collections.Counter(); real = []; sigch = []
for f in sorted(os.listdir(B)):
    if not f.endswith('.c'): continue
    a = open(os.path.join(A, f)).read(); b = open(os.path.join(B, f)).read()
    if a == b: continue
    ks = kinds(a, b) or {'address-only'}
    isreal = not mid(int(f[:8], 16))
    cnt['changed all'] += 1
    if isreal:
        cnt['changed real'] += 1
        if ks != {'notes', 'only-notes'}: real.append((f[:8], ','.join(sorted(ks))))
    sa, sb = sig(a), sig(b)
    if sa != sb or [re.sub(r'\W+', '', r) for r in rets(a)] != [re.sub(r'\W+', '', r) for r in rets(b)]:
        sigch.append((f[:8], isreal, sa, sb, rets(a), rets(b)))
print(dict(cnt), 'real beyond notes', len(real))
for r in real: print('REAL', *r)
print('--- sig or return changes:', len(sigch))
for s in sigch: print('SIG', s[0], 'real' if s[1] else 'frag', '|', s[2][:100], '=>', s[3][:100], '|', s[4][:2], '=>', s[5][:2])
