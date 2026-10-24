#!/usr/bin/env python3
# usage: mutkill14.py <build dir> <SaturnExplorer dir> <work dir> <scratch for the tools> [ids...]
# Area 2 round 14: the mutants of round 13 (mutants13.py: each guard of patch 0064 reverted alone) against the round 14 instruments instead of the engine pins.
# Per mutant: the mutated ruleaction.cc / subflow.cc is compiled (-O1) and linked in place of the engine's into divexh14, edge14 and rulefuzz14; then
#   divexh14 opt   xs=1 (constants 0..1999), xs=2 (constants 0..2999)     -> wrong configurations
#   divexh14 term  xs=1 (0..1999), xs=2 (0..1999)                         -> wrong configurations
#   edge14 threeway (seeds 1..200000), divchain1 (the whole grid), divchain2 (2000 pairs), signform1 (all multipliers of 2 bytes)  -> wrong graphs
#   rulefuzz14 with RF_JUDGEWRAP=1 RF_CONSUME=1, 30,000 graphs            -> wrong applications
# One line per mutant in <work dir>/summary.txt; a finished mutant is skipped when the script runs again.
import os, re, subprocess, sys
B = os.path.abspath(sys.argv[1]); SE = os.path.abspath(sys.argv[2]); M = os.path.abspath(sys.argv[3]); S = os.path.abspath(sys.argv[4])
os.makedirs(M, exist_ok=True)
CPP = SE + '/FrontEnd/third_party/ghidra-decompiler/cpp'
here = os.path.dirname(os.path.abspath(__file__))
exec(open(os.path.join(here, '..', 'a2_round13', 'mutants13.py')).read())     # MUT = { id: (file, [(old, new), ...], note) }
ids = sys.argv[5:] or sorted(MUT)
OBJ = B + '/FrontEnd/third_party/ghidra-decompiler/CMakeFiles/SaturnExplorerGhidraDecomp.dir/cpp'
def sh(cmd, env=None):
    e = dict(os.environ); e.update(env or {})
    return subprocess.run(cmd, shell=True, capture_output=True, text=True, executable='/bin/bash', env=e)
def wrong(out, pat):
    m = re.search(pat, out)
    return int(m.group(1)) if m else -1
for i in ids:
    done = f'{M}/{i}.done'
    if os.path.exists(done): continue
    fn, edits, note = MUT[i]
    src = open(f'{CPP}/{fn}').read()
    for old, new in edits:
        assert src.count(old) >= 1, (i, old[:60])
        src = src.replace(old, new)
    mf = f'{M}/{i}_{fn}'
    open(mf, 'w').write(src)
    obj = f'{M}/{i}_{fn}.o'
    r = sh(f"g++ -O1 -g0 -DNDEBUG -std=gnu++14 -Wno-sign-compare -Wno-unused-variable -Wno-unused-function -Wno-deprecated-declarations -I{CPP} -c {mf} -o {obj}")
    if r.returncode:
        open(f'{M}/summary.txt', 'a').write(f'{i} COMPILE ERROR\n'); open(done, 'w').close(); continue
    env = {'RULEACTION_OBJ': obj} if fn == 'ruleaction.cc' else {}
    if fn != 'ruleaction.cc':
        # a mutated subflow.cc: replace that object instead
        env = {}
    res = {}
    for tool in ('divexh14', 'edge14', 'rulefuzz14'):
        exe = f'{M}/{tool}_{i}'
        if fn == 'ruleaction.cc':
            r = sh(f"cd {here} && RULEACTION_OBJ={obj} ./build14.sh {tool}.cpp {exe} {B} {SE}")
        else:
            objs = subprocess.run(f"ls {OBJ}/*.o | grep -v '/{fn}.o$'", shell=True, capture_output=True, text=True).stdout.split()
            gen = subprocess.run(f"find {B} -name Sh2SpecData.h -printf '%h\\n' | head -1", shell=True, capture_output=True, text=True).stdout.strip()
            r = sh(f"g++ -O2 -std=gnu++14 -Wno-format -I{SE}/FrontEnd/src -I{CPP} -I{SE}/FrontEnd/third_party/zlib -I{gen} -I{gen}/.. {here}/{tool}.cpp {SE}/FrontEnd/src/Decompiler/SpecBundle.cpp {SE}/FrontEnd/src/Sha256.cpp {SE}/FrontEnd/src/FileWrite.cpp {' '.join(objs)} {obj} {B}/lib/libSaturnExplorerZlib.a -lpthread -o {exe}")
        res[tool] = exe if r.returncode == 0 else None
    runs = []
    def run(label, cmd, pat):
        out = sh(f"ulimit -v 4000000; timeout 900 {cmd}").stdout
        runs.append(f'{label}={wrong(out, pat)}')
    d = res['divexh14']; e = res['edge14']; f = res['rulefuzz14']
    if d:
        run('opt1', f'{d} {S}/m opt 1 0 2000 {M}/x.bin', r'(\d+) wrong')
        run('opt2', f'{d} {S}/m opt 2 0 3000 {M}/x.bin', r'(\d+) wrong')
        run('term1', f'{d} {S}/m term 1 0 2000 {M}/x.bin', r'(\d+) wrong')
        run('term2', f'{d} {S}/m term 2 0 2000 {M}/x.bin', r'(\d+) wrong')
    if e:
        run('threeway', f'{e} {S}/m threeway 1 200000 {M}/x.bin', r'(\d+) wrong')
        run('divchain1', f'{e} {S}/m divchain1 0 196608 {M}/x.bin', r'(\d+) wrong')
        run('divchain2', f'{e} {S}/m divchain2 0 20000 {M}/x.bin', r'(\d+) wrong')
        run('signform1', f'{e} {S}/m signform1 0 65536 {M}/x.bin', r'(\d+) wrong')
    if f:
        run('rulefuzz', f'RF_JUDGEWRAP=1 RF_CONSUME=1 RF_SHOW=0 {f} {S}/m 5000001 30000 3', r'graphs, (\d+) wrong')
    open(f'{M}/summary.txt', 'a').write(f'{i} {note}: ' + ' '.join(runs) + '\n')
    for p in [obj, mf] + [x for x in res.values() if x]:
        try: os.remove(p)
        except OSError: pass
    open(done, 'w').close()
print('done')
