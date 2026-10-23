#!/bin/bash
# usage: famrc13.sh <SaturnExplorerSh2GeneratedTests relinked with the in-situ check (relink13.sh)> <out dir> <functions per family> [first seed] [families...]
# Every family (a2_round10 dagen10.py 0-19, a2_round11 dagen11.py 20-28 and dagen11b.py 29-38, a2_round12 dagen12.py 40-54, a2_round13 dagen13.py 55-58)
# through the printed-C differential with SE_RULECHECK=1. <out dir>/fam<n>.log holds the run; <out dir>/summary.txt one line per family: the printed-C line and
# the number of RULECHECK-BAD lines. A finished family leaves fam<n>.done, so a restart resumes.
# Sum the rule tallies with: cat <out dir>/fam*.log | grep '^RULECHECK ' | awk '{a[$2]+=$4; c[$2]+=$6; b[$2]+=$8} END{for(k in a) print k, a[k], c[k], b[k]}'
here=$(cd "$(dirname "$0")" && pwd); D=$here/..
gen=$1; out=$2; n=$3; first=${4:-1}; shift 4 2>/dev/null; fams="$@"
[ -z "$fams" ] && fams="$(seq 0 38) $(seq 40 54) $(seq 55 58)"
mkdir -p $out
fam_script() { case $1 in [0-9]|1[0-9]) echo $D/a2_round10/dagen10.py;; 2[0-8]) echo $D/a2_round11/dagen11.py;; 29|3[0-8]) echo $D/a2_round11/dagen11b.py;; 5[5-9]) echo $D/a2_round13/dagen13.py;; *) echo $D/a2_round12/dagen12.py;; esac; }
for fam in $fams; do
  [ -f $out/fam$fam.done ] && continue
  for ((s=first; s<first+n; s+=100)); do
    m=100; ((s+m>first+n)) && m=$((first+n-s))
    W=$(python3 -I $(fam_script $fam) $s $m $fam)
    ( ulimit -v 4000000; SE_RULECHECK=1 SE_GENERATED_PRINTEDC=only SE_GENERATED_PRINTEDC_WORDS="$W" timeout 900 nice $gen $out/w$fam >> $out/fam$fam.log 2>&1 )
  done
  echo "fam $fam: $(grep 'printed-C:' $out/fam$fam.log | cut -c1-150 | head -3 | tr '\n' ' ') BAD=$(grep -c 'RULECHECK-BAD' $out/fam$fam.log)" >> $out/summary.txt
  touch $out/fam$fam.done
done
