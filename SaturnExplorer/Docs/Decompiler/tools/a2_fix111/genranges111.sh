#!/bin/bash
# usage: genranges111.sh <SaturnExplorerSh2GeneratedTests> <out dir>
# The ranges of area 2 fixer: grammar 2 4,900,001-4,920,000, grammar 1 4,900,001-4,910,000, printed-C 4,900,001-4,915,000 (20,000+ functions), in chunks of 5,000 seeds (each under 3 minutes for the grammars). A finished chunk leaves <label>.done and is skipped when the script
# is run again, so a restart resumes.
gen=$1; out=$2; mkdir -p "$out"
here=$(cd "$(dirname "$0")" && pwd)
run() { label=$1; gr=$2; first=$3; n=$4; pc=$5
  [ -f "$out/$label.done" ] && return
  "$here/../a2_round10/gen_range.sh" "$gen" "$out" "$label" "$gr" "$first" "$n" "$pc" | tee -a "$out/summary.txt"
  touch "$out/$label.done"; }
for first in 4900001 4905001 4910001 4915001; do run g2_$first 2 $first 5000 ""; done
for first in 4900001 4905001; do run g1_$first 1 $first 5000 ""; done
for first in 4900001 4905001 4910001; do run pc_$first 0 $first 5000 1; done
run g2_4515001 2 4515001 5000 ""   # the chunk of seed 4515655 (A2-115), which ended without its summary
echo ALLDONE >> "$out/summary.txt"
