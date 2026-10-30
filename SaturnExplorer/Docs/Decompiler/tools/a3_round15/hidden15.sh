#!/bin/bash
# TestArea3HiddenInputFuzz at seeds 14 and 15 (5,000 programs each), resumable.
# usage: TESTS=<SaturnExplorerSh2DecompilerTests> OUT=<dir> hidden14.sh
mkdir -p $OUT
for sd in 14 15; do
  [ -f $OUT/fz$sd.done ] && continue
  rm -rf $OUT/fzs$sd
  ( ulimit -v 4000000; start=$(date +%s); SE_A3FUZZ_PROGRAMS=5000 SE_A3FUZZ_SEED=$sd timeout 1100 $TESTS $OUT/fzs$sd > $OUT/fuzz$sd.log 2>&1; echo "rc $? wall $(( $(date +%s) - start )) s" >> $OUT/fuzz$sd.log )
  grep -E "hidden|rc [0-9]+ wall|all passed|A3FUZZ|perturbed|findings" $OUT/fuzz$sd.log | tail -6 | cut -c1-250 > $OUT/fz$sd.sum
  touch $OUT/fz$sd.done
done
