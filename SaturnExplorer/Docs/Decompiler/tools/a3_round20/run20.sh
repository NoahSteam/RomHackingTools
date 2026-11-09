#!/bin/bash
# Area 3 round 20: assemble one fixture of fixtures.src and run it through the generator's single-subject mode (SE_GENERATED_WORDS).
# usage: W=<SaturnExplorer dir> S=<scratch dir> run20.sh <name> [env...]   (names: the `name` lines of fixtures.src)
W=${W:?set W to the SaturnExplorer directory}; S=${S:?set S to a scratch directory}
here=$(cd "$(dirname "$0")" && pwd)
name=$1; shift
src=$(awk -v n="$name" '$1=="name"{on=($2==n);next} on && $1!="#"' "$here/fixtures.src")
[ -n "$src" ] || { echo "no fixture $name"; exit 2; }
words=$(echo "$src" | python3 "$here/../a3_round16/asm.py") || exit 1
echo "$name: $words"
D=$(mktemp -d "$S/r20.XXXX")
( ulimit -v 3500000; env "$@" SE_GENERATED_WORDS="$words" timeout 300 "$W/build/bin/SaturnExplorerSh2GeneratedTests" "$D/x" 2>&1 | sed -n '/default request/,$p' )
rm -rf "$D"
