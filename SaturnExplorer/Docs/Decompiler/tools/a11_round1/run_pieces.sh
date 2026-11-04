#!/bin/bash
# Area 11 round 1: the mutation runs as pieces of at most 20 minutes (mutate.py --minutes 12 stops before a mutant
# once 12 minutes have gone; a mutant takes at most about 6). Resumable: rerun after a restart.
# usage: run_pieces.sh <group> <scratch> [extra mutate.py args]
G=$1; S=$2; shift 2
ROOT=$(git rev-parse --show-toplevel)
for i in $(seq 1 60); do
  out=$(python3 "$ROOT/SaturnExplorer/Docs/Decompiler/tools/a11_round1/mutate.py" --root "$ROOT" --build "$ROOT/SaturnExplorer/build" \
        --rev "$S/rev" --out "$S/mut" --hwram "$S/h/hwram007.bin" --group "$G" --minutes 12 "$@" 2>&1)
  echo "$out"
  echo "$out" | grep -q "nothing left" && break
  echo "$out" | grep -q "not clean" && exit 1
  git -C "$ROOT" status --porcelain -- SaturnExplorer/FrontEnd | grep -q . && { echo "tree not clean"; exit 1; }
done
