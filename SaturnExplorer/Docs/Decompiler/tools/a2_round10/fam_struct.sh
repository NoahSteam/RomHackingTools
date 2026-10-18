#!/bin/bash
# usage: GEN=<generator with the struct checker> fam_struct.sh <family> <first seed> <count> <out dir>   -> out/fam<F>_<first>.log (STRUCT lines and summaries), prints a one-line result
fam=$1; first=$2; count=$3; out=$4; mkdir -p "$out"
here=$(cd "$(dirname "$0")" && pwd)
log="$out/fam${fam}_${first}.log"; : > "$log"
end=$((first+count))
for ((s=first; s<end; s+=300)); do
  n=300; if ((s+n>end)); then n=$((end-s)); fi
  W=$(python3 -I "$here/dagen10.py" $s $n $fam)
  SE_STRUCT_CHECK=1 SE_STRUCT_MODEL=1 SE_NZM_FREE=1 SE_NZM_CHECK=1 SE_CONSUME_CHECK=1 SE_GENERATED_PRINTEDC=only SE_GENERATED_PRINTEDC_WORDS="$W" timeout 1100 "$GEN" "$out/w_${fam}_$s" >> "$log" 2>&1 || echo "EXIT $? at $s" >> "$log"
  rm -rf "$out/w_${fam}_$s"
done
echo "fam $fam seeds $first..$((end-1)): $(grep -c '^STRUCT kind' "$log") struct, $(grep -c '^NZMCHK' "$log") nzmchk, $(grep -c '^CONSUMECHK' "$log") consumechk, $(grep -c 'FINDING' "$log") findings, $(grep -c 'EXIT' "$log") exits"
