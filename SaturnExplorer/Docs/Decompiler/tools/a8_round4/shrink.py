#!/usr/bin/env python3
# Area 8, round 4: shrink a failing Follow PC script (SE_TRUST_FOLLOW_SCRIPT) by deleting one operation at a time while the output still
# holds <text>.   shrink.py <trust test binary> <scratch dir> "<script>" "<text>" [VAR=value ...]
import os, subprocess, sys
binp, scratch, script, want = sys.argv[1:5]
extra = dict(x.split('=', 1) for x in sys.argv[5:])
ops = [w.strip() for w in script.split(';') if w.strip()]
def fails(o):
    env = dict(os.environ); env.update(extra); env['SE_TRUST_FOLLOW_SCRIPT'] = '; '.join(o)
    r = subprocess.run(['bash', '-c', 'ulimit -v 3500000; timeout 120 "$0" "$1"', binp, scratch], env=env, capture_output=True, text=True)
    return want in r.stdout
if not fails(ops): sys.exit('the script does not fail')
changed = True
while changed:
    changed = False
    i = 0
    while i < len(ops):
        t = ops[:i] + ops[i + 1:]
        if t and fails(t): ops = t; changed = True
        else: i += 1
print('; '.join(ops))
