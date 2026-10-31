#!/bin/bash
# usage: fam17.sh <SaturnExplorerSh2GeneratedTests> <out dir> <functions per family> <first seed> <families...>
# Families of dagen17.py (and, through it, dagen16.py's 86..99) through the printed-C differential, 200 functions per run. <out>/summary.txt gets one line per family and
# chunk of 2,000; a finished chunk leaves <label>.done and is skipped on a restart. Memory families (91..94, 97, 104..106) are run at each access width in $WIDTHS
# (default "4 2 1", one width of every access: DAGEN16_W; SE_GENERATED_PRINTEDC_PTR; BE=1 reads them on a big-endian arena, SE_GENERATED_PRINTEDC_BE). Extra environment is passed through.
here=$(cd "$(dirname "$0")" && pwd)
gen=$1; out=$2; n=$3; first=$4; shift 4; fams="$@"
mkdir -p $out
for fam in $fams; do
  mem=0; case $fam in 91|92|93|94|97|104|105|106|110|111) mem=1;; esac
  widths="0"; [ $mem = 1 ] && widths="${WIDTHS:-4 2 1}"
  for w in $widths; do
  for ((c=first; c<first+n; c+=2000)); do
    label=fam${fam}_w${w}_$c; [ -f $out/$label.done ] && continue
    m=2000; ((c+m>first+n)) && m=$((first+n-c))
    : > $out/$label.log
    for ((s=c; s<c+m; s+=200)); do
      k=200; ((s+k>c+m)) && k=$((c+m-s))
      W=$(DAGEN16_W=$w python3 -I $here/dagen17.py $s $k $fam)
      pt=""; [ $mem = 1 ] && pt="SE_GENERATED_PRINTEDC_PTR=$w"; [ $mem = 1 ] && [ -n "$BE" ] && pt="$pt SE_GENERATED_PRINTEDC_BE=1"
      ( ulimit -v 3500000; env $pt SE_GENERATED_PRINTEDC=only SE_GENERATED_PRINTEDC_WORDS="$W" timeout 900 nice $gen $out/w$fam >> $out/$label.log 2>&1 )
    done
    echo "$label: $(grep -h 'printed-C:' $out/$label.log | sed 's/.*classes: //; s/skipped://g' | tr ' ' '\n' | awk -F= 'NF==2{a[$1]+=$2} END{for(k in a) printf "%s=%d ", k, a[k]}') BAD=$(grep -c 'RULECHECK-BAD\|WHOLEFN-BAD' $out/$label.log)" >> $out/summary.txt
    touch $out/$label.done
  done
  done
done
