#!/bin/bash
gen=${W:?set W to the SaturnExplorer directory}/build/bin/SaturnExplorerSh2GeneratedTests
out=${S:?set S to a scratch directory}/gen
run() { label=$1; shift; first=$1; shift
  [ -f "$out/$label.done" ] && return
  start=$(date +%s)
  ( ulimit -v 4000000; env "$@" SE_GENERATED_FIRST=$first SE_GENERATED_SEEDS=5000 timeout 1150 $gen "$out/w_$label" > "$out/$label.log" 2>&1 ); rc=$?
  echo "$label: exit $rc, $(( $(date +%s) - start )) s, $(grep -c 'FINDING' "$out/$label.log") FINDING, $(tail -3 "$out/$label.log" | tr '\n' ' ' | cut -c1-300)" >> $out/summary.txt
  rm -rf "$out/w_$label"; touch "$out/$label.done"; }
for f in 2200001 2205001 2210001 2215001; do run g2_$f $f SE_GENERATED_GRAMMAR=2; done
for f in 2200001 2205001; do run g1_$f $f SE_GENERATED_GRAMMAR=1; done
for f in 2200001 2205001 2210001; do run pc_$f $f SE_GENERATED_PRINTEDC=only; done
run sp_2250001 2250001 SE_GENERATED_GRAMMAR=2 SE_GENERATED_ALPHABET=specials
echo ALLDONE >> $out/summary.txt
