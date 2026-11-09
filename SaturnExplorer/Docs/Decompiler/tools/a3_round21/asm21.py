#!/usr/bin/env python3
"""Area 3 round 21: ../a3_round16/asm.py with two additions for jump tables, resolved before it runs.
  .addr LABEL           two words: the label's address (base 0x06009000, or BASE=0x... in the environment)
  mova @(LABEL,pc),r0   mova with the displacement to an aligned label
"""
import os, re, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'a3_round16'))
import asm

base = int(os.environ.get('BASE', '0x06009000'), 0)
src = [ln.split(';')[0].strip() for ln in sys.stdin.read().splitlines()]
src = [ln for ln in src if ln]
lines = []
for ln in src:
    m = re.fullmatch(r'\.addr\s+(\w+)', ln)
    lines += [('addrhi', m.group(1)), ('addrlo', m.group(1))] if m else [ln]
labels, n = {}, 0
for ln in lines:
    if isinstance(ln, str) and ln.endswith(':'): labels[ln[:-1]] = base + 2 * n
    else: n += 1
out, n = [], 0
for ln in lines:
    if isinstance(ln, tuple):
        a = labels[ln[1]]
        out.append('.word %d' % ((a >> 16) if ln[0] == 'addrhi' else (a & 0xFFFF))); n += 1; continue
    if ln.endswith(':'): out.append(ln); continue
    m = re.fullmatch(r'mova\s+@\(([A-Za-z_]\w*),pc\),\s*r0', ln)
    if m:
        pc = base + 2 * n
        d = (labels[m.group(1)] - ((pc & ~3) + 4)) // 4
        assert 0 <= d < 256 and labels[m.group(1)] % 4 == 0, ln
        ln = 'mova @(%d,pc),r0' % (4 * d)
    out.append(ln); n += 1
print(' '.join('%04X' % w for w in asm.assemble(out)))
