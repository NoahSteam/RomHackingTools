#!/bin/bash
# usage: genranges17.sh <SaturnExplorerSh2GeneratedTests> <out dir>
# The ranges of round 17: grammar 2 4,500,001-4,520,000, grammar 1 4,500,001-4,510,000, printed-C 4,500,001-4,515,000 (20,000+ functions), in chunks of 5,000 seeds (each under 3 minutes for the grammars). A finished chunk leaves <label>.done and is skipped when the script
# is run again, so a restart resumes.
gen=$1; out=$2; mkdir -p "$out"
here=$(cd "$(dirname "$0")" && pwd)
run() { label=$1; gr=$2; first=$3; n=$4; pc=$5
  [ -f "$out/$label.done" ] && return
  "$here/../a2_round10/gen_range.sh" "$gen" "$out" "$label" "$gr" "$first" "$n" "$pc" | tee -a "$out/summary.txt"
  touch "$out/$label.done"; }
for first in 4500001 4505001 4510001 4515001; do run g2_$first 2 $first 5000 ""; done
for first in 4500001 4505001; do run g1_$first 1 $first 5000 ""; done
for first in 4500001 4505001 4510001; do run pc_$first 0 $first 5000 1; done
echo ALLDONE >> "$out/summary.txt"
