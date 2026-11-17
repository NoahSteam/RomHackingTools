#!/bin/bash
# Area 7 P2 fixes: the sweeps, 10,000 seeds each of grammars 1 and 2 from 10,600,001, canary and shrink off, one after the other (not beside
# ctest). usage: sweep.sh <generator binary> <scratch dir> [tag suffix]   (the mutation build's SE_P2_OFF189 / SE_P2_OFF190 pass through)
B="$1"; S="$2"; X="${3:-}"
ulimit -v 6000000
for g in 1 2; do
  tag="g${g}_10600001$X"
  env SE_GENERATED_GRAMMAR=$g SE_GENERATED_FIRST=10600001 SE_GENERATED_SEEDS=10000 SE_GENERATED_CANARY=0 SE_GENERATED_SHRINK=0 "$B" "$S/out_$tag" > "$S/sweep_$tag.txt" 2>&1
  echo "$tag: $(grep -o 'failing seeds=[0-9]*; findings=[0-9]*' "$S/sweep_$tag.txt")"
  grep -E '^FINDING|^ *\[[a-z]:' "$S/sweep_$tag.txt" | cut -c1-200
  rm -rf "$S/out_$tag"
done
