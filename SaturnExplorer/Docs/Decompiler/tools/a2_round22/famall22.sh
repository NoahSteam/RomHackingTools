#!/bin/bash
# famall.sh <out dir> <functions per family> <first seed> <families...>: the round 10-17 families through the printed-C differential, 100 per run
T=$(cd "$(dirname "$0")/.." && pwd)
gen=${GEN:?set GEN to SaturnExplorerSh2GeneratedTests}
out=$1; n=$2; first=$3; shift 3
mkdir -p $out
for fam in "$@"; do
  if   ((fam<=28)); then mod=a2_round11/dagen11.py
  elif ((fam<=38)); then mod=a2_round11/dagen11b.py
  elif ((fam<=54)); then mod=a2_round12/dagen12.py
  elif ((fam<=61)); then mod=a2_round13/dagen13.py
  elif ((fam<=78)); then mod=a2_round14/dagen14.py
  elif ((fam<=85)); then mod=a2_round15/dagen15.py
  else mod=a2_round17/dagen17.py; fi
  mem=0; case $fam in 91|92|93|94|97|104|105|106|110|111) mem=1;; esac
  widths="0"; [ $mem = 1 ] && widths="4 2 1"
  for w in $widths; do
    label=fam${fam}_w$w; [ -f $out/$label.done ] && continue
    : > $out/$label.log
    for ((s=first; s<first+n; s+=100)); do
      k=100; ((s+k>first+n)) && k=$((first+n-s))
      W=$(DAGEN16_W=$w python3 -I $T/$mod $s $k $fam)
      pt=""; [ $mem = 1 ] && pt="SE_GENERATED_PRINTEDC_PTR=$w SE_GENERATED_PRINTEDC_BE=1"
      ( ulimit -v 3500000; env $pt SE_GENERATED_PRINTEDC=only SE_GENERATED_VERBOSE=1 SE_GENERATED_PRINTEDC_WORDS="$W" timeout 1100 nice $gen $out/w$fam >> $out/$label.log 2>&1 ); echo "rc=$?" >> $out/$label.log
    done
    echo "$label: $(grep -h 'printed-C:' $out/$label.log | sed 's/.*classes: //; s/skipped://g' | tr ' ' '\n' | awk -F= 'NF==2{a[$1]+=$2} END{for(k in a) printf "%s=%d ", k, a[k]}') rc=$(grep '^rc=' $out/$label.log | sort | uniq -c | tr '\n' ' ')" >> $out/summary.txt
    rm -rf $out/w$fam; touch $out/$label.done
  done
done
echo ALLDONE >> $out/summary.txt
