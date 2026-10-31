#!/usr/bin/env python3
"""shrink16.py GENBIN SRC NAME [kind] : delta-debug a call-free frame16 fixture of SRC by dropping statement groups (a group starts at an address
computation `mov r15,rN` / `mov.l @(d,r15),r1` / `mov #n,r2` / `mov rN,r0`) while the printed-C differential still reports `kind` (default mismatch).
Prints the reduced assembly. The head (up to the end of the chain set-up: the first `mov #0,r0`-free prefix of stores to slots) and the tail (from
`mov #0,r0`) are kept."""
import sys, subprocess, os, re, tempfile, shutil
import asm
genbin, src, name = sys.argv[1:4]
kind = sys.argv[4] if len(sys.argv) > 4 else 'mismatch'
keep = int(sys.argv[5]) if len(sys.argv) > 5 else 0      # the first `keep` groups (the fill of S) are never dropped
WORK = os.path.join(tempfile.gettempdir(), 'shrink16_%d' % os.getpid())
blocks = open(src).read().split('\nname ')
for b in blocks:
    b = b[5:] if b.startswith('name ') else b
    if b.startswith(name + ' '):
        lines = b.splitlines(); break
i = [k for k, l in enumerate(lines) if l.startswith('asm ')][0]; j = lines.index('endasm')
body = lines[i+1:j]
ti = body.index('mov #0,r0')
ei = next(k for k in range(ti, len(body)) if body[k] in ('add #64,r15', 'add #80,r15', 'mov r14,r15'))
tail = []; epi = body[ei:]; mid = body[:ei]    # the reads between `mov #0,r0` and the epilogue are dropped in groups too
# the head: through the last `mov.l rN,@(d,r15)` of the chain set-up (the stores of slot addresses come first)
hd = 0
for k, l in enumerate(mid):
    if re.match(r'mov\.l r1,@\((32|36|40|44|48|52),r15\)$', l): hd = k + 1
    elif k > hd + 6: break
head = mid[:hd]; rest = mid[hd:]
starts = [k for k, l in enumerate(rest) if not (k and re.match(r'mov #-?\d+,r2$', rest[k-1]) and not l.startswith('mov #')) and re.match(r'(mov r15,r\d+|mov\.l @\(\d+,r15\),r1|mov #-?\d+,r2|mov r\d,r0|mov #\d+,r3|mov #0,r0|LP?\d*:|L\d+:)$', l)]
groups = []
if not starts: starts = [0]
if starts[0] != 0: head = head + rest[:starts[0]]
for a, b2 in zip(starts, starts[1:] + [len(rest)]): groups.append(rest[a:b2])
def lines_of(gs): return head + [l for g in groups[:keep] for l in g] + [l for g in gs for l in g] + epi
def test(gs):
    try: w = ' '.join('%04X' % x for x in asm.assemble(lines_of(gs)))
    except Exception: return False
    env = dict(os.environ, SE_GENERATED_PRINTEDC='only', SE_GENERATED_PRINTEDC_WORDS=w)
    p = subprocess.run(['bash', '-c', 'ulimit -v 3500000; exec timeout 60 %s %s' % (genbin, WORK)], env=env, capture_output=True, text=True)
    shutil.rmtree(WORK, ignore_errors=True)
    out = p.stdout + p.stderr
    return 'FINDING' in out and kind in out
cur = groups[keep:]
assert test(cur), 'not failing'
n = 2
while len(cur) >= 2:
    chunk = max(1, len(cur) // n); changed = False
    for s in range(0, len(cur), chunk):
        cand = cur[:s] + cur[s+chunk:]
        if cand != cur and test(cand):
            cur = cand; changed = True; break
    if not changed:
        if chunk == 1: break
        n = min(len(cur), n * 2)
print('groups %d -> %d (+%d kept)' % (len(groups), len(cur), keep))
print('\n'.join(lines_of(cur)))
