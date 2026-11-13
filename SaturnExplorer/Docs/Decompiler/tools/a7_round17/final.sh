#!/bin/bash
# Area 7 round 17: the runs after the last code edit, one after another (the machine is shared: -j2 and ulimit -v 6000000).
# usage: final.sh <worktree SaturnExplorer dir>   (SCRATCH set to a scratch directory; logs to $SCRATCH/r17/final_*.log)
set -u
W=$1
S=${SCRATCH:?set SCRATCH to a scratch directory}
B=$W/build/bin
mkdir -p $S/r17
ulimit -v 6000000
cd $W
# the fresh sweeps (canary and shrink off) on this binary
Docs/Decompiler/tools/a7_round17/sweep.sh $B final
# the 20,000-seed matrix (canary on, every pin)
SE_GENERATED_SEEDS=20000 $B/SaturnExplorerSh2GeneratedTests $S/r17/fm20k > $S/r17/final_m20k.log 2>&1; echo "EXIT $?" >> $S/r17/final_m20k.log
# printed-C
SE_GENERATED_PRINTEDC=only SE_GENERATED_SEEDS=3000 $B/SaturnExplorerSh2GeneratedTests $S/r17/fpc > $S/r17/final_pc.log 2>&1; echo "EXIT $?" >> $S/r17/final_pc.log
# the fault suite
mkdir -p $S/r17/ffault
$B/SaturnExplorerDecompilerFaultTests $S/r17/ffault > $S/r17/final_fault.log 2>&1; echo "EXIT $?" >> $S/r17/final_fault.log
# ctest
ctest --test-dir build --output-on-failure -j2 > $S/r17/final_ctest.log 2>&1; echo "EXIT $?" >> $S/r17/final_ctest.log
# the corpus, both images, lent
for img in hwram007 hwram008; do
  mkdir -p $S/r17/fcorp_$img
  SE_GAME_HWRAM=$S/h/$img.bin SE_GAME_LEND=1 $B/SaturnExplorerSh2DecompilerTests $S/r17/fcorp_$img > $S/r17/final_corpus_$img.log 2>&1; echo "EXIT $?" >> $S/r17/final_corpus_$img.log
done
echo done > $S/r17/final.done
