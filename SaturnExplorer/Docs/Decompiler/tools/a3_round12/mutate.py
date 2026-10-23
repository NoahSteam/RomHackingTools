#!/usr/bin/env python3
"""mutate.py <source file> <out file> <old>===<new> ... : copy a source file with exact, unique text replacements (a mutation for the instrument checks).
Fails when an <old> does not occur exactly once, so a mutation cannot silently miss. '\\n' and '\\t' in the arguments are newline and tab."""
import sys
src=open(sys.argv[1]).read()
for m in sys.argv[3:]:
    old,new=m.split('===',1)
    old=old.replace('\\n','\n').replace('\\t','\t'); new=new.replace('\\n','\n').replace('\\t','\t')
    n=src.count(old)
    if n!=1:
        sys.exit('mutation text occurs %d times: %r'%(n,old[:80]))
    src=src.replace(old,new)
open(sys.argv[2],'w').write(src)
