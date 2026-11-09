#!/bin/bash
# Area 3, A3-182 and A3-183 fixer: run one fixture of fixtures.src (here, else ../a3_round23/fixtures.src) through round 23's runner
# (run23.py: functions separated by `fn`, padded, the module's FUNCS and RETURNS worked out). BIN overrides the generator binary (to
# compare a build of master's host).
# usage: W=<SaturnExplorer dir> S=<scratch dir> [BIN=<binary>] run.sh <name> [KEY=VAL ...]
W=${W:?set W to the SaturnExplorer directory}; S=${S:?set S to a scratch directory}
here=$(cd "$(dirname "$0")" && pwd)
name=$1; shift
D=$(mktemp -d "$S/f182.XXXX")
mkdir -p "$D/t/a3_round23" "$D/build/bin"
cat "$here/fixtures.src" "$here/../a3_round23/fixtures.src" > "$D/t/a3_round23/fixtures.src"
cp "$here/../a3_round23/run23.py" "$D/t/a3_round23/"
ln -s "$here/../a3_round22" "$D/t/a3_round22"
ln -s "$here/../a3_round16" "$D/t/a3_round16"
ln -s "${BIN:-$W/build/bin/SaturnExplorerSh2GeneratedTests}" "$D/build/bin/SaturnExplorerSh2GeneratedTests"
W="$D" S="$S" python3 -I "$D/t/a3_round23/run23.py" "$name" "$@"
rm -rf "$D"
