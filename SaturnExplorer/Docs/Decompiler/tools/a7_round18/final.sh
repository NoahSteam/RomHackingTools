#!/bin/bash
# Area 7 round 18: the runs after the last code edit, one after another (the machine is shared: -j2 and ulimit -v 6000000).
# usage: final.sh <worktree SaturnExplorer dir>   (SCRATCH set to a scratch directory; logs to $SCRATCH/r18/final_*.log)
set -u
W=$1
S=${SCRATCH:?set SCRATCH to a scratch directory}
B=$W/build/bin
mkdir -p $S/r18
ulimit -v 6000000
cd $W
# printed-C
SE_GENERATED_PRINTEDC=only SE_GENERATED_SEEDS=3000 $B/SaturnExplorerSh2GeneratedTests $S/r18/fpc > $S/r18/final_pc.log 2>&1; echo "EXIT $?" >> $S/r18/final_pc.log
# the fault suite
mkdir -p $S/r18/ffault
$B/SaturnExplorerDecompilerFaultTests $S/r18/ffault > $S/r18/final_fault.log 2>&1; echo "EXIT $?" >> $S/r18/final_fault.log
# ctest
ctest --test-dir build --output-on-failure -j2 > $S/r18/final_ctest.log 2>&1; echo "EXIT $?" >> $S/r18/final_ctest.log
# the corpus, both images, lent
for img in hwram007 hwram008; do
  SE_GAME_HWRAM=$S/h/$img.bin SE_GAME_LEND=1 $B/SaturnExplorerSh2DecompilerTests $S/r18/fcorp_$img > $S/r18/final_corpus_$img.log 2>&1; echo "EXIT $?" >> $S/r18/final_corpus_$img.log
done
echo done > $S/r18/final.done
