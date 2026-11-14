#!/bin/bash
# The required runs after the last code edit: usage final.sh <worktree SaturnExplorer dir> <scratch dir> <image dir with hwram007.bin and hwram008.bin>
W="$1"; S="$2"; H="$3"; cd "$W"
( ulimit -v 6000000
echo "== ctest"; ctest --test-dir build --output-on-failure -j2 2>&1 | tail -4
echo "== fault"; build/bin/SaturnExplorerDecompilerFaultTests "$S/fault" 2>&1 | grep "fault matrix"
echo "== 20k"; SE_GENERATED_SEEDS=20000 build/bin/SaturnExplorerSh2GeneratedTests "$S/m20k" 2>&1 | grep -E "failing seeds=|^FINDING" | grep -o "failing seeds=[0-9]*; findings=[0-9]*\|^FINDING.*"
echo "== printed-C"; SE_GENERATED_PRINTEDC=only SE_GENERATED_SEEDS=3000 build/bin/SaturnExplorerSh2GeneratedTests "$S/pc" 2>&1 | grep -o "agrees=[0-9]*" | sort -t= -k2 -n | tail -1
for i in 007 008; do echo "== corpus $i"; SE_GAME_HWRAM="$H/hwram$i.bin" SE_GAME_LEND=1 build/bin/SaturnExplorerSh2DecompilerTests "$S/corpus$i" 2>&1 | grep -E "all passed|missing and said|signatures lent|real entries|FAIL|failed" | head -8; done
) 
