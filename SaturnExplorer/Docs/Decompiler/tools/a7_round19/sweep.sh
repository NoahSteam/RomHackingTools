#!/bin/bash
# usage: sweep.sh <bin dir> <tag>
# Area 7 round 19's fresh sweeps (canary and shrink off): grammar 1 and 2, 10,000 seeds each from 8,700,001; grammar 1 specials,
# 5,000 from 8,800,001. Logs go to $SCRATCH/r19/sweep_<tag>_{g1,g2,sp}.log (the binary deletes its out dir: a fresh one each).
S=${SCRATCH:?set SCRATCH to a scratch directory}
B=$1/SaturnExplorerSh2GeneratedTests
TAG=$2
mkdir -p $S/r19
ulimit -v 6000000
SE_GENERATED_GRAMMAR=1 SE_GENERATED_FIRST=8700001 SE_GENERATED_SEEDS=10000 SE_GENERATED_CANARY=0 SE_GENERATED_SHRINK=0 $B $S/r19/sw_${TAG}_g1 > $S/r19/sweep_${TAG}_g1.log 2>&1; echo "EXIT $?" >> $S/r19/sweep_${TAG}_g1.log
SE_GENERATED_GRAMMAR=2 SE_GENERATED_FIRST=8700001 SE_GENERATED_SEEDS=10000 SE_GENERATED_CANARY=0 SE_GENERATED_SHRINK=0 $B $S/r19/sw_${TAG}_g2 > $S/r19/sweep_${TAG}_g2.log 2>&1; echo "EXIT $?" >> $S/r19/sweep_${TAG}_g2.log
SE_GENERATED_GRAMMAR=1 SE_GENERATED_ALPHABET=specials SE_GENERATED_FIRST=8800001 SE_GENERATED_SEEDS=5000 SE_GENERATED_CANARY=0 SE_GENERATED_SHRINK=0 $B $S/r19/sw_${TAG}_sp > $S/r19/sweep_${TAG}_sp.log 2>&1; echo "EXIT $?" >> $S/r19/sweep_${TAG}_sp.log
