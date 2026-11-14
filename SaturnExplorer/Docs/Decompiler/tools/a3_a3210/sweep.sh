#!/bin/bash
# Area 3, A11-33: the sweep of the caution grammar's range (grammar 2 seeds from kCautionFirst, 12,000,001; grammar 1 the same seeds, which
# the caution productions do not reach), canary, shrink and pins off. SE_GENERATED_CAUTION=deep also lists the later-callee misses (counted
# otherwise). usage: sweep.sh <build dir> <scratch dir> [first=12000001] [count=10000] [grammars="2 1"]
# Prints each run's failing seeds, finding classes and the caution counts.
set -u
B=${1:?build dir}; OUT=${2:?scratch dir}; FIRST=${3:-12000001}; N=${4:-10000}; GS=${5:-"2 1"}
mkdir -p "$OUT"
for g in $GS; do
    log="$OUT/sweep_g$g.log"
    ( ulimit -v 3000000
      SE_GENERATED_PINS=0 SE_GENERATED_GRAMMAR=$g SE_GENERATED_FIRST=$FIRST SE_GENERATED_SEEDS=$N SE_GENERATED_CANARY=0 SE_GENERATED_SHRINK=0 \
          "$B/bin/SaturnExplorerSh2GeneratedTests" "$OUT/run_g$g" > "$log" 2>&1 )
    echo "grammar $g: $(grep -o 'failing seeds=[0-9]*; findings=[0-9]*' "$log" | tail -1)"
    grep -o '^FINDING seed [0-9]*' "$log" | awk '{print "  seed " $3}'
    grep '^finding classes' "$log" | tail -1
    grep '^checks:' "$log" | tr ' ' '\n' | grep '^caution\.' | tr '\n' ' '; echo
    grep '^shapes:' "$log" | tr ' ' '\n' | grep -E '^(caution\.|fn\.thunk)' | tr '\n' ' '; echo
done
