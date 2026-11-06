#!/bin/bash
# Area 10 round 2: what a mutant changes in the printed texts, not only whether a check fails.
# usage: textdiff.sh <repo root> <build dir> <out dir> <name> '<apply command>' [<hwram image>]
#   Applies the mutant (a shell command run in the repo root; empty for the base run), rebuilds
#   SaturnExplorerSh2DecompilerTests (-j2, -j1 under 6,000 MB available), runs the suite under
#   ulimit -v 3500000 and timeout into <out>/<name>.unit.txt (with an image: SE_GAME_HWRAM and
#   SE_GAME_OUT=<out>/<name>.texts too), then puts the vendored tree back (git checkout) and prints
#   git status. Compare two runs with:
#   diff <(sed -E 's/[0-9]+\.[0-9]+ ms//g' base.unit.txt) <(sed -E 's/[0-9]+\.[0-9]+ ms//g' <name>.unit.txt)
#   diff -rq base.texts <name>.texts
# Mutants used in the round (patch_mutants.py of tools/a11_round2 writes the NNNN.hKK.rev.patch files):
#   0014 hunk K alone:  git apply -R --recount $(cat REV/0014.hKK.rev.patch.ctx) REV/0014.hKK.rev.patch
#   0060 (h01 h02 h04), 0078 (h01..h05): the same for each hunk
#   V-0061-memescape:   sed -i 's/^  if (memoryEscape) escaped = true;$/  if (false \&\& memoryEscape) escaped = true;/' <vendor>/cpp/heritage.cc
#   V-0065-steppeddown: an early 'return false' in LoadGuard::pointerSteppedDown (tools/a11_round2/mutate.py)
set -u
R=$1; B=$2; O=$3; name=$4; apply=$5; img=${6:-}
mkdir -p "$O"; cd "$R" || exit 1
if [ -n "$apply" ]; then bash -c "$apply" || { echo "APPLY FAILED"; exit 1; }; fi
avail=$(free -m | awk '/Mem:/{print $7}'); j=2; [ "$avail" -lt 6000 ] && j=1
cmake --build "$B" -j$j --target SaturnExplorerSh2DecompilerTests > "$O/$name.build.log" 2>&1 || echo "BUILD FAILED (see $O/$name.build.log)"
rm -rf "$O/$name.scratch" "$O/$name.texts"; mkdir -p "$O/$name.scratch"
if [ -n "$img" ]; then
  mkdir -p "$O/$name.texts"
  ( ulimit -v 3500000; SE_GAME_HWRAM="$img" SE_GAME_OUT="$O/$name.texts" timeout 1100 "$B/bin/SaturnExplorerSh2DecompilerTests" "$O/$name.scratch" > "$O/$name.unit.txt" 2>&1; echo "EXIT $?" >> "$O/$name.unit.txt" )
else
  ( ulimit -v 3500000; timeout 600 "$B/bin/SaturnExplorerSh2DecompilerTests" "$O/$name.scratch" > "$O/$name.unit.txt" 2>&1; echo "EXIT $?" >> "$O/$name.unit.txt" )
fi
rm -rf "$O/$name.scratch"
[ -n "$apply" ] && git checkout -- SaturnExplorer/FrontEnd/third_party/ghidra-decompiler
git status --porcelain
tail -2 "$O/$name.unit.txt"
