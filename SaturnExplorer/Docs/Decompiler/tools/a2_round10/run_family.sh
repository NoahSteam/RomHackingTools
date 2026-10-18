#!/bin/bash
# Area 2 round 9 instrument: dagen.py's functions through the printed-C differential, in batches of 300 (the environment limit on one word list).
# usage: GEN=<SaturnExplorerSh2GeneratedTests> [CHECK=1] run_family.sh <first seed> <count> <family|-> <scratch dir>
#   CHECK=1 needs a generator built from a tree with nzm_check_r9.patch applied; it adds SE_NZM_FREE/SE_NZM_CHECK/SE_CONSUME_CHECK (stale non-zero masks, contradicted consume masks).
#   SE_TRIAL_CONSUME=1 is read by a tree that also has the trial in A2-58's row. Prints the printed-C summary of each batch and every finding.
here=$(cd "$(dirname "$0")" && pwd)
first=$1; count=$2; fam=$3; scratch=$4
end=$((first+count))
for ((s=first; s<end; s+=300)); do
  n=300; if ((s+n>end)); then n=$((end-s)); fi
  if [ "$fam" = "-" ]; then W=$(python3 -I "$here/dagen.py" $s $n); else W=$(python3 -I "$here/dagen.py" $s $n $fam); fi
  if [ -n "$CHECK" ]; then export SE_NZM_FREE=1 SE_NZM_CHECK=1 SE_CONSUME_CHECK=1; fi
  SE_GENERATED_PRINTEDC=only SE_GENERATED_PRINTEDC_WORDS="$W" "$GEN" "$scratch" 2>&1 | grep -E -A3 'FINDING|printed-C:|NZMCHK|CONSUMECHK'
done
