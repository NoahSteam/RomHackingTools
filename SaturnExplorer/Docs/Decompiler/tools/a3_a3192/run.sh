#!/bin/bash
# Area 3, A3-192: the round 24 fixtures (tools/a3_round24/fixtures.src) through the generator's SE_GENERATED_WORDS mode; prints each caller's
# prototype line and its return. Right: `return xStack_c;` (o4b: `return xStack_10;`) for all nine, no `return 5;`.
# usage: W=<SaturnExplorer dir> S=<scratch dir> run.sh [fixture ...]   (default: o0 o1 o1c o2 o3 o4b o5 o6 o7)
here=$(cd "$(dirname "$0")" && pwd)
names=${*:-o0 o1 o1c o2 o3 o4b o5 o6 o7}
for f in $names; do
    echo "== $f"
    W=$W S=$S python3 -I "$here/../a3_round24/run24.py" "$f" | grep -E 'prototype|return [^;]|finding' | tail -3
done
