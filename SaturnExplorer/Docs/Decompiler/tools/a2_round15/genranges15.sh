#!/bin/bash
# usage: genranges15.sh <SaturnExplorerSh2GeneratedTests> <out dir>
# Round 15's three ranges, well past rounds 11 to 14: grammar 2 3,800,001-3,820,000, grammar 1 3,800,001-3,810,000, printed-C 3,800,001-3,815,000,
# in chunks of 5,000 seeds (each under 3 minutes). A finished chunk leaves <label>.done and is skipped when the script
# is run again, so a restart resumes.
gen=$1; out=$2; mkdir -p "$out"
here=$(cd "$(dirname "$0")" && pwd)
run() { label=$1; gr=$2; first=$3; n=$4; pc=$5
  [ -f "$out/$label.done" ] && return
  "$here/../a2_round10/gen_range.sh" "$gen" "$out" "$label" "$gr" "$first" "$n" "$pc" | tee -a "$out/summary.txt"
  touch "$out/$label.done"; }
for first in 3800001 3805001 3810001 3815001; do run g2_$first 2 $first 5000 ""; done
for first in 3800001 3805001; do run g1_$first 1 $first 5000 ""; done
for first in 3800001 3805001 3810001; do run pc_$first 0 $first 5000 1; done
echo ALLDONE >> "$out/summary.txt"
