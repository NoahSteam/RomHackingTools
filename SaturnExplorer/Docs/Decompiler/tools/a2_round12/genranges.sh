#!/bin/bash
# usage: genranges.sh <SaturnExplorerSh2GeneratedTests> <out dir>
# Round 12's three ranges beyond the nightly 1..20000 (and round 11's 1,400,001..): grammar 2 1,500,001-1,520,000, grammar 1
# 1,500,001-1,510,000, printed-C 1,500,001-1,515,000, in chunks of 5,000 seeds (each under 3 minutes). A finished chunk leaves <label>.done
# and is skipped when the script is run again, so a restart resumes.
gen=$1; out=$2; mkdir -p "$out"
here=$(cd "$(dirname "$0")" && pwd)
run() { label=$1; gr=$2; first=$3; n=$4; pc=$5
  [ -f "$out/$label.done" ] && return
  "$here/../a2_round10/gen_range.sh" "$gen" "$out" "$label" "$gr" "$first" "$n" "$pc" | tee -a "$out/summary.txt"
  touch "$out/$label.done"; }
for first in 1500001 1505001 1510001 1515001; do run g2_$first 2 $first 5000 ""; done
for first in 1500001 1505001; do run g1_$first 1 $first 5000 ""; done
for first in 1500001 1505001 1510001; do run pc_$first 0 $first 5000 1; done
echo ALLDONE >> "$out/summary.txt"
