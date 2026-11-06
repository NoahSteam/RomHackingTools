# A10-16 sibling family: a stack word built by a store and an overlapping overwrite, read back narrower or whole, then used.
# usage: python3 -I stackword16.py list [uses]   (one name and its words per line; 'handN' of the run is line N+1)
#        python3 -I stackword16.py words [uses]  (the words, ';'-separated, for SE_GENERATED_PRINTEDC_WORDS)
# uses: a comma list of USES keys; without it, all of them (572 functions).
import itertools, sys
STORES = {'L': ['2F52'], 'W0': ['2F51'], 'W2': ['6053', '81F1']}          # mov.l r5,@r15 ; mov.w r5,@r15 ; mov r5,r0 + mov.w r0,@(2,r15)
OVER = {'b0': ['6073', '80F0'], 'b1': ['6073', '80F1'], 'b2': ['6073', '80F2'], 'b3': ['6073', '80F3'],
        'h0': ['6073', '81F0'], 'h2': ['6073', '81F1']}
READS = {'w0': ['85F0'], 'w2': ['85F1'], 'b0': ['84F0'], 'b1': ['84F1'], 'b2': ['84F2'], 'b3': ['84F3'], 'l': ['60F2']}
USES = {'ret': [], 'swap': ['6109', '6013'], 'shlr16': ['4029'], 'shar': ['4021'], 'extuw': ['600D'], 'extub': ['600C'],
        'pz': ['4011', '0029'], 'zw_swap': ['600D', '6109', '6013'], 'zw_shar': ['600D', '4021'], 'zw_add': ['600D', '304C'],
        'zw_pz': ['600D', '4011', '0029'], 'zw_pair': ['600D', '6109', '6073'], 'zb_shar': ['600C', '4021'], 'zb_add': ['600C', '304C'],
        'sb_swap': ['600E', '6109', '6013'], 'sw_swap': ['600F', '6109', '6013'], 'sw_add': ['600F', '304C'], 'add4': ['304C'], 'shlr': ['4001'], 'add1': ['7001'], 'and': ['2049'], 'pair': ['6109', '6073']}
# 'and' = and r4,r0 ; 'pair' = swap.w r0,r1 ; mov r7,r0 (the A10-16 return in r0:r1)
def span(rd):
    return {'w0': (0, 2), 'w2': (2, 4), 'b0': (0, 1), 'b1': (1, 2), 'b2': (2, 3), 'b3': (3, 4), 'l': (0, 4)}[rd]
def ospan(o):
    return {'b0': (0, 1), 'b1': (1, 2), 'b2': (2, 3), 'b3': (3, 4), 'h0': (0, 2), 'h2': (2, 4)}[o]
def sspan(s):
    return {'L': (0, 4), 'W0': (0, 2), 'W2': (2, 4)}[s]
out = []
for s, o, r, u in itertools.product(STORES, OVER, READS, USES):
    a, b = span(r); oa, ob = ospan(o); sa, sb = sspan(s)
    if not (a < ob and oa < b): continue          # the read must see the overwrite
    if not (a >= min(sa, oa) and b <= max(sb, ob)): continue   # and no uninitialised byte
    if (a, b) == (oa, ob): continue                # nor be the overwrite alone (no piece)
    if s != 'L' and not (a < sb and sa < b): continue
    w = ['7FFC'] + STORES[s] + OVER[o] + READS[r] + ['7F04'] + USES[u] + ['000B', '0009']
    out.append((f'{s}.{o}.{r}.{u}', ' '.join(w)))
mode = sys.argv[1] if len(sys.argv) > 1 else 'list'
if len(sys.argv) > 2: out = [x for x in out if any(x[0].endswith('.' + u) for u in sys.argv[2].split(','))]
if mode == 'list':
    for n, w in out: print(n, w)
else:
    print(';'.join(w for n, w in out))
