#!/bin/bash
# Area 3, A3-197: round 25's register sweep forms vC and vE (a3_round25/run25.py) and this row's fixtures w1-w7 (run197.py); prints function 0's
# return lines, the cautions "this call may change rN" and the finding count of each.
# usage: W=<SaturnExplorer dir> S=<scratch dir> run.sh [fixture ...]   (default: vCr0-vCr14, vEr0-vEr14, w1-w7)
here=$(cd "$(dirname "$0")" && pwd)
names=${*:-"$(grep -oE '^name v[CE][a-z0-9]*' "$here/../a3_round25/fixtures.src" | cut -d' ' -f2) $(grep -oE '^name w[0-9]+' "$here/fixtures.src" | cut -d' ' -f2)"}
for f in $names; do
    case $f in w*) run="$here/run197.py";; *) run="$here/../a3_round25/run25.py";; esac
    out=$(python3 -I "$run" "$f" 2>&1)
    body=$(echo "$out" | sed -n '/function 0 at/,/finding(s)/p')
    echo "$f: $(echo "$body" | grep -E '^\s+return' | tr -s ' ' | tr '\n' ' ')caution: $(echo "$body" | grep -oE 'this call may change r[0-9]+' | sed 's/this call may change //' | tr '\n' ' ')findings $(echo "$out" | grep -c '^  \[')"
done
