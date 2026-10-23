#!/bin/bash
# usage: runmut13.sh <rulefuzz13 linked with mutate12.py's ruleaction.cc and subflow.cc> <scratch> <out dir> <first seed> <seeds> <depth> [mutant ids...]
# The rule fuzzer against round 12's 17 environment-gated mutants (SE_MUT=<id>; the baseline is SE_MUT=0): per mutant, the rules whose count of wrong
# applications is above the baseline's. A mutant that no run kills is a rule the fuzzer does not test (a mutant of a rule that is never applied to a graph).
bin=$1; scratch=$2; out=$3; first=$4; n=$5; depth=$6; shift 6
ids="$@"; [ -z "$ids" ] && ids="1 2 4 5 6 7 8 9 10 11 12 13 14 15 16 17"
mkdir -p $out
run() { ( ulimit -v 4000000; SE_MUT=$1 RF_CONSUME=1 RF_SHOW=1 timeout 900 $bin $scratch/m$1 $first $n $depth > $out/mut$1.log 2>&1 ); }
[ -f $out/mut0.log ] || run 0
for m in $ids; do
  [ -f $out/mut$m.log ] || run $m
  diff=""
  for r in $(grep '^RULE' $out/mut$m.log | awk '{print $2}'); do
    b=$(grep "^RULE $r " $out/mut0.log | awk '{print $8}'); c=$(grep "^RULE $r " $out/mut$m.log | awk '{print $8}')
    [ "${c:-0}" -gt "${b:-0}" ] && diff="$diff $r(${b:-0}->${c})"
  done
  mk=$(grep -c '^MASK' $out/mut$m.log); mk0=$(grep -c '^MASK' $out/mut0.log)
  echo "mutant $m: killed by$diff  masks $mk0->$mk"
done
