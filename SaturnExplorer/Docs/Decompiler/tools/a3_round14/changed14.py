#!/usr/bin/env python3
"""changed14.py img(007|008) base_lend_after new_lend_after hwram_dir : the REAL entries (the word before is not a push of r15) whose text changed between two corpus
runs, with the kinds of change (textdiff.py), one per line; the same for the notes-only ones with -n."""
import sys, os
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'a3_round13'))
from textdiff import kinds
img, A, B, H = sys.argv[1:5]
d = open('%s/hwram%s.bin' % (H, img), 'rb').read()
def mid(addr):
    off = addr - 0x06000000 - 2
    if off < 0: return False
    w = (d[off] << 8) | d[off + 1]
    return (w & 0xF00F) == 0x2006 and ((w >> 8) & 0xF) == 15
real = notes_only = other = 0
for f in sorted(os.listdir(B)):
    if not f.endswith('.c'): continue
    a = open(os.path.join(A, f)).read(); b = open(os.path.join(B, f)).read()
    if a == b or mid(int(f[:8], 16)): continue
    ks = kinds(a, b) or {'address-only'}
    if ks == {'notes', 'only-notes'}:
        notes_only += 1
        if '-n' in sys.argv: print(f[:8], 'notes')
    else:
        other += 1
        print(f[:8], ','.join(sorted(ks)))
print('real entries changed: %d beyond notes, %d by notes only' % (other, notes_only))
