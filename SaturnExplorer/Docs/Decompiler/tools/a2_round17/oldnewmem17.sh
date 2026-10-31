#!/bin/bash
# usage: oldnewmem17.sh <old SaturnExplorerSh2GeneratedTests> <new SaturnExplorerSh2GeneratedTests> <out dir> <functions per run (200)> <first seed>
# oldnew17.sh for the memory families (91..94, 97 at widths 4, 2, 1; 104..106, 110, 111 at width 4 / 2) in the committed little-endian memory mode (SE_GENERATED_PRINTEDC_PTR): the texts and
# verdicts of two engines. <out>/summary.txt has diff17.py's count per family and width.
here=$(cd "$(dirname "$0")" && pwd)
old=$1; new=$2; out=$3; n=$4; first=$5
mkdir -p $out
for spec in "91 4" "91 2" "91 1" "92 4" "92 1" "93 4" "93 2" "93 1" "94 4" "94 2" "94 1" "97 4" "97 2" "104 4" "105 4" "106 4" "110 4" "111 2" "111 1"; do
  set -- $spec; fam=$1; w=$2
  W=$(DAGEN16_W=$w python3 -I $here/dagen17.py $first $n $fam)
  ( ulimit -v 3500000; SE_GENERATED_PRINTEDC_PTR=$w SE_GENERATED_PRINTEDC=only SE_GENERATED_PRINTEDC_WORDS="$W" timeout 900 nice $old $out/wo >  $out/old_${fam}_$w.log 2>&1 )
  ( ulimit -v 3500000; SE_GENERATED_PRINTEDC_PTR=$w SE_GENERATED_PRINTEDC=only SE_GENERATED_PRINTEDC_WORDS="$W" timeout 900 nice $new $out/wn >  $out/new_${fam}_$w.log 2>&1 )
  echo "fam $fam w$w: $(python3 -I $here/diff17.py $out/old_${fam}_$w.log $out/new_${fam}_$w.log 0 | head -1) | new: $(grep -h 'printed-C:' $out/new_${fam}_$w.log | sed 's/.*classes: //')" >> $out/summary.txt
done
