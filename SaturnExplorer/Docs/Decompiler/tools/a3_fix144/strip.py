import sys,re
for p in sys.argv[1:]:
    L=open(p).read().split('\n')
    out=[l for l in L if 'DBG76' not in l]
    open(p,'w').write('\n'.join(out))
