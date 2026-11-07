# pairs20.py <cls output> : for each signed/unsigned pair (names ending .s/.u) say whether the two texts are the same
import sys, collections
recs = {}
for r in open(sys.argv[1]).read().split('\n@@')[0:]:
    lines = r.strip('\n').split('\n')
    h = lines[0].lstrip('@ ').split()
    if len(h) < 3: continue
    name, cls = h[1], h[2]
    words = lines[1].split('words:')[1].strip()
    txt = [l.strip() for l in lines[2:] if l.strip()]
    sig = txt[0] if txt else ''
    body = '\n'.join(txt[1:])
    recs[name] = (cls, sig, body, words)
same_full = same_body = n = 0
cls_pairs = collections.Counter()
ex = []
for name in sorted(recs):
    if not name.endswith('.s'): continue
    u = name[:-2] + '.u'
    if u not in recs: continue
    n += 1
    s, uu = recs[name], recs[u]
    if s[2] == uu[2]:
        same_body += 1
        full = s[1] == uu[1]
        same_full += full
        cls_pairs[(s[0], uu[0], 'full' if full else 'body')] += 1
        if len(ex) < 400: ex.append((name, s, uu, full))
print('pairs', n, 'same body', same_body, 'same whole text', same_full)
for k, v in sorted(cls_pairs.items()): print(' ', k, v)
for name, s, uu, full in ex:
    print('==', name[:-2], 'whole' if full else 'body', '|', s[0], '/', uu[0])
    print('   s:', s[3]); print('   u:', uu[3])
    print('   ' + s[1]); print('   ' + uu[1]) if not full else None
    print('   ' + s[2].replace('\n', ' '))
