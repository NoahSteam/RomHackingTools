#!/bin/bash
# usage: pcw.sh label "python cmd printing words for (start,count)"... simpler: pcw.sh label file(list of "name words") [env...]
# runs the functions 150 at a time through the printed-C differential, verbose; log in fam/label.log
S=${S:?set S to a scratch directory}
W=${W:?set W to the SaturnExplorer directory}
label=$1; list=$2; shift 2
mkdir -p $S/fam; : > $S/fam/$label.log
n=$(wc -l < $list)
for ((s=1; s<=n; s+=150)); do
  words=$(sed -n "${s},$((s+149))p" $list | cut -d' ' -f2- | paste -sd';')
  D=$(mktemp -d $S/fam/w.XXXX)
  ( ulimit -v 3500000; env "$@" SE_GENERATED_PRINTEDC=only SE_GENERATED_VERBOSE=1 SE_GENERATED_PRINTEDC_WORDS="$words" timeout 1000 nice $W/build/bin/SaturnExplorerSh2GeneratedTests $D >> $S/fam/$label.log 2>&1 )
  rm -rf $D
done
echo "$label: $(grep -h 'printed-C:' $S/fam/$label.log | sed 's/.*classes: //; s/skipped://g' | tr ' ' '\n' | awk -F= 'NF==2{a[$1]+=$2} END{for(k in a) printf "%s=%d ", k, a[k]}')" | tee -a $S/fam/summary.txt
