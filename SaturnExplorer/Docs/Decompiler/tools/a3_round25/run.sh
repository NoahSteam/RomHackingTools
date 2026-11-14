#!/bin/bash
# Area 3 round 25: the fixtures of fixtures.src through run25.py; prints function 0's return lines and the finding count of each.
# usage: W=<SaturnExplorer dir> S=<scratch dir> run.sh [fixture ...]   (default: the A3-192 attacks k*, and regs.py's register sweep v*)
here=$(cd "$(dirname "$0")" && pwd)
names=${*:-$(grep -oE '^name (k|v)[A-Za-z0-9]*' "$here/fixtures.src" | cut -d' ' -f2)}
for f in $names; do
    out=$(python3 -I "$here/run25.py" "$f" 2>&1)
    echo "$f: $(echo "$out" | sed -n '/function 0 at/,/finding(s)/p' | grep -E '^\s+return|frame address' | tr -s ' ' | tr '\n' ' ')findings $(echo "$out" | grep -c '^  \[')"
done
