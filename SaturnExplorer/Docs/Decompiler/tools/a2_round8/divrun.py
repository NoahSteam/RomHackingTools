#!/usr/bin/env python3
# Area 2 round 8 instrument: runs divgen over about 6,700 to 13,000 divisors in batches through the printed-C differential.
# usage: SE_BIN=<SaturnExplorerSh2GeneratedTests> divrun.py u|s <scale> <tag> [seed]
import subprocess, sys, os, random
sys.path.insert(0, os.path.dirname(__file__))
import divgen
S = os.environ.get("A2_SCRATCH", "/tmp")
def divisors(kind, rnd):
    ds = list(range(3, 4000))
    ds += [rnd.randrange(4000, 1 << 32) for _ in range(2500)]
    for k in range(3, 32):
        for e in (-2, -1, 1, 2, 3):
            ds.append((1 << k) + e)
    ds += [0xFFFFFFFF - i for i in range(0, 40)] + [0x7FFFFFFF - i for i in range(0, 20)] + [0x80000001 + i for i in range(0, 20)]
    ds += [641, 6700417, 0x12345, 1000000007, 100, 1000, 10000, 100000, 1000000, 10000000, 100000000, 1000000000]
    if kind == 's':
        ds = [d if d < 0x80000000 else d - (1 << 32) for d in ds]
        ds += [-d for d in ds if 0 < d < 0x7FFFFFFF]
    return sorted(set(ds))
def main():
    kind = sys.argv[1]; scale = int(sys.argv[2]); tag = sys.argv[3]; seed = int(sys.argv[4]) if len(sys.argv) > 4 else 1
    rnd = random.Random(seed)
    ds = divisors(kind, rnd)
    funcs = []
    for d in ds:
        if abs(d) < 2 or (abs(d) & (abs(d) - 1)) == 0: continue
        funcs.append((d, divgen.function(d, kind == 's', scale)))
    print(kind, scale, len(funcs), "functions")
    bad = 0
    for i in range(0, len(funcs), 600):
        part = funcs[i:i + 600]
        env = dict(os.environ, SE_GENERATED_PRINTEDC="only", SE_GENERATED_PRINTEDC_WORDS=";".join(w for _, w in part), SE_GENERATED_PRINTEDC_INPUTS="48")
        out = subprocess.run([os.environ["SE_BIN"], S + "/runDV" + tag], env=env, capture_output=True, text=True).stdout
        for line in out.splitlines():
            if line.startswith("FINDING"):
                bad += 1
                k = int(line.split("hand")[1].split()[0]) if "hand" in line else -1
                print(line[:200], "divisor", hex(part[k][0]) if 0 <= k < len(part) else "?")
            if line.startswith("printed-C:"): print(line[:300])
    print("findings", bad)
main()
