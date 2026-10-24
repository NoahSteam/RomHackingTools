#!/bin/bash
R=${A3R13_OUT:?}   # the directory with build/bin/SaturnExplorerSh2DecompilerTests
for sd in 10 11; do
  d=$R/fz$sd; rm -rf $d; mkdir -p $d
  ( ulimit -v 4000000; start=$(date +%s); SE_A3FUZZ_PROGRAMS=5000 SE_A3FUZZ_SEED=$sd timeout 1100 $R/build/bin/SaturnExplorerSh2DecompilerTests $d/scr > $R/fuzz$sd.log 2>&1; echo "rc $? wall $(( $(date +%s) - start )) s" >> $R/fuzz$sd.log )
  grep -E "hidden|rc [0-9]+ wall|all passed|A3FUZZ|perturbed|findings" $R/fuzz$sd.log | tail -6 | cut -c1-250
done
