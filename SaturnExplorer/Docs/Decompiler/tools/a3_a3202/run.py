#!/usr/bin/env python3
"""Area 3, A3-202 (round 26's run26.py, unchanged but for this line and the scratch prefix): assemble a fixture of fixtures.src (functions separated by `fn` lines, each padded to 0x20) and run it through the
generator's SE_GENERATED_WORDS mode. usage: W=<SaturnExplorer dir> S=<scratch> run25.py <name> [KEY=VAL ...]
`name N` starts a fixture; `ret a,b,c` sets SE_GENERATED_RETURNS (default 0 per function); `fn` starts the next function; lines as asm22.py's."""
import os, re, subprocess, sys, tempfile
here = os.path.dirname(os.path.abspath(__file__))
W, S = os.environ['W'], os.environ['S']
name = sys.argv[1]; extra = dict(a.split('=', 1) for a in sys.argv[2:])
fns, ret, on = [], None, False
for ln in open(os.path.join(here, 'fixtures.src')).read().splitlines():
    t = ln.split()
    if t and t[0] == 'name': on = (t[1] == name); fns = [[]] if on else fns; continue
    if not on or not t or t[0].startswith('#'): continue
    if t[0] == 'ret': ret = t[1]; continue
    if t[0] == 'fn': fns.append([]); continue
    fns[-1].append(ln.strip())
if not fns: sys.exit('no fixture ' + name)
text, spans, n = [], [], 0
for f in fns:
    cnt = sum(1 for l in f if not l.endswith(':'))
    pad = (-cnt) % 16
    spans.append((n * 2, (n + cnt) * 2)); text += f + ['nop'] * pad; n += cnt + pad
lab, k = {}, 0
for l in text:
    if l.endswith(':'): lab[l[:-1]] = 0x06009000 + 2 * k
    else: k += 1
text = [re.sub(r'=@(\w+)-@(\w+)', lambda m: '=0x%08X' % ((lab[m.group(1)] - lab[m.group(2)]) & 0xFFFFFFFF), l) for l in text]
text = [re.sub(r'=@(\w+)', lambda m: '=0x%08X' % lab[m.group(1)], l) for l in text]
env = dict(os.environ)
words = subprocess.run([sys.executable, '-I', os.path.join(here, '..', 'a3_round22', 'asm22.py')], input='\n'.join(text), capture_output=True, text=True)
if words.returncode: sys.exit(words.stderr)
env['SE_GENERATED_WORDS'] = words.stdout.strip()
env['SE_GENERATED_FUNCS'] = ','.join('%x/%x/%x' % (a, a, b) for a, b in spans)
env['SE_GENERATED_RETURNS'] = ret or ','.join('0' for _ in fns)
env.update(extra)
print(name, 'FUNCS=' + env['SE_GENERATED_FUNCS'], 'RETURNS=' + env['SE_GENERATED_RETURNS'], 'WORDS="' + env['SE_GENERATED_WORDS'] + '"')
d = tempfile.mkdtemp(prefix='a3202.', dir=S)
r = subprocess.run('ulimit -v 3500000; timeout 300 %s/build/bin/SaturnExplorerSh2GeneratedTests %s/x 2>&1' % (W, d), shell=True, env=env, capture_output=True, text=True, executable='/bin/bash').stdout
subprocess.run(['rm', '-rf', d])
out = r[r.find('default request'):] if 'default request' in r else r
keep = os.environ.get('FULL')
for l in out.splitlines():
    if keep or not (l.startswith('  warning') or l.startswith('  fates') or l.startswith('  [n:generator') or l.startswith('CHECK') or 'ms total' in l or l.startswith('  note')) :
        print(l)
    elif l.startswith('  note'): print(l[:230])
