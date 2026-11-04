import sys
sys.path.insert(0, '.')
import asm
src=open(sys.argv[1]).read().split('\nname ')
for b in src:
    lines=b.splitlines()
    if not lines: continue
    name=lines[0].replace('name ','').strip()
    i=next(k for k,l in enumerate(lines) if l.startswith('asm '))
    j=lines.index('endasm')
    w=asm.assemble(lines[i+1:j])
    args=[l for l in lines if l.startswith('args')]
    print(name,'|',' '.join('%04X'%x for x in w),'|',args[:1])
