#!/usr/bin/env python3
"""Prints the assembled halfwords (hex, comma separated) of a fixture of ../a3_round41/fixtures.src, laid out as run.py does (functions padded to 0x20 bytes from 0x06009000): the words the A3-280 pins in Sh2DecompilerTests.cpp hold. usage: words.py <fixture>"""
import os, re, subprocess, sys, tempfile
here = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'a3_round41')
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
for f in fns:       # `.align4` (round 41): a nop when the next word is at an odd halfword (every function starts 32-byte aligned), else nothing
    w, o = 0, []
    for l in f:
        if l == '.align4': o += ['nop'] if w % 2 else []; w += w % 2; continue
        o.append(l); w += 0 if l.endswith(':') else 2 if l.startswith('.addr') else 1
    f[:] = o
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
print(words.stdout.strip())
