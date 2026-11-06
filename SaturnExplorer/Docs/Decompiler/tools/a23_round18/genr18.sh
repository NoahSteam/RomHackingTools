#!/bin/bash
# Area 2 round 18 / area 3 round 17 reviewer. W = the SaturnExplorer directory (its build/ holds the binaries); S = a scratch directory where
# the scripts and asm.py/mk.py of ../a3_round16 sit (the driver drv is built there by ../a3_round16/bdrv16.sh).
# round 18 ranges: chunks of 5,000 seeds, .done markers, one at a time
gen=${W:?set W to the SaturnExplorer directory}/build/bin/SaturnExplorerSh2GeneratedTests
out=${S:?set S}/gen
run() { label=$1; shift; first=$1; shift
  [ -f "$out/$label.done" ] && return
  start=$(date +%s)
  ( ulimit -v 4000000; env "$@" SE_GENERATED_FIRST=$first SE_GENERATED_SEEDS=5000 timeout 1150 $gen "$out/w_$label" > "$out/$label.log" 2>&1 ); rc=$?
  echo "$label: exit $rc, $(( $(date +%s) - start )) s, $(grep -c 'FINDING' "$out/$label.log") FINDING, $(tail -3 "$out/$label.log" | tr '\n' ' ' | cut -c1-300)" >> $out/summary.txt
  rm -rf "$out/w_$label"; touch "$out/$label.done"; }
for f in 1900001 1905001 1910001 1915001; do run g2_$f $f SE_GENERATED_GRAMMAR=2; done
for f in 1900001 1905001; do run g1_$f $f SE_GENERATED_GRAMMAR=1; done
for f in 1900001 1905001 1910001; do run pc_$f $f SE_GENERATED_PRINTEDC=only; done
run sp_1950001 1950001 SE_GENERATED_GRAMMAR=2 SE_GENERATED_ALPHABET=specials
echo ALLDONE >> $out/summary.txt
