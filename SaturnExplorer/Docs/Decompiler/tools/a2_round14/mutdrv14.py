#!/usr/bin/env python3
# usage: mutdrv14.py <build dir> <SaturnExplorer dir> <work dir> <tag> [ids...]
# Area 2 round 14 review: applies each mutant of mutants14.py (the fixes of patch 0064 reverted one at a time) to a scratch copy of the engine source, compiles it,
# links SaturnExplorerSh2DecompilerEngineTests with the mutated object in place of the engine's, runs it under ulimit -v and timeout, and writes
# <work dir>/<tag>.<id>.result (CHECK-failed=0 is a mutant that lives). A finished mutant is skipped when the script is run again.
import os, re, subprocess, sys
B=os.path.abspath(sys.argv[1])
SE=os.path.abspath(sys.argv[2])
M=os.path.abspath(sys.argv[3])
os.makedirs(M,exist_ok=True)
CPP=SE+'/FrontEnd/third_party/ghidra-decompiler/cpp'
exec(open(os.path.join(os.path.dirname(os.path.abspath(__file__)),'mutants14.py')).read())   # MUT = { id: (file, [(old,new),...], note) }
pre="FrontEnd/third_party/ghidra-decompiler/CMakeFiles/SaturnExplorerGhidraDecomp.dir/cpp"
link=open(B+'/CMakeFiles/SaturnExplorerSh2DecompilerEngineTests.dir/link.txt').read().strip()
ids=sys.argv[5:] or sorted(MUT)
tag=sys.argv[4]
for i in ids:
    done=f'{M}/{tag}.{i}.result'
    if os.path.exists(done): continue
    fn,edits,note=MUT[i]
    src=open(f'{CPP}/{fn}').read()
    for old,new in edits:
        assert src.count(old)>=1,(i,old[:60])
        src=src.replace(old,new)
    open(f'{M}/{fn}','w').write(src)
    obj=f'{M}/{i}_{fn}.o'
    r=subprocess.run(['g++','-O1','-g0','-DNDEBUG','-std=gnu++14','-Wno-sign-compare','-Wno-unused-variable','-Wno-unused-function','-Wno-deprecated-declarations',f'-I{CPP}',f'-I{CPP}/../../zlib','-c',f'{M}/{fn}','-o',obj],capture_output=True,text=True)
    if r.returncode: open(done,'w').write('COMPILE ERROR '+r.stderr[:500]); continue
    exe=f'{M}/engtest_{i}'
    l=re.sub(r'"?'+re.escape(pre)+'/'+re.escape(fn)+r'\.o"?',obj,link)
    l=l.replace('-o bin/SaturnExplorerSh2DecompilerEngineTests','-o '+exe)
    r=subprocess.run(l,shell=True,cwd=B,capture_output=True,text=True)
    if r.returncode: open(done,'w').write('LINK ERROR '+r.stderr[:500]); continue
    r=subprocess.run(f'ulimit -v 4000000; timeout 600 {exe} {M}/scr_{i} 2>&1',shell=True,capture_output=True,text=True,executable='/bin/bash')
    out=r.stdout
    nfail=len(re.findall(r'CHECK failed',out))
    n14=len(re.findall(r'RuleDivOpt exhaustive:',out)); n13=len(re.findall(r'^RuleDivOpt: ',out,re.M))+len(re.findall(r'^RuleDivOpt control',out,re.M))
    small14 = 'RuleDivOpt exhaustive (1-byte numerand)' in out and re.search(r'(\d+) rewritten',out.split('RuleDivOpt exhaustive (1-byte numerand)')[1][:200])
    first=[l for l in out.splitlines() if re.match(r'Rule[A-Za-z0-9/ ]+:|RuleDiv',l) and 'by value' not in l and 'rule applications' not in l][:2]
    open(done,'w').write(f'{i}: {note}: rc={r.returncode} CHECK-failed={nfail} pin14-lines={n14} pin13-lines={n13} rewritten14={small14.group(1) if small14 else '?'} | '+' || '.join(first)[:300]+'\n')
    os.remove(exe); os.remove(obj)
print('done')
