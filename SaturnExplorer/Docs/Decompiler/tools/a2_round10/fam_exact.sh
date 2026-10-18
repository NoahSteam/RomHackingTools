#!/bin/bash
# usage: GEN=<generator with the exact-stress> fam_exact.sh <mode 1|2|3> <family> <first seed> <count> <out dir>
# runs 100 functions per process with a 120 s timeout so that a hang names its batch; SE_REPEAT_LIMIT=60
mode=$1; fam=$2; first=$3; count=$4; out=$5; mkdir -p "$out"
here=$(cd "$(dirname "$0")" && pwd)
log="$out/ex${mode}_fam${fam}_${first}.log"; : > "$log"
end=$((first+count))
for ((s=first; s<end; s+=100)); do
  n=100; if ((s+n>end)); then n=$((end-s)); fi
  W=$(python3 -I "$here/dagen10.py" $s $n $fam)
  SE_EXACT_STRESS=$mode SE_REPEAT_LIMIT=60 SE_GENERATED_PRINTEDC=only SE_GENERATED_PRINTEDC_WORDS="$W" timeout 120 "$GEN" "$out/w_${mode}_${fam}_$s" >> "$log" 2>&1
  rc=$?
  if [ $rc = 124 ]; then echo "HANG batch $s $n" >> "$log"; fi
  rm -rf "$out/w_${mode}_${fam}_$s"
done
echo "exact mode $mode fam $fam seeds $first..$((end-1)): $(grep -c 'unsettled' "$log") unsettled, $(grep -c '^HANG' "$log") hang batches, $(grep -c 'FINDING' "$log") findings"
