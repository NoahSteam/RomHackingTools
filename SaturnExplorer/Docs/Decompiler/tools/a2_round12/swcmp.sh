#!/bin/bash
# usage: swcmp.sh <new generated-tests binary> <old one> <first seed> <count> <scratch dir>
# Jump-table modules of a2_round11/swtab11.py through two engines (the head and one with the pre-0059 rangeutil.cc): the whole output of the
# generator (the decompiled text and every verdict) must be the same, apart from timings. Prints the seeds that differ or fail.
here=$(cd "$(dirname "$0")" && pwd)
new=$1; old=$2; first=$3; count=$4; scratch=$5; mkdir -p $scratch
same=0; diff=0; fail=0
for ((s=first; s<first+count; s++)); do
  out=$(python3 -I "$here/../a2_round11/swtab11.py" $s 2>/dev/null) || continue
  W=${out%%|*}; rest=${out#*|}; F=${rest%%|*}; T=${rest#*|}
  a=$( ulimit -v 4000000; SE_GENERATED_WORDS="$W" SE_GENERATED_FUNCS="$F" SE_GENERATED_TABLES="$T" timeout 120 "$new" "$scratch/w1" 2>&1 | grep -v " ms")
  b=$( ulimit -v 4000000; SE_GENERATED_WORDS="$W" SE_GENERATED_FUNCS="$F" SE_GENERATED_TABLES="$T" timeout 120 "$old" "$scratch/w2" 2>&1 | grep -v " ms")
  echo "$a" | grep -q "all passed" || { fail=$((fail+1)); echo "seed $s: HEAD FAILS"; }
  if [ "$a" == "$b" ]; then same=$((same+1)); else diff=$((diff+1)); echo "seed $s: output differs between head and pre-0059"; fi
done
echo "modules: same $same, differ $diff, head failing $fail"
