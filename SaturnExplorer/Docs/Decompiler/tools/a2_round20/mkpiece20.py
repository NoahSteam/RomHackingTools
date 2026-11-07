# r20: a stack piece (store + overwrite + read, as round 19's mkpiece.py) read by every op that may read it signed or unsigned:
# shifts, sign tests, compares with immediates, multiplies (16, 32, 64 bit), div0s, addv/subv, addc/subc, negation, cmp/str, extensions.
# usage: mkpiece20.py list|count [stride] [offset]
import itertools, sys
STORES = {'L': ['2F52'], 'W0': ['2F51'], 'W2': ['6053', '81F1']}
OVER = {'b0': ['6073', '80F0'], 'b1': ['6073', '80F1'], 'b2': ['6073', '80F2'], 'b3': ['6073', '80F3'], 'h0': ['6073', '81F0'], 'h2': ['6073', '81F1']}
READS = {'w0': ['85F0'], 'w2': ['85F1'], 'b0': ['84F0'], 'b1': ['84F1'], 'b2': ['84F2'], 'b3': ['84F3'], 'l': ['60F2']}
def span(rd): return {'w0': (0, 2), 'w2': (2, 4), 'b0': (0, 1), 'b1': (1, 2), 'b2': (2, 3), 'b3': (3, 4), 'l': (0, 4)}[rd]
def ospan(o): return {'b0': (0, 1), 'b1': (1, 2), 'b2': (2, 3), 'b3': (3, 4), 'h0': (0, 2), 'h2': (2, 4)}[o]
def sspan(s): return {'L': (0, 4), 'W0': (0, 2), 'W2': (2, 4)}[s]
PUSE = {'none': [], 'extuw': ['600D'], 'extsw': ['600F'], 'extsb': ['600E'], 'extub': ['600C']}
MOVT = ['0029']
USES = {
 'shar': ['4021'], 'shal': ['4020'], 'shlr': ['4001'], 'shll_t': ['4000'] + MOVT, 'shar_t': ['4021'] + MOVT,
 'shar2': ['4021', '4021'], 'shlr8': ['4019'], 'shlr16': ['4029'],
 'pz': ['4011'] + MOVT, 'pl': ['4015'] + MOVT,
 'eqm1': ['88FF'] + MOVT, 'eqm128': ['8880'] + MOVT, 'eq7f': ['887F'] + MOVT, 'eq1': ['8801'] + MOVT,
 'tst80': ['C880'] + MOVT,
 'mulsw': ['240F', '001A'], 'muluw': ['240E', '001A'], 'mull': ['0407', '001A'],
 'dmuls_h': ['340D', '000A'], 'dmulu_h': ['3405', '000A'], 'dmuls_l': ['340D', '001A'],
 'div0s': ['2407'] + MOVT, 'div0s_r': ['2047'] + MOVT,
 'addv': ['304F'] + MOVT, 'subv': ['304B'] + MOVT, 'subv_r': ['340B', '0029'],
 'addc': ['0008', '304E', '0029'], 'subc': ['0008', '304A', '0029'],
 'neg_gt': ['600B', '3407'] + MOVT, 'not_ge': ['6007', '3403'] + MOVT,
 'add1_gt': ['7001', '3407'] + MOVT, 'addm1_pz': ['70FF', '4011'] + MOVT,
 'add_r4_pz': ['304C', '4011'] + MOVT, 'sub_r4_gt': ['3048', '3067'] + MOVT,
 'str': ['240C'] + MOVT,
 'shar_gt': ['4021', '3407'] + MOVT, 'shar_eq_r4': ['4021', '614F', '3010'] + MOVT,
 'neg_ret': ['600B'], 'negc': ['0008', '600A'],
 'div1': ['2407', '3404', '6043'],
}
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
    for pu, us in itertools.product(PUSE, USES):
        w = ['7FFC'] + STORES[s] + OVER[o] + READS[r] + ['7F04'] + PUSE[pu] + USES[us] + ['000B', '0009']
        out.append((f'{s}.{o}.{r}.{pu}.{us}', ' '.join(w)))
mode = sys.argv[1]
stride = int(sys.argv[2]) if len(sys.argv) > 2 else 1
off = int(sys.argv[3]) if len(sys.argv) > 3 else 0
out = out[off::stride]
if mode == 'list':
    for n, w in out: print(n, w)
else: print(len(out))
