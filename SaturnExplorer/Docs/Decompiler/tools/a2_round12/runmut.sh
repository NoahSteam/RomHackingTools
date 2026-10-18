#!/bin/bash
# usage: runmut.sh <instrumented SaturnExplorerSh2GeneratedTests (mutate12.py applied)> <scratch dir> <count per family> <mutant>:<family,family,...> ...
# For each mutant, the printed-C differential on the given families (a2_round10 dagen10.py 0-19, a2_round11 dagen11.py 20-28 and dagen11b.py
# 29-38, a2_round12 dagen12.py 40-43), SE_MUT=<mutant>. Prints the number of functions with a verdict other than agrees / narrow-c /
# skipped / void-return for the mutant, per family: a mutant that no family kills is a rule the families do not test.
gen=$1; scratch=$2; n=$3; shift 3
here=$(cd "$(dirname "$0")" && pwd); D=$here/..
fam_script() { case $1 in [0-9]|1[0-9]) echo $D/a2_round10/dagen10.py;; 2[0-8]) echo $D/a2_round11/dagen11.py;; 29|3[0-8]) echo $D/a2_round11/dagen11b.py;; *) echo $D/a2_round12/dagen12.py;; esac; }
mkdir -p $scratch
for spec in "$@"; do
  m=${spec%%:*}; fams=${spec#*:}; tot=0; line=""
  for fam in ${fams//,/ }; do
    sc=$(fam_script $fam); W=$(python3 -I $sc 1 $n $fam)
    out=$( ulimit -v 4000000; SE_MUT=$m SE_GENERATED_PRINTEDC=only SE_GENERATED_PRINTEDC_WORDS="$W" timeout 900 nice $gen $scratch/w 2>&1 | grep "printed-C:" | sed 's/.*classes: //' | tr ' ' '\n' | grep -v -E "^(agrees|narrow-c|void-return|void-return-lent|skipped:[a-z-]*)=" | sed 's/.*=//' | paste -sd+ | bc )
    [ -z "$out" ] && out=0
    line="$line f$fam=$out"; tot=$((tot+out))
  done
  echo "mutant $m: killed by $tot functions ($line )"
done
