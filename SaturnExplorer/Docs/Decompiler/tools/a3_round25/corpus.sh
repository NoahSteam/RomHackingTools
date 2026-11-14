#!/bin/bash
# Area 3 round 25: both corpus images alone and lent with one binary (a7_round2/corpus.sh), and the token dumps compared function by function with
# a baseline's (a dump dir holding <btag>007_alone.tsv ... <btag>008_lent.tsv, e.g. a3_a3192/corpus.sh's from master's binary).
# usage: corpus.sh <SaturnExplorerSh2DecompilerTests> <image dir with hwram007.bin and hwram008.bin> <scratch> <baseline dump dir> <baseline tag>
B=$1; H=$2; D=$3; BASE=$4; BT=$5
here=$(cd "$(dirname "$0")" && pwd)
for img in 007 008; do
    bash "$here/../a7_round2/corpus.sh" "$B" "$H/hwram$img.bin" "$D" "r$img" lend
    echo "$img: $(grep -E 'missing and said' "$D/corpusr$img.log" | head -1)"; grep -E '\[after\] real entries|all passed|done ' "$D/corpusr$img.log" | tail -3
    for mode in alone lent; do
        python3 -I - "$BASE/$BT${img}_$mode.tsv" "$D/dump/r${img}_$mode.tsv" "$img $mode" <<'PY'
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
