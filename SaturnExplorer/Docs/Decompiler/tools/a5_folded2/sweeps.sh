#!/bin/bash
# The routed-folded-2 sweeps: usage sweeps.sh <worktree SaturnExplorer dir> <scratch dir>
# 10,000 seeds each of grammars 1 and 2 from 8,200,001, and 5,000 specials from 8,300,001 (canary and shrink off).
B="$1/build/bin/SaturnExplorerSh2GeneratedTests"; S="$2"
for cfg in "1 - 8200001 10000" "2 - 8200001 10000" "1 specials 8300001 5000"; do
  set -- $cfg
  tag="g$1${2/-/}"
  env SE_GENERATED_GRAMMAR=$1 ${3:+SE_GENERATED_FIRST=$3} SE_GENERATED_SEEDS=$4 SE_GENERATED_CANARY=0 SE_GENERATED_SHRINK=0 \
      $( [ "$2" = specials ] && echo SE_GENERATED_ALPHABET=specials ) "$B" "$S/out_$tag" > "$S/sweep_$tag.txt" 2>&1
  echo "$tag: $(grep -E 'failing seeds=' "$S/sweep_$tag.txt" | grep -o 'failing seeds=[0-9]*; findings=[0-9]*')"
done
