#!/bin/bash
# Area 11 round 2: the mutation runs as pieces of at most 20 minutes (mutate.py --minutes 12 stops before a mutant
# once 12 minutes have gone; a mutant takes at most about 6). Resumable: rerun after a restart.
# usage: run_pieces.sh <group> <scratch> [extra mutate.py args]
G=$1; S=$2; shift 2
HW=${HW:-/tmp/claude-0/-home-user-RomHackingTools/aae0ae36-3afa-5288-8786-5063bd965426/scratchpad/h/hwram007.bin}
ROOT=$(git -C "$(dirname "$0")" rev-parse --show-toplevel)
for i in $(seq 1 ${N:-60}); do
  out=$(python3 "$ROOT/SaturnExplorer/Docs/Decompiler/tools/a11_round2/mutate.py" --root "$ROOT" --build "$ROOT/SaturnExplorer/build" \
        --rev "$S/rev" --out "$S/mut" --hwram "$HW" --group "$G" --minutes 12 "$@" 2>&1)
  echo "$out"
  echo "$out" | grep -q "nothing left" && break
  echo "$out" | grep -q "not clean" && exit 1
  git -C "$ROOT" status --porcelain -- SaturnExplorer/FrontEnd | grep -q . && { echo "tree not clean"; exit 1; }
  cp "$S/mut/results.tsv" "$ROOT/SaturnExplorer/Docs/Decompiler/tools/a11_round2/results.tsv"
  git -C "$ROOT" add SaturnExplorer/Docs/Decompiler/tools/a11_round2/results.tsv && git -C "$ROOT" commit -q -m "WIP area 11 round 2: mutation results so far" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_015yhSVBbgN7po8UTqDDxHqd" -- SaturnExplorer/Docs/Decompiler/tools/a11_round2/results.tsv
done
