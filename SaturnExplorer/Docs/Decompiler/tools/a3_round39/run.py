#!/usr/bin/env python3
"""Area 3 round 39 review (A3-210's run.py, unchanged but for this line and the scratch prefix): assemble a fixture of fixtures.src (functions separated by `fn` lines, each padded to 0x20) and run it through the
generator's SE_GENERATED_WORDS mode. usage: W=<SaturnExplorer dir> S=<scratch> run25.py <name> [KEY=VAL ...]
`name N` starts a fixture; `ret a,b,c` sets SE_GENERATED_RETURNS (default 0 per function); `fn` starts the next function; `env K=V ...` sets the fixture's environment (SE_GENERATED_CLOBBERS); lines as asm22.py's."""
import os, re, subprocess, sys, tempfile
here = os.path.dirname(os.path.abspath(__file__))
W, S = os.environ['W'], os.environ['S']
name = sys.argv[1]; extra = dict(a.split('=', 1) for a in sys.argv[2:])
fns, ret, on, fenv = [], None, False, {}
for ln in open(os.path.join(here, 'fixtures.src')).read().splitlines():
    t = ln.split()
    if t and t[0] == 'name': on = (t[1] == name); fns = [[]] if on else fns; continue
    if not on or not t or t[0].startswith('#'): continue
    if t[0] == 'ret': ret = t[1]; continue
    if t[0] == 'env': fenv.update(a.split('=', 1) for a in t[1:]); continue
    if t[0] == 'fn': fns.append([]); continue
    fns[-1].append(ln.strip())
if not fns: sys.exit('no fixture ' + name)
text, spans, n = [], [], 0
for f in fns:
    cnt = sum(2 if l.startswith('.addr') else 1 for l in f if not l.endswith(':'))
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
if words.returncode: print(words.stderr.strip()); print('RUN FAILED: %s did not assemble' % name); sys.exit(1)
env['SE_GENERATED_WORDS'] = words.stdout.strip()
env['SE_GENERATED_FUNCS'] = ','.join('%x/%x/%x' % (a, a, b) for a, b in spans)
env['SE_GENERATED_RETURNS'] = ret or ','.join('0' for _ in fns)
env.update(fenv)
env.update(extra)
print(name, 'FUNCS=' + env['SE_GENERATED_FUNCS'], 'RETURNS=' + env['SE_GENERATED_RETURNS'], 'WORDS="' + env['SE_GENERATED_WORDS'] + '"')
d = tempfile.mkdtemp(prefix='a3r36.', dir=S)
p = subprocess.run('ulimit -v 3500000; timeout 300 %s/build/bin/SaturnExplorerSh2GeneratedTests %s/x 2>&1' % (W, d), shell=True, env=env, capture_output=True, text=True, executable='/bin/bash')
subprocess.run(['rm', '-rf', d])
r = p.stdout
# A crash, a timeout (124), a failed CHECK or a run that never printed its summary is not a fixture with no findings: say so, keep the
# child's own lines, and exit non-zero so run.sh does not summarise it as clean. A run that finished with findings exits 1 through
# exactly one CHECK, `rep.findings.empty()`; that is a result, not a failure.
fm = re.search(r'^(\d+) finding\(s\)$', r, re.M)
checks = re.findall(r'^CHECK failed at .*$', r, re.M)
if not fm: done = False
elif p.returncode == 0: done = re.search(r'^all passed$', r, re.M) is not None and not checks
else: done = p.returncode == 1 and int(fm.group(1)) > 0 and len(checks) == 1 and checks[0].endswith(': rep.findings.empty()')
if not done:
    print(r)
    print('RUN FAILED: %s rc=%d' % (name, p.returncode))
    sys.exit(p.returncode or 1)
out = r[r.find('default request'):] if 'default request' in r else r
keep = os.environ.get('FULL')
gen = {}
for l in out.splitlines():
    if keep or not (l.startswith('  warning') or l.startswith('  fates') or l.startswith('  [n:generator') or l.startswith('CHECK') or 'ms total' in l or l.startswith('  note')) :
        print(l)
    elif l.startswith('  note'): print(l[:230])
    elif l.startswith('  [n:generator'):
        k = l.split(']')[0] + ']'; gen[k] = gen.get(k, 0) + 1
# The generator's own findings (a fixture that breaks the callee-saved convention on purpose, say) are not printed line by line, but they
# are findings: one line per class keeps them in run.sh's summary (A3-254), so a nonzero finding count never reads as "none".
if not keep:
    for k, c in sorted(gen.items()): print('%s (generator finding, %d line(s))' % (k, c))

