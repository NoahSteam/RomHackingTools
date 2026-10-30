#!/usr/bin/env python3
"""f15pc.py GENBIN SRC OUTDIR [max] : run each call-free fixture of SRC through the generator's printed-C differential
(the text compiled and run beside the code). Prints FINDING lines with the fixture name. Resumable per OUTDIR/<tag>.done."""
import sys, subprocess, os, re
import asm
genbin, src, outdir = sys.argv[1:4]
mx = int(sys.argv[4]) if len(sys.argv) > 4 else 10**9
os.makedirs(outdir, exist_ok=True)
blocks = open(src).read().split('\nname ')
blocks[0] = blocks[0][5:] if blocks[0].startswith('name ') else blocks[0]
done = 0; nfind = 0; skipped = 0
for b in blocks:
    lines = b.splitlines()
    if not lines: continue
    name = lines[0].strip()
    if any(l.startswith('lend') or l.startswith('unknown') for l in lines) or b.count('asm ') != 1:
        skipped += 1; continue
    i = lines.index(next(l for l in lines if l.startswith('asm ')))
    j = lines.index('endasm')
    words = asm.assemble(lines[i+1:j])
    if any(l.strip().startswith('jsr') for l in lines[i+1:j]): skipped += 1; continue
    w = ' '.join('%04X' % x for x in words)
    env = dict(os.environ, SE_GENERATED_PRINTEDC='only', SE_GENERATED_PRINTEDC_WORDS=w)
    p = subprocess.run(['bash', '-c', 'ulimit -v 3500000; exec timeout 60 %s %s/w' % (genbin, outdir)], env=env, capture_output=True, text=True)
    out = p.stdout + p.stderr
    done += 1
    if 'FINDING' in out:
        nfind += 1
        m = re.search(r'FINDING \[(\w[\w-]*)\]', out)
        d = re.search(r'detail: (.*)', out)
        print('%s | %s | %s | words: %s' % (name, m.group(1) if m else '?', (d.group(1)[:150] if d else ''), w))
    subprocess.run(['rm', '-rf', outdir + '/w'])
    if done >= mx: break
print('functions %d, findings %d, skipped (calls) %d' % (done, nfind, skipped), file=sys.stderr)
