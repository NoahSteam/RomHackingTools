#!/usr/bin/env python3
# Area 7 review round 24: the corpus spot check. From a token dump (tools/a7_round2/corpus.sh), pick functions by a fixed stride
# and print each statement with the instructions its drawn tokens are at (and the empty ones it names), to read against the
# disassembly. usage: spot.py <dump.tsv> <how many> [min lines] [max lines]   or   spot.py <dump.tsv> =<entry hex>
import sys
def load(path):
    funcs = []; cur = None
    for row in open(path, encoding='utf-8', errors='replace'):
        f = row.rstrip('\n').split('\t')
        if f[0] == 'F':
            cur = {'entry': f[2], 'lines': {}, 'toks': {}}; funcs.append(cur)
        elif f[0] == 'L':
            cur['lines'][int(f[1])] = (f[2], f[3] if len(f) > 3 else '')
        elif f[0] == 'T':
            cur['toks'].setdefault(int(f[1]), []).append((int(f[3]) < int(f[4]), f[6], f[9], f[10] if len(f) > 10 else '', f[11] == '1' if len(f) > 11 else False))
    return funcs
def show(fn):
    print('== %s' % fn['entry'])
    for li in sorted(fn['lines']):
        first, text = fn['lines'][li]
        ts = fn['toks'].get(li, [])
        if not ts: continue
        seen = []
        for drawn, a, insn, txt, m in ts:
            k = (a, drawn, m)
            if k in seen: continue
            seen.append(k)
        parts = ['%s%s%s %s' % (a[-5:], '' if drawn else '(e)', 'm' if m else '', insn) for a, drawn, m in seen for insn in [next(t[2] for t in ts if t[1] == a)]]
        print('  %-60s | first %s | %s' % (text.strip()[:60], first[-5:], '; '.join(parts)))
def main():
    funcs = load(sys.argv[1])
    if sys.argv[2].startswith('='):
        for fn in funcs:
            if fn['entry'].upper() == sys.argv[2][1:].upper(): show(fn)
        return
    n = int(sys.argv[2]); lo = int(sys.argv[3]) if len(sys.argv) > 3 else 15; hi = int(sys.argv[4]) if len(sys.argv) > 4 else 60
    pool = [f for f in funcs if lo <= len(f['lines']) <= hi]
    step = max(1, len(pool) // n)
    for fn in pool[::step][:n]: show(fn)
main()
