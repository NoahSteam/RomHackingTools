#!/usr/bin/env python3
"""mk.py in.src > out.spec : 'asm ADDR' ... 'endasm' blocks become 'code ADDR words'; the rest passes through."""
import sys, asm
out=[]; blk=None
for ln in open(sys.argv[1]).read().splitlines():
    s=ln.strip()
    if s.startswith('asm '):
        blk=[s.split()[1],[]]; continue
    if s=='endasm':
        w=asm.assemble(blk[1]); out.append('code %s %s'%(blk[0],' '.join('%04X'%x for x in w))); blk=None; continue
    if blk is not None: blk[1].append(ln); continue
    out.append(ln)
print('\n'.join(out))
