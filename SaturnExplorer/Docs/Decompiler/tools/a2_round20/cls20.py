# cls20.py log list : per-function class and text, keyed by the list's names
import re, sys
log, lst = sys.argv[1], sys.argv[2]
names = [l.split(' ', 1) for l in open(lst).read().splitlines()]
recs = []; batch = 0; cur = None
lines = open(log).read().splitlines()
i = 0
while i < len(lines):
    l = lines[i]
    m = re.match(r'^hand(\d+): (\S+)(.*)', l)
    if m:
        idx = batch * 150 + int(m.group(1))
        body = []
        i += 1
        while i < len(lines) and not re.match(r'^(hand\d+|printed-C:)', lines[i]):
            body.append(lines[i]); i += 1
        txt = '\n'.join(b for b in body if b.strip())
        recs.append((idx, m.group(2), m.group(3), txt))
        continue
    if l.startswith('printed-C:'): batch += 1
    i += 1
for idx, c, info, txt in recs:
    n, w = names[idx]
    print('@@', idx, n, c, info.strip()[:200]); print('   words:', w)
    t = txt.split('\nhand')[0]
    print('\n'.join('   ' + x for x in t.splitlines()))
