#!/bin/bash
# usage: GEN=<SaturnExplorerSh2GeneratedTests> runsw.sh <first seed> <count> <scratch dir>   -> the shape and the generator's verdict per module that has findings
here=$(cd "$(dirname "$0")" && pwd)
first=$1; count=$2; scratch=$3
for ((s=first; s<first+count; s++)); do
  out=$(python3 -I "$here/swtab11.py" $s 2>"$scratch/shape.txt") || { echo "seed $s: generation failed"; continue; }
  W=${out%%|*}; rest=${out#*|}; F=${rest%%|*}; T=${rest#*|}
  res=$( ulimit -v 4000000; SE_GENERATED_WORDS="$W" SE_GENERATED_FUNCS="$F" SE_GENERATED_TABLES="$T" timeout 120 "$GEN" "$scratch/w" 2>&1 )
  if echo "$res" | grep -q "all passed"; then :; else echo "== seed $s $(cat $scratch/shape.txt)"; echo "$res" | grep -E "^\s+\[|finding|CHECK|crash|rror" | head -8; echo "   WORDS=\"$W\" FUNCS=\"$F\" TABLES=\"$T\""; fi
done
