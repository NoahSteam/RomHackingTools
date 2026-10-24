#!/bin/bash
# usage: pcsweep14.sh <SaturnExplorerSh2GeneratedTests> <out dir> <functions per family> <first seed> <families...>
# The printed-C differential over the round 15 families (dagen15.py), batches of 200 functions; <out>/fam<n>.log per family, <out>/summary.txt one line each.
here=$(cd "$(dirname "$0")" && pwd)
gen=$1; out=$2; n=$3; first=$4; shift 4; fams="$@"
mkdir -p $out
for fam in $fams; do
  [ -f $out/fam$fam.done ] && continue
  for ((s=first; s<first+n; s+=200)); do
    m=200; ((s+m>first+n)) && m=$((first+n-s))
    W=$(python3 -I $here/dagen15.py $s $m $fam)
    ( ulimit -v 4000000; SE_GENERATED_PRINTEDC=only SE_GENERATED_PRINTEDC_WORDS="$W" timeout 900 nice $gen $out/w$fam >> $out/fam$fam.log 2>&1 )
  done
  echo "fam $fam: $(grep 'printed-C:' $out/fam$fam.log | sed 's/ inputs each=32//' | cut -c1-160 | tr '\n' ' ') FINDINGS=$(grep -c 'mismatch\|FINDING' $out/fam$fam.log)" >> $out/summary.txt
  touch $out/fam$fam.done
done
