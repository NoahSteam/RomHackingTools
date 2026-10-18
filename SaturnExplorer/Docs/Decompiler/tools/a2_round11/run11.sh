#!/bin/bash
# Area 2 round 11 instrument: a function generator's functions through the printed-C differential in batches, with the extra input rows of the
# harness patch (printedc_extra_rows.patch): the literals of the batch and their neighbours as r4/r5, and with BYTES=1 every byte value.
# usage: GEN=<SaturnExplorerSh2GeneratedTests built with the patch> [DAGEN=<generator script, default dagen11.py beside this file; dagen10.py of round 10 has no extras>]
#        [BYTES=1] [BATCH=100] [MAXEXTRA=600] [TMO=900] run11.sh <first seed> <count> <family> <scratch dir>
here=$(cd "$(dirname "$0")" && pwd)
dagen=${DAGEN:-$here/dagen11.py}
first=$1; count=$2; fam=$3; scratch=$4
end=$((first+count)); batch=${BATCH:-100}
for ((s=first; s<end; s+=batch)); do
  n=$batch; if ((s+n>end)); then n=$((end-s)); fi
  W=$(python3 -I "$dagen" $s $n $fam)
  X=$(python3 -I "$dagen" extras $s $n $fam 2>/dev/null | head -c 20000)
  X=$(echo "$X" | tr ',' '\n' | awk -v max=${MAXEXTRA:-600} 'NR<=max' | paste -sd,)
  ( ulimit -v 4000000; SE_GENERATED_PRINTEDC_BYTES=${BYTES:-0} SE_GENERATED_PRINTEDC_EXTRA="$X" SE_GENERATED_PRINTEDC=only SE_GENERATED_PRINTEDC_WORDS="$W" timeout ${TMO:-900} "$GEN" "$scratch" 2>&1 | grep -E -A3 'FINDING|printed-C:' )
done
