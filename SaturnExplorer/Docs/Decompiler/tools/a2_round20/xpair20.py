# r20 (A2-128): pairs of functions that differ only in the sign of one op reading a stack piece (an xunknown local or field, or an
# in-place CONCAT): cmp/gt against cmp/hi, cmp/ge against cmp/hs, shar against shlr, with constants and a second piece.
# The pair is the same function but for that op, so the two must not print the same text.
# usage: xpair20.py list    -> "name words" lines, the names end in .s (signed) or .u (unsigned)
import itertools
STORES = {'L': ['2F62'], 'W0': ['2F61'], 'W2': ['6063', '81F1']}
OVER = {'b0': ['6073', '80F0'], 'b1': ['6073', '80F1'], 'b2': ['6073', '80F2'], 'b3': ['6073', '80F3'], 'h0': ['6073', '81F0'], 'h2': ['6073', '81F1']}
READS = {'w0': ['85F0'], 'w2': ['85F1'], 'l': ['60F2']}
def span(rd): return {'w0': (0, 2), 'w2': (2, 4), 'l': (0, 4)}[rd]
def ospan(o): return {'b0': (0, 1), 'b1': (1, 2), 'b2': (2, 3), 'b3': (3, 4), 'h0': (0, 2), 'h2': (2, 4)}[o]
def sspan(s): return {'L': (0, 4), 'W0': (0, 2), 'W2': (2, 4)}[s]
# a 16-bit read is cut with extu.w for the unsigned op (exts.w is mov.w's own), so each side is the code's natural one
CONSTS = {'5': 'E105', '7f': 'E17F', 'm1': 'E1FF', 'm80': 'E180', '0': 'E100'}
# (signed op words, unsigned op words) with r1 the constant, r0 the piece
OPS = {
 'gt':   (['3017', '0029'], ['3016', '0029']),      # r0 > r1
 'gt_r': (['3107', '0029'], ['3106', '0029']),      # r1 > r0
 'ge':   (['3013', '0029'], ['3012', '0029']),
 'ge_r': (['3103', '0029'], ['3102', '0029']),
}
SHIFTS = {'sh1': (['4021'], ['4001']), 'sh2': (['4021', '4021'], ['4001', '4001'])}
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
    pre = ['7FFC'] + STORES[s] + OVER[o] + READS[r] + ['7F04']
    cut_u = ['600D'] if r != 'l' else []
    for (on, (so, uo)), (cn, cw) in itertools.product(OPS.items(), CONSTS.items()):
        out.append((f'{s}.{o}.{r}.{on}.{cn}.s', pre + [cw] + so))
        out.append((f'{s}.{o}.{r}.{on}.{cn}.u', pre + cut_u + [cw] + uo))
    for sn, (so, uo) in SHIFTS.items():
        out.append((f'{s}.{o}.{r}.{sn}.x.s', pre + so))
        out.append((f'{s}.{o}.{r}.{sn}.x.u', pre + cut_u + uo))
for n, w in out: print(n, ' '.join(w + ['000B', '0009']))
