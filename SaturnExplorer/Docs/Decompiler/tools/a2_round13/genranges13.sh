#!/bin/bash
# usage: genranges13.sh <SaturnExplorerSh2GeneratedTests> <out dir>
# Round 13's three ranges beyond the nightly 1..20000 and the ranges of rounds 11 and 12: grammar 2 2,300,001-2,320,000, grammar 1 2,300,001-2,310,000,
# printed-C 2,300,001-2,315,000, in chunks of 5,000 seeds (each under 3 minutes). A finished chunk leaves <label>.done and is skipped when the script
# is run again, so a restart resumes.
gen=$1; out=$2; mkdir -p "$out"
here=$(cd "$(dirname "$0")" && pwd)
run() { label=$1; gr=$2; first=$3; n=$4; pc=$5
  [ -f "$out/$label.done" ] && return
  "$here/../a2_round10/gen_range.sh" "$gen" "$out" "$label" "$gr" "$first" "$n" "$pc" | tee -a "$out/summary.txt"
  touch "$out/$label.done"; }
for first in 2300001 2305001 2310001 2315001; do run g2_$first 2 $first 5000 ""; done
for first in 2300001 2305001; do run g1_$first 1 $first 5000 ""; done
for first in 2300001 2305001 2310001; do run pc_$first 0 $first 5000 1; done
echo ALLDONE >> "$out/summary.txt"
