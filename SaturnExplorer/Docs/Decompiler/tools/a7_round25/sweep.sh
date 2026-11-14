#!/bin/bash
# Area 7 round 25 sweeps: usage sweep.sh <generator binary> <scratch dir> [tag suffix]
# 10,000 seeds each of grammars 1 and 2 from 10,200,001 (canary and shrink off).
# SE_A7_RULE_OFF passes through (the relaxation-width run sets it).
B="$1"; S="$2"; X="${3:-}"
ulimit -v 6000000
for cfg in "1 10200001 10000" "2 10200001 10000"; do
  set -- $cfg
  tag="g$1$X"
  env SE_GENERATED_GRAMMAR=$1 SE_GENERATED_FIRST=$2 SE_GENERATED_SEEDS=$3 SE_GENERATED_CANARY=0 SE_GENERATED_SHRINK=0 "$B" "$S/out_$tag" > "$S/sweep_$tag.txt" 2>&1
  echo "$tag: $(grep -E 'failing seeds=' "$S/sweep_$tag.txt" | grep -o 'failing seeds=[0-9]*; findings=[0-9]*')"
  rm -rf "$S/out_$tag"
done
