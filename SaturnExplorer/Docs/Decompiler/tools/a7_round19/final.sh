#!/bin/bash
# Area 7 round 19: the runs after the last code edit, one after another (the machine is shared: -j2 and ulimit -v 6000000).
# usage: final.sh <worktree SaturnExplorer dir>   (SCRATCH set to a scratch directory; logs to $SCRATCH/r19/final_*.log)
set -u
W=$1
S=${SCRATCH:?set SCRATCH to a scratch directory}
B=$W/build/bin
mkdir -p $S/r19
ulimit -v 6000000
cd $W
# printed-C
SE_GENERATED_PRINTEDC=only SE_GENERATED_SEEDS=3000 $B/SaturnExplorerSh2GeneratedTests $S/r19/fpc > $S/r19/final_pc.log 2>&1; echo "EXIT $?" >> $S/r19/final_pc.log
# the fault suite
mkdir -p $S/r19/ffault
$B/SaturnExplorerDecompilerFaultTests $S/r19/ffault > $S/r19/final_fault.log 2>&1; echo "EXIT $?" >> $S/r19/final_fault.log
# ctest
ctest --test-dir build --output-on-failure -j2 > $S/r19/final_ctest.log 2>&1; echo "EXIT $?" >> $S/r19/final_ctest.log
# the corpus, both images, lent
for img in hwram007 hwram008; do
  SE_GAME_HWRAM=$S/h/$img.bin SE_GAME_LEND=1 $B/SaturnExplorerSh2DecompilerTests $S/r19/fcorp_$img > $S/r19/final_corpus_$img.log 2>&1; echo "EXIT $?" >> $S/r19/final_corpus_$img.log
done
echo done > $S/r19/final.done
