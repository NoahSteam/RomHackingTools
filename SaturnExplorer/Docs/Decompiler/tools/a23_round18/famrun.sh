#!/bin/bash
# Area 2 round 18 / area 3 round 17 reviewer. W = the SaturnExplorer directory (its build/ holds the binaries); S = a scratch directory where
# the scripts and asm.py/mk.py of ../a3_round16 sit (the driver drv is built there by ../a3_round16/bdrv16.sh).
# usage: famrun.sh <label> <python generator script> <total> [env...]: runs the generator's functions 200 at a time through the printed-C differential
S=${S:-$(cd "$(dirname "$0")" && pwd)}
gen=${W:?set W to the SaturnExplorer directory}/build/bin/SaturnExplorerSh2GeneratedTests
label=$1; py=$2; total=$3; shift 3
out=$S/fam; mkdir -p $out
[ -f $out/$label.done ] && exit 0
: > $out/$label.log
for ((s=0; s<total; s+=200)); do
  WORDS=$(cd $S && python3 $py $s 200)
  [ -z "$WORDS" ] && continue
  ( ulimit -v 3500000; env "$@" SE_GENERATED_PRINTEDC=only SE_GENERATED_PRINTEDC_WORDS="$WORDS" timeout 900 nice $gen $out/w$label >> $out/$label.log 2>&1 )
done
echo "$label: $(grep -h 'printed-C:' $out/$label.log | sed 's/.*classes: //; s/skipped://g' | tr ' ' '\n' | awk -F= 'NF==2{a[$1]+=$2} END{for(k in a) printf "%s=%d ", k, a[k]}')" >> $out/summary.txt
rm -rf $out/w$label; touch $out/$label.done
