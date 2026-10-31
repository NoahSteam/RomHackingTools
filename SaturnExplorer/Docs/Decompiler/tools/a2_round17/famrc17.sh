#!/bin/bash
# usage: famrc17.sh <SaturnExplorerSh2GeneratedTests relinked with the in-situ check (mkrc17.sh, relink17.sh)> <out dir> <functions per family> <first seed> <families...>
# The round 14 in-situ check (every rule application and every leaf action checked by value on the function, SE_RULECHECK/SE_WHOLEFN/SE_WHOLEFN_ACTIONS) over any family of the chain
# dagen10..dagen17, 100 functions per run. <out>/fam<n>.log, <out>/summary.txt one line per family (the printed-C line, RULECHECK-BAD and WHOLEFN-BAD counts).
here=$(cd "$(dirname "$0")" && pwd); D=$here/..
gen=$1; out=$2; n=$3; first=$4; shift 4; fams="$@"
fam_script() { case $1 in [0-9]|1[0-9]) echo $D/a2_round10/dagen10.py;; 2[0-8]) echo $D/a2_round11/dagen11.py;; 29|3[0-8]) echo $D/a2_round11/dagen11b.py;; 4[0-9]|5[0-4]) echo $D/a2_round12/dagen12.py;;
  5[5-9]|6[01]) echo $D/a2_round13/dagen13.py;; 6[2-9]|7[0-8]) echo $D/a2_round14/dagen14.py;; 79|8[0-5]) echo $D/a2_round15/dagen15.py;; 8[6-9]|9[0-9]) echo $D/a2_round16/dagen16.py;; *) echo $here/dagen17.py;; esac; }
mkdir -p $out
for fam in $fams; do
  [ -f $out/fam$fam.done ] && continue
  : > $out/fam$fam.log
  for ((s=first; s<first+n; s+=100)); do
    m=100; ((s+m>first+n)) && m=$((first+n-s))
    W=$(python3 -I $(fam_script $fam) $s $m $fam)
    ( ulimit -v 4000000; SE_RULECHECK=1 SE_WHOLEFN=1 SE_WHOLEFN_ACTIONS=1 SE_GENERATED_PRINTEDC=only SE_GENERATED_PRINTEDC_WORDS="$W" timeout 900 nice $gen $out/w$fam >> $out/fam$fam.log 2>&1 )
  done
  echo "fam $fam: $(grep -h 'printed-C:' $out/fam$fam.log | sed 's/.*classes: //' | tr ' ' '\n' | awk -F= 'NF==2{a[$1]+=$2} END{for(k in a) printf "%s=%d ", k, a[k]}') RULEBAD=$(grep -c 'RULECHECK-BAD' $out/fam$fam.log) WFBAD=$(grep -c 'WHOLEFN-BAD' $out/fam$fam.log)" >> $out/summary.txt
  touch $out/fam$fam.done
done
