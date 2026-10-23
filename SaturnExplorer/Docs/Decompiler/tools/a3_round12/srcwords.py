#!/usr/bin/env python3
"""srcwords.py file.src 'name prefix' : the words of every asm block of the fixture whose name starts with the prefix, as CallFixture literals."""
import sys, os
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import asm
def blocks(path):
    cur=None; blk=None; out=[]
    for ln in open(path).read().splitlines():
        s=ln.strip()
        if s.startswith('name '): cur={'name':s[5:],'asm':[],'lend':''}; out.append(cur)
        elif s.startswith('asm '): blk=[s.split()[1],[]]
        elif s=='endasm': cur['asm'].append((blk[0],asm.assemble(blk[1]))); blk=None
        elif blk is not None: blk[1].append(ln)
        elif s.startswith('lend '): cur['lend']=s[5:]
    return out
if __name__=='__main__':
    for b in blocks(sys.argv[1]):
        if b['name'].startswith(sys.argv[2]):
            print('#',b['name'],'lend',b['lend'])
            for a,w in b['asm']: print('  0x%s { %s }'%(a,', '.join('0x%04X'%x for x in w)))
