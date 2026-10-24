#!/usr/bin/env python3
# usage: mutrc14.py <build dir> <SaturnExplorer dir> <work dir> <rc object (mkrc14.sh: action.cc.o)> <ids...>
# Area 2 round 14: the in-situ instruments against the mutants of round 13 (mutants13.py, the guards of patch 0064 reverted one at a time): the mutated
# ruleaction.cc is compiled and linked, in place of the engine's, with the in-situ check (rulecheck14.hh, wholefn14.hh) into the generator; the families that
# build the shapes of the guard (56 lerp, 60 mulshift, 61 mulhsum, 21 udiv, 59 shdiv, 67 divterm, 22 sdiv) run through the printed-C differential. One line per
# mutant: the printed-C findings, the RULECHECK-BAD lines (a rule application that changed a value), the WHOLEFN-BAD lines (a rule application that changed the
# function's result).
import os, re, subprocess, sys
B = os.path.abspath(sys.argv[1]); SE = os.path.abspath(sys.argv[2]); M = os.path.abspath(sys.argv[3]); RCOBJ = os.path.abspath(sys.argv[4])
os.makedirs(M, exist_ok=True)
CPP = SE + '/FrontEnd/third_party/ghidra-decompiler/cpp'
here = os.path.dirname(os.path.abspath(__file__))
exec(open(os.path.join(here, '..', 'a2_round13', 'mutants13.py')).read())
ids = sys.argv[5:] or sorted(MUT)
FAMS = [(56, 'a2_round13/dagen13.py', 100), (60, 'a2_round13/dagen13.py', 100), (61, 'a2_round13/dagen13.py', 100), (21, 'a2_round11/dagen11.py', 100),
        (59, 'a2_round13/dagen13.py', 100), (67, 'a2_round14/dagen14.py', 100), (22, 'a2_round11/dagen11.py', 100)]
def sh(cmd, env=None):
    e = dict(os.environ); e.update(env or {})
    return subprocess.run(cmd, shell=True, capture_output=True, text=True, executable='/bin/bash', env=e)
for i in ids:
    done = f'{M}/{i}.done'
    if os.path.exists(done): continue
    fn, edits, note = MUT[i]
    src = open(f'{CPP}/{fn}').read()
    for old, new in edits:
        assert src.count(old) >= 1, (i, old[:60])
        src = src.replace(old, new)
    mf = f'{M}/{i}_{fn}'; open(mf, 'w').write(src)
    obj = f'{M}/{i}_{fn}.o'
    r = sh(f"g++ -O1 -g0 -DNDEBUG -std=gnu++14 -Wno-sign-compare -Wno-unused-variable -Wno-unused-function -Wno-deprecated-declarations -I{CPP} -c {mf} -o {obj}")
    if r.returncode:
        open(f'{M}/summary.txt', 'a').write(f'{i} COMPILE ERROR\n'); open(done, 'w').close(); continue
    exe = f'{M}/gen_{i}'
    r = sh(f"cd {here} && ./relink14.sh {B} SaturnExplorerSh2GeneratedTests {RCOBJ} {exe} {fn}.o={obj}")
    if r.returncode:
        open(f'{M}/summary.txt', 'a').write(f'{i} LINK ERROR {r.stderr[:200]}\n'); open(done, 'w').close(); continue
    tot = {'mismatch': 0, 'rcbad': 0, 'wfbad': 0}
    per = []
    for fam, script, n in FAMS:
        w = sh(f"python3 -I {here}/../{script} 2381001 {n} {fam}").stdout.strip()
        out = sh(f"ulimit -v 4000000; SE_RULECHECK=1 SE_WHOLEFN=1 SE_GENERATED_PRINTEDC=only SE_GENERATED_PRINTEDC_WORDS=\"{w}\" timeout 900 {exe} {M}/scr_{i} 2>&1").stdout
        mm = len(re.findall(r'FINDING \[mismatch\]', out)); rb = len(re.findall(r'RULECHECK-BAD', out)); wb = len(re.findall(r'WHOLEFN-BAD', out))
        tot['mismatch'] += mm; tot['rcbad'] += rb; tot['wfbad'] += wb
        per.append(f'{fam}:{mm}/{rb}/{wb}')
    open(f'{M}/summary.txt', 'a').write(f"{i} {note}: printed-C mismatches {tot['mismatch']}, RULECHECK-BAD {tot['rcbad']}, WHOLEFN-BAD {tot['wfbad']} (family: mismatch/rc/wf {' '.join(per)})\n")
    for p in (obj, mf, exe):
        try: os.remove(p)
        except OSError: pass
    open(done, 'w').close()
print('done')
