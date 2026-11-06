# r19: a stack piece (store + overwrite + read, as stackword16.py) compared with a narrow cut of r4 / r6
# usage: mkpiece.py list|words [stride] [offset]
import itertools, sys
STORES = {'L': ['2F52'], 'W0': ['2F51'], 'W2': ['6053', '81F1']}
OVER = {'b0': ['6073', '80F0'], 'b1': ['6073', '80F1'], 'b2': ['6073', '80F2'], 'b3': ['6073', '80F3'], 'h0': ['6073', '81F0'], 'h2': ['6073', '81F1']}
READS = {'w0': ['85F0'], 'w2': ['85F1'], 'b0': ['84F0'], 'b1': ['84F1'], 'b2': ['84F2'], 'b3': ['84F3'], 'l': ['60F2']}
def span(rd): return {'w0': (0, 2), 'w2': (2, 4), 'b0': (0, 1), 'b1': (1, 2), 'b2': (2, 3), 'b3': (3, 4), 'l': (0, 4)}[rd]
def ospan(o): return {'b0': (0, 1), 'b1': (1, 2), 'b2': (2, 3), 'b3': (3, 4), 'h0': (0, 2), 'h2': (2, 4)}[o]
def sspan(s): return {'L': (0, 4), 'W0': (0, 2), 'W2': (2, 4)}[s]
PUSE = {'none': [], 'extuw': ['600D'], 'extub': ['600C'], 'extsb': ['600E'], 'extsw': ['600F']}
# the other side into r1
QS = {'r4': ['6143'], 'extsw': ['614F'], 'extuw': ['614D'], 'extsb': ['614E'], 'extub': ['614C'], 'and80': ['6143', 'C9FF' if False else '2149'] }
QS = {'r4': ['6143'], 'extsw': ['614F'], 'extuw': ['614D'], 'extsb': ['614E'], 'extub': ['614C'],
      'andw': ['614D', '6163', '2169' if False else '2169']}
QS = {'r4': ['6143'], 'extsw': ['614F'], 'extuw': ['614D'], 'extsb': ['614E'], 'extub': ['614C']}
CMPS = {'eq': '3010', 'hs': '3012', 'hs_r': '3102', 'ge': '3013', 'ge_r': '3103', 'hi': '3016', 'gt_r': '3107'}
combos = []
for s, o, r in itertools.product(STORES, OVER, READS):
    a, b = span(r); oa, ob = ospan(o); sa, sb = sspan(s)
    if not (a < ob and oa < b): continue
    if not (a >= min(sa, oa) and b <= max(sb, ob)): continue
    if (a, b) == (oa, ob): continue
    if s != 'L' and not (a < sb and sa < b): continue
    combos.append((s, o, r))
out = []
for (s, o, r) in combos:
    for pu, qs, cm in itertools.product(PUSE, QS, CMPS):
        if r.startswith('b') and pu in ('extsw','extuw','none') and False: pass
        w = ['7FFC'] + STORES[s] + OVER[o] + READS[r] + ['7F04'] + PUSE[pu] + QS[qs] + [CMPS[cm], '0029', '000B', '0009']
        out.append((f'{s}.{o}.{r}.{pu}.{qs}.{cm}', ' '.join(w)))
mode = sys.argv[1]
stride = int(sys.argv[2]) if len(sys.argv) > 2 else 1
off = int(sys.argv[3]) if len(sys.argv) > 3 else 0
out = out[off::stride]
if mode == 'list':
    for n, w in out: print(n, w)
elif mode == 'count': print(len(out))
else: print(';'.join(w for n, w in out))
