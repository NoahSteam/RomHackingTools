#!/bin/bash
# Area 3, A3-202: this fixer's fixtures (fixtures.src, through run.py); prints function 0's return lines, the cautions "this call may change ..."
# and the finding count of each.
# usage: W=<SaturnExplorer dir> S=<scratch dir> run.sh [fixture ...]   (default: every fixture of fixtures.src)
here=$(cd "$(dirname "$0")" && pwd)
names=${*:-$(grep -oE '^name [a-z0-9]+' "$here/fixtures.src" | cut -d' ' -f2)}
for f in $names; do
    out=$(python3 -I "$here/run.py" "$f" 2>&1)
    body=$(echo "$out" | sed -n '/function 0 at/,/finding(s)/p')
    echo "$f: $(echo "$body" | grep -E '^\s+return' | tr -s ' ' | tr '\n' ' ')caution: $(echo "$body" | grep -oE 'this call may change ((r[0-9]+|gbr|vbr)(, | and )?)+' | sed 's/this call may change //; s/ and /,/g; s/, /,/g' | tr '\n' ' ')findings $(echo "$out" | grep -c '^  \[')"
done
