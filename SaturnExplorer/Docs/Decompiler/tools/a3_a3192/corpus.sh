#!/bin/bash
# Area 3, A3-192: both corpus images alone and lent with two binaries (master's and this branch's), and the token dumps compared.
# usage: corpus.sh <master SaturnExplorerSh2DecompilerTests> <branch SaturnExplorerSh2DecompilerTests> <image dir with hwram007.bin and hwram008.bin> <scratch>
# One heavy job at a time (a7_round2/corpus.sh's rules). Prints the lent counts of each run and the functions whose text differs.
M=$1; B=$2; H=$3; D=$4
here=$(cd "$(dirname "$0")" && pwd)
for img in 007 008; do
    bash "$here/../a7_round2/corpus.sh" "$M" "$H/hwram$img.bin" "$D/m" "m$img" lend
    bash "$here/../a7_round2/corpus.sh" "$B" "$H/hwram$img.bin" "$D/b" "b$img" lend
    for r in m b; do echo "$r $img: $(grep -E 'missing and said' "$D/$r/corpus$r$img.log" | head -1)"; grep -E '\[after\] real entries' "$D/$r/corpus$r$img.log"; done
    for mode in alone lent; do
        python3 -I - "$D/m/dump/m${img}_$mode.tsv" "$D/b/dump/b${img}_$mode.tsv" "$img $mode" <<'PY'
import sys
def texts(p):
    out, cur = {}, None
    for l in open(p, encoding='utf-8', errors='replace'):
        t = l.rstrip('\n').split('\t')
        if t[0] == 'F': cur = t[2]; out[cur] = []
        elif t[0] == 'L' and cur: out[cur].append(t[3] if len(t) > 3 else '')
    return out
a, b = texts(sys.argv[1]), texts(sys.argv[2])
diff = sorted(k for k in set(a) | set(b) if a.get(k) != b.get(k))
print(sys.argv[3] + ': ' + str(len(diff)) + ' function text(s) differ' + (': ' + ' '.join(diff) if diff else ''))
PY
    done
done
