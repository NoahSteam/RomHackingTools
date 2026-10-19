#!/bin/bash
# usage: famcensus.sh <SaturnExplorerSh2GeneratedTests built with census12.patch> <out dir> <functions per family> [first seed]
# Every family (a2_round10 dagen10.py 0-19, a2_round11 dagen11.py 20-28 and dagen11b.py 29-38, a2_round12 dagen12.py 40-44) through the printed-C
# differential with SE_CENSUS=1: the printed-C line of each family, and in <out dir>/fam<n>.log the `CENSUS rule:<name> <count>` lines, the
# rules that applied. Sum them with: cat <out dir>/fam*.log | grep '^CENSUS rule:' | awk '{a[$2]+=$3} END{for(k in a) print a[k], k}' | sort -rn
here=$(cd "$(dirname "$0")" && pwd); D=$here/..
gen=$1; out=$2; n=$3; first=${4:-1}; mkdir -p $out
fam_script() { case $1 in [0-9]|1[0-9]) echo $D/a2_round10/dagen10.py;; 2[0-8]) echo $D/a2_round11/dagen11.py;; 29|3[0-8]) echo $D/a2_round11/dagen11b.py;; *) echo $D/a2_round12/dagen12.py;; esac; }
for fam in $(seq 0 38) $(seq 40 44); do
  W=$(python3 -I $(fam_script $fam) $first $n $fam)
  ( ulimit -v 4000000; SE_CENSUS=1 SE_GENERATED_PRINTEDC=only SE_GENERATED_PRINTEDC_WORDS="$W" timeout 900 nice $gen $out/w$fam > $out/fam$fam.log 2>&1 )
  echo "fam $fam: $(grep 'printed-C:' $out/fam$fam.log | cut -c1-160)"
done
