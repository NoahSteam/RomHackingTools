#!/bin/bash
# usage: fam16.sh <SaturnExplorerSh2GeneratedTests> <out dir> <functions per family> <first seed> <families...>
# Families of dagen16.py (86..90) through the printed-C differential, 200 functions per run. <out>/summary.txt gets one line per family and chunk of
# 2,000; a finished chunk leaves <label>.done and is skipped on a restart. Extra environment (SE_RULECHECK=1 SE_WHOLEFN=1 ...) is passed through.
here=$(cd "$(dirname "$0")" && pwd)
gen=$1; out=$2; n=$3; first=$4; shift 4; fams="$@"
mkdir -p $out
for fam in $fams; do
  for ((c=first; c<first+n; c+=2000)); do
    label=fam${fam}_$c; [ -f $out/$label.done ] && continue
    m=2000; ((c+m>first+n)) && m=$((first+n-c))
    : > $out/$label.log
    for ((s=c; s<c+m; s+=200)); do
      k=200; ((s+k>c+m)) && k=$((c+m-s))
      W=$(python3 -I $here/dagen16.py $s $k $fam)
      ( ulimit -v 3500000; SE_GENERATED_PRINTEDC=only SE_GENERATED_PRINTEDC_WORDS="$W" timeout 900 nice $gen $out/w$fam >> $out/$label.log 2>&1 )
    done
    echo "$label: $(grep -h 'printed-C:' $out/$label.log | sed 's/.*classes: //; s/skipped://g' | tr ' ' '\n' | awk -F= 'NF==2{a[$1]+=$2} END{for(k in a) printf "%s=%d ", k, a[k]}') BAD=$(grep -c 'RULECHECK-BAD\|WHOLEFN-BAD' $out/$label.log)" >> $out/summary.txt
    touch $out/$label.done
  done
done
