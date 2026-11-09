#!/bin/bash
# Area 3, A3-178 to A3-180 fixer: assemble one fixture of fixtures.src (here, else ../a3_round22/fixtures.src) and run it through the
# generator's single-subject mode (SE_GENERATED_WORDS). BIN overrides the generator binary (to compare a build of master's host).
# usage: W=<SaturnExplorer dir> S=<scratch dir> [BIN=<binary>] run.sh <name> [env...]
W=${W:?set W to the SaturnExplorer directory}; S=${S:?set S to a scratch directory}
BIN=${BIN:-$W/build/bin/SaturnExplorerSh2GeneratedTests}
here=$(cd "$(dirname "$0")" && pwd)
name=$1; shift
src=""
for f in "$here/fixtures.src" "$here/../a3_round22/fixtures.src"; do
  [ -n "$src" ] || src=$(awk -v n="$name" '$1=="name"{on=($2==n);next} on && $1!="#"' "$f")
done
[ -n "$src" ] || { echo "no fixture $name"; exit 2; }
words=$(echo "$src" | python3 -I "$here/../a3_round22/asm22.py") || exit 1
echo "$name: $words"
D=$(mktemp -d "$S/f178.XXXX")
( ulimit -v 3500000; env "$@" SE_GENERATED_WORDS="$words" timeout 300 "$BIN" "$D/x" 2>&1 | sed -n '/default request/,$p' )
rm -rf "$D"
