#!/bin/bash
# usage: oldnew17.sh <old SaturnExplorerSh2GeneratedTests> <new SaturnExplorerSh2GeneratedTests> <out dir> <functions per family> <first seed> [families...]
# The same words through two engines (here: the tree before patches 0072, 0073 and 0075 and the tree with them), 200 functions per run. <out>/old<fam>.log and new<fam>.log are the
# printed-C differential logs; <out>/summary.txt has diff17.py's count per family (identical text and verdict / different). Families 0..109 (default: all but 39).
here=$(cd "$(dirname "$0")" && pwd); D=$here/..
old=$1; new=$2; out=$3; n=$4; first=$5; shift 5; fams="$@"
[ -z "$fams" ] && fams="$(seq 0 38) $(seq 40 85) $(seq 86 99) $(seq 100 103) 107 108 109"
fam_script() { case $1 in [0-9]|1[0-9]) echo $D/a2_round10/dagen10.py;; 2[0-8]) echo $D/a2_round11/dagen11.py;; 29|3[0-8]) echo $D/a2_round11/dagen11b.py;; 4[0-9]|5[0-4]) echo $D/a2_round12/dagen12.py;;
  5[5-9]|6[01]) echo $D/a2_round13/dagen13.py;; 6[2-9]|7[0-8]) echo $D/a2_round14/dagen14.py;; 79|8[0-5]) echo $D/a2_round15/dagen15.py;; 8[6-9]|9[0-9]) echo $D/a2_round16/dagen16.py;; *) echo $here/dagen17.py;; esac; }
mkdir -p $out
for fam in $fams; do
  [ -f $out/fam$fam.done ] && continue
  : > $out/old$fam.log; : > $out/new$fam.log
  for ((s=first; s<first+n; s+=200)); do
    m=200; ((s+m>first+n)) && m=$((first+n-s))
    W=$(python3 -I $(fam_script $fam) $s $m $fam)
    ( ulimit -v 3500000; SE_GENERATED_PRINTEDC=only SE_GENERATED_PRINTEDC_WORDS="$W" timeout 900 nice $old $out/wo$fam >> $out/old$fam.log 2>&1 )
    ( ulimit -v 3500000; SE_GENERATED_PRINTEDC=only SE_GENERATED_PRINTEDC_WORDS="$W" timeout 900 nice $new $out/wn$fam >> $out/new$fam.log 2>&1 )
  done
  echo "fam $fam: $(python3 -I $here/diff17.py $out/old$fam.log $out/new$fam.log 0 | head -1) | new: $(grep -h 'printed-C:' $out/new$fam.log | sed 's/.*classes: //' | tr '\n' ' ')" >> $out/summary.txt
  touch $out/fam$fam.done
done
