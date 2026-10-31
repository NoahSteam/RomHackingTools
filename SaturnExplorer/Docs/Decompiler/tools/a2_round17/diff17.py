#!/usr/bin/env python3
# usage: diff17.py <log of the old engine> <log of the new engine> [shown]
# Two printed-C differential logs of the same word list (SE_GENERATED_PRINTEDC=only SE_GENERATED_PRINTEDC_WORDS=...): per function the verdict class and the text.
# Prints how many texts are identical, how many differ and in which verdict, and the first <shown> differing texts side by side.
import re, sys
def load(path):
    items = {}; cur = None
    head = re.compile(r'^(hand\d+): (\S+)')
    for ln in open(path, errors='replace').read().split('\n'):
        m = head.match(ln)
        if m: cur = m.group(1); items[cur] = [m.group(2), []]
        elif cur and not ln.startswith(('printed-C:', 'FINDING', '  words:', '  detail:', '  replay:', 'Sh2Generated', 'CHECK', 'all passed')): items[cur][1].append(ln)
    return {k: (v[0], '\n'.join(x for x in v[1] if x.strip()).strip()) for k, v in items.items()}
a, b = load(sys.argv[1]), load(sys.argv[2]); shown = int(sys.argv[3]) if len(sys.argv) > 3 else 5
same = diff = 0; cls = {}
out = []
for k in sorted(set(a) & set(b), key=lambda s: int(s[4:])):
    (ca, ta), (cb, tb) = a[k], b[k]
    if ta == tb and ca == cb: same += 1; continue
    diff += 1
    cls[(ca, cb)] = cls.get((ca, cb), 0) + 1
    out.append((k, ca, cb, ta, tb))
print('functions %d, identical text and verdict %d, different %d' % (len(set(a) & set(b)), same, diff))
for (ca, cb), n in sorted(cls.items()): print('  %s -> %s: %d' % (ca, cb, n))
for k, ca, cb, ta, tb in out[:shown]:
    print('--- %s (%s -> %s)\nOLD: %s\nNEW: %s' % (k, ca, cb, ta.replace('\n', ' ')[:500], tb.replace('\n', ' ')[:500]))
