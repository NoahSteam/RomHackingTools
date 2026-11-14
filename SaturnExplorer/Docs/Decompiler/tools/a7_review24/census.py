#!/usr/bin/env python3
# Area 7 review round 24 (A7-175): in a token dump (tools/a7_round2/corpus.sh, SE_GAME_TOKEN_DUMP), the statements drawn only at
# an instruction that makes nothing of them: a plain assignment (not a condition, a loop head, a goto or a return) whose drawn
# tokens are all at one branch (bt, bf, bt/s, bf/s, bra, braf). Split by whether the line is a conversion `V = (type)W;` and by the
# materialised mark (A7-25's copies are drawn where the engine put them, on purpose).
# usage: census.py <dump.tsv> [max samples]
import re, sys
BR = {'bt', 'bf', 'bt/s', 'bf/s', 'bra', 'braf'}
KEY = re.compile(r'^\s*(if|while|for|do|switch|return|goto|else|case|default|\}|\{)\b')
conv = re.compile(r'^\s*[A-Za-z_][A-Za-z_0-9.]* = \([a-z_0-9 *]+\)[A-Za-z_0-9.]+;$')
def main():
    path = sys.argv[1]; nmax = int(sys.argv[2]) if len(sys.argv) > 2 else 20
    fn = None; lines = {}; toks = {}
    counts = {}; samples = []
    def flush():
        for li, text in lines.items():
            ts = toks.get(li, [])
            drawn = [t for t in ts if t[0] < t[1]]
            if not drawn or KEY.match(text) or ' = ' not in text: continue
            mn = {t[3].split(' ')[0] for t in drawn}
            if not mn <= BR: continue
            k = ('conversion' if conv.match(text) else 'other', 'materialised' if all(t[4] for t in drawn) else 'plain')
            counts[k] = counts.get(k, 0) + 1
            if k == ('conversion', 'plain') and len(samples) < nmax:
                samples.append('%s\t%s\t%s' % (fn, text.strip(), ' '.join(sorted({'%08X %s' % (t[2], t[3]) for t in drawn}))))
    for row in open(path, encoding='utf-8', errors='replace'):
        f = row.rstrip('\n').split('\t')
        if f[0] == 'F':
            flush(); fn = f[1] + ' ' + f[2]; lines = {}; toks = {}
        elif f[0] == 'L':
            lines[int(f[1])] = f[3] if len(f) > 3 else ''
        elif f[0] == 'T':
            toks.setdefault(int(f[1]), []).append((int(f[3]), int(f[4]), int(f[6], 16), f[9], f[11] == '1' if len(f) > 11 else False))
    flush()
    for k in sorted(counts): print('%s %s: %d' % (k[0], k[1], counts[k]))
    for s in samples: print('  ' + s)
main()
