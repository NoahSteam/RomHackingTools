#!/usr/bin/env python3
"""shrink15.py GENBIN SRC NAME : delta-debug a call-free fixture of SRC by dropping store groups while the printed-C differential still finds a mismatch."""
import sys, subprocess, os, re, tempfile, shutil
import asm
genbin, src, name = sys.argv[1:4]
WORK = os.path.join(tempfile.gettempdir(), 'shrink15_%d' % os.getpid())
blocks = open(src).read().split('\nname ')
for b in blocks:
    b = b[5:] if b.startswith('name ') else b
    if b.startswith(name + ' '):
        lines = b.splitlines(); break
i = [k for k, l in enumerate(lines) if l.startswith('asm ')][0]; j = lines.index('endasm')
body = lines[i+1:j]
# groups: split at the start of each address computation / reload: lines beginning a store sequence
hd = max(k for k, l in enumerate(body) if re.match(r'mov\.l r1,@\(\d+,r15\)$', l) and k < 12) + 1
starts = [k for k, l in enumerate(body) if k >= hd and (l == 'mov.l @(32,r15),r1' or l == 'mov r15,r1' or re.match(r'mov #-?\d+,r2$', l))]
# build groups: from one start to the next
groups = []; head = body[:starts[0]]
for a, b2 in zip(starts, starts[1:] + [len(body)]):
    groups.append(body[a:b2])
# the last group holds the final read and the epilogue: keep it
tail = groups.pop() if groups else []
def test(gs):
    w = ' '.join('%04X' % x for x in asm.assemble(head + [l for g in gs for l in g] + tail))
    env = dict(os.environ, SE_GENERATED_PRINTEDC='only', SE_GENERATED_PRINTEDC_WORDS=w)
    p = subprocess.run(['bash', '-c', 'ulimit -v 3500000; exec timeout 60 %s %s' % (genbin, WORK)], env=env, capture_output=True, text=True)
    shutil.rmtree(WORK, ignore_errors=True)
    return 'FINDING' in (p.stdout + p.stderr)
cur = groups[:]
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
for g in cur:
    pass
final = head + [l for g in cur for l in g] + tail
print('groups %d -> %d' % (len(groups), len(cur)))
print('\n'.join(final))
