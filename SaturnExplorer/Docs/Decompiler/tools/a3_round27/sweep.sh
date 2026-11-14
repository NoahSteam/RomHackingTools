#!/bin/bash
# Area 3 round 27: the sweep of the record's section (round 26's sweep.sh with this round's first seed) (canary and shrink off).
# usage: sweep.sh <build dir> <scratch dir> [first=10100001] [count=10000]
# Runs grammar 1 and grammar 2, one after the other, and prints each run's failing seeds and finding classes.
set -u
B=${1:?build dir}; OUT=${2:?scratch dir}; FIRST=${3:-10100001}; N=${4:-10000}
mkdir -p "$OUT"
for g in 1 2; do
    log="$OUT/sweep_g$g.log"
    ( ulimit -v 6000000
      SE_GENERATED_GRAMMAR=$g SE_GENERATED_FIRST=$FIRST SE_GENERATED_SEEDS=$N SE_GENERATED_CANARY=0 SE_GENERATED_SHRINK=0 \
          "$B/bin/SaturnExplorerSh2GeneratedTests" "$OUT/run_g$g" > "$log" 2>&1 )
    echo "grammar $g: $(grep -o 'failing seeds=[0-9]*; findings=[0-9]*' "$log" | tail -1)"
    grep -o '^FINDING seed [0-9]*' "$log" | awk '{print "  seed " $3}'
    grep '^finding classes' "$log" | tail -1
done
