#!/usr/bin/env python3
# usage: reduce14.py <undecompiled|loop> "<hex words of the function>"   (SE_GENERATED_TESTS=<build>/bin/SaturnExplorerSh2GeneratedTests, a scratch dir in SE_REDUCE_DIR)
# Area 2 round 14: reduces a failing generated function by deleting one instruction (or one delayed branch with its slot) at a time while the failure stays: branch
# displacements are rebuilt, a branch whose target is deleted goes to the next instruction. Predicates: `undecompiled` (the engine fails the function, A2-93)
# and `loop` (the printed text has an empty `do { } while`, the compiled text does not end, A2-97; a backward branch is kept only after a `dt`).
import os, re, shutil, subprocess, sys
BIN = os.environ.get('SE_GENERATED_TESTS', 'build/bin/SaturnExplorerSh2GeneratedTests')
WORK = os.environ.get('SE_REDUCE_DIR', '/tmp/reduce14')
def s8(v): return v-256 if v&0x80 else v
def s12(v): return v-4096 if v&0x800 else v
def parse(ws):
    units=[]; i=0
    while i<len(ws):
        v=ws[i]
        if v&0xF000==0xA000 or v==0x000B:
            units.append([ws[i],ws[i+1]]); i+=2
        else:
            units.append([v]); i+=1
    addr=[]; a=0
    for u in units: addr.append(a); a+=2*len(u)
    tgt=[None]*len(units)
    for k,u in enumerate(units):
        v=u[0]
        if v&0xFF00 in (0x8B00,0x8900): t=addr[k]+4+2*s8(v&0xFF)
        elif v&0xF000==0xA000: t=addr[k]+4+2*s12(v&0xFFF)
        else: continue
        tgt[k]=addr.index(t) if t in addr else None
    return units,tgt
def build(units,tgt):
    addr=[]; a=0
    for u in units: addr.append(a); a+=2*len(u)
    out=[]
    for k,u in enumerate(units):
        v=u[0]
        if tgt[k] is not None or v&0xFF00 in (0x8B00,0x8900) or v&0xF000==0xA000:
            t=addr[tgt[k]] if tgt[k] < len(units) else a
            d=(t-(addr[k]+4))//2
            if v&0xF000==0xA000:
                if not -2048<=d<2048: return None
                out+= [0xA000|(d&0xFFF)]+u[1:]
            else:
                if not -128<=d<128: return None
                out+= [(v&0xFF00)|(d&0xFF)]
        else: out+=u
    return out

def bad(ws):
    if ws is None: return False
    W = " ".join("%04X" % x for x in ws)
    shutil.rmtree(WORK, ignore_errors=True); os.makedirs(WORK)
    r = subprocess.run(['bash', '-c', 'ulimit -v 4000000; ulimit -t 5; SE_GENERATED_KEEP=1 SE_GENERATED_PRINTEDC=only SE_GENERATED_PRINTEDC_WORDS="%s" timeout 60 %s %s 2>&1' % (W, BIN, WORK)], capture_output=True, text=True)
    if MODE == 'undecompiled': return 'undecompiled' in r.stdout
    try: c = open(WORK + '/printedc_0_all.c').read()
    except Exception: return False
    return re.search(r'do \{\s*\} while', c) is not None
def okfl(us, tg):
    if MODE != 'loop': return True
    for k, t in enumerate(tg):
        if t is not None and t <= k:
            if not (t < k and k > 0 and (us[k-1][0] & 0xF0FF) == 0x4010): return False
    return True
MODE = sys.argv[1]
ws = [int(x, 16) for x in sys.argv[2].split()]
units, tgt = parse(ws)
assert bad(build(units, tgt)), 'does not reproduce'
changed = True
while changed:
    changed = False
    for k in range(len(units) - 1):
        nu = units[:k] + units[k+1:]
        nt = []
        for j, t in enumerate(tgt):
            if j == k: continue
            if t is None: nt.append(None); continue
            nt.append(t if t < k else (t - 1 if t > k else k))
        if okfl(nu, nt) and bad(build(nu, nt)):
            units, tgt = nu, nt; changed = True; break
print(" ".join("%04X" % x for x in build(units, tgt)))
