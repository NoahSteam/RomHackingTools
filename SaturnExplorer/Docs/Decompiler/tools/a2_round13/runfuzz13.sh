#!/bin/bash
# usage: runfuzz13.sh <rulefuzz13> <scratch> <out dir> <label> <first seed> <count> <depth> [env assignments, e.g. RF_SH2=1 RF_WIDE=1]
# Runs the rule fuzzer in chunks of 500,000 seeds (each about 90 s), under ulimit -v 4000000 and timeout; a finished chunk leaves a marker, so a restart resumes.
bin=$1; scratch=$2; out=$3; label=$4; first=$5; count=$6; depth=$7; shift 7
mkdir -p "$out"
n=0
while [ $n -lt $count ]; do
  c=500000; [ $((count-n)) -lt $c ] && c=$((count-n))
  f=$((first+n))
  if [ ! -f "$out/$label.$f.done" ]; then
    ( ulimit -v 4000000; env "$@" RF_SHOW=3 timeout 900 "$bin" "$scratch/$label" $f $c $depth > "$out/$label.$f.log" 2>&1 )
    echo "$label $f $c depth $depth rc=$?" >> "$out/summary.txt"
    grep -E '^rulefuzz13' "$out/$label.$f.log" >> "$out/summary.txt"
    touch "$out/$label.$f.done"
  fi
  n=$((n+c))
done
