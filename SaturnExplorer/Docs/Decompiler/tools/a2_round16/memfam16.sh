#!/bin/bash
# usage: memfam16.sh <SaturnExplorerSh2GeneratedTests (the memory mode is part of it since A2-106; mkptr16.py's scratch copy is no longer needed)> <out dir> <functions per family and width> <first seed> [families (default 91 92 93)]
# The memory mode of the printed-C differential: families 91 to 93 of dagen16.py at each access width (4, 2, 1), 200 functions per run. <out>/summary.txt gets one line per
# family, width and chunk of 2,000; a finished chunk leaves a .done and is skipped on a restart.
here=$(cd "$(dirname "$0")" && pwd)
gen=$1; out=$2; n=$3; first=$4; shift 4; fams="${@:-91 92 93}"
mkdir -p $out
for w in 4 2 1; do for fam in $fams; do
  for ((c=first; c<first+n; c+=2000)); do
    label=mem${fam}_w${w}_$c; [ -f $out/$label.done ] && continue
    m=2000; ((c+m>first+n)) && m=$((first+n-c))
    : > $out/$label.log
    for ((s=c; s<c+m; s+=200)); do
      k=200; ((s+k>c+m)) && k=$((c+m-s))
      W=$(DAGEN16_W=$w python3 -I $here/dagen16.py $s $k $fam)
      ( ulimit -v 3500000; SE_GENERATED_PRINTEDC_PTR=$w SE_GENERATED_PRINTEDC=only SE_GENERATED_PRINTEDC_WORDS="$W" timeout 900 nice $gen $out/w$fam >> $out/$label.log 2>&1 )
    done
    echo "$label: $(grep -h 'printed-C:' $out/$label.log | sed 's/.*classes: //; s/skipped://g' | tr ' ' '\n' | awk -F= 'NF==2{a[$1]+=$2} END{for(k in a) printf "%s=%d ", k, a[k]}')" >> $out/summary.txt
    touch $out/$label.done
  done
done; done
