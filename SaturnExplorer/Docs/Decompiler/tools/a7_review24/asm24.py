#!/usr/bin/env python3
# Area 7 review round 24: assemble the hand fixtures of fixtures24.s (blocks separated by a line "==", each headed
# "name|base[|VOLATILE=..;POLL=..;ALIAS=..]") with round 5's tiny assembler and print, per block,
#   name<TAB>words<TAB>extra env
# usage: asm24.py fixtures24.s [name-substring]
import os, sys
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'a7_round5'))
from sh2asm import assemble  # noqa: E402

def main():
    want = sys.argv[2] if len(sys.argv) > 2 else None
    for src in open(sys.argv[1]).read().split('\n==\n'):
        L = [x for x in src.strip().split('\n') if x.strip()]
        if not L or L[0].startswith('#'):
            L = [x for x in L if not x.startswith('#')]
            if not L:
                continue
        f = L[0].split('|')
        name, base = f[0], int(f[1], 16)
        extra = f[2] if len(f) > 2 else ''
        if want and want not in name:
            continue
        o = assemble(L[1:], base)
        ws, cur = [], base
        for a in sorted(o):
            while cur < a:
                ws.append('0009'); cur += 2
            ws.append('%04X' % (o[a] & 0xFFFF)); cur = a + 2
        print('%s\t%s\t%s' % (name, ' '.join(ws), extra))

main()
