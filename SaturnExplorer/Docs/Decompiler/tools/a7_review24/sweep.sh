#!/bin/bash
# Area 7 review round 24 sweeps: usage sweep.sh <generator binary> <scratch dir> [tag suffix]
# 10,000 seeds each of grammars 1 and 2 from 9,900,001, and 5,000 grammar 2 specials from 10,000,001 (canary and shrink off).
# SE_A7_RULE_OFF passes through (the relaxation-width run sets it).
B="$1"; S="$2"; X="${3:-}"
ulimit -v 6000000
for cfg in "1 - 9900001 10000" "2 - 9900001 10000" "2 specials 10000001 5000"; do
  set -- $cfg
  tag="g$1${2/-/}$X"
  env SE_GENERATED_GRAMMAR=$1 SE_GENERATED_FIRST=$3 SE_GENERATED_SEEDS=$4 SE_GENERATED_CANARY=0 SE_GENERATED_SHRINK=0 \
      $( [ "$2" = specials ] && echo SE_GENERATED_ALPHABET=specials ) "$B" "$S/out_$tag" > "$S/sweep_$tag.txt" 2>&1
  echo "$tag: $(grep -E 'failing seeds=' "$S/sweep_$tag.txt" | grep -o 'failing seeds=[0-9]*; findings=[0-9]*')"
  rm -rf "$S/out_$tag"
done
