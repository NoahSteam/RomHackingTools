#!/bin/bash
# Area 2 round 18 / area 3 round 17 reviewer. W = the SaturnExplorer directory (its build/ holds the binaries); S = a scratch directory where
# the scripts and asm.py/mk.py of ../a3_round16 sit (the driver drv is built there by ../a3_round16/bdrv16.sh).
S=${S:?set S to a scratch directory}
until grep -q ALLDONE $S/gen/summary.txt 2>/dev/null; do sleep 20; done
cd $S/drv && ./famrun.sh ord 'ordfam.py' 12960
# hidden-input fuzz at fresh seeds
T=${W:?set W to the SaturnExplorer directory}/build/bin/SaturnExplorerSh2DecompilerTests
for sd in 31 32 33; do
  [ -f $S/fz$sd.done ] && continue
  ( ulimit -v 3500000; start=$(date +%s); SE_A3FUZZ_PROGRAMS=5000 SE_A3FUZZ_SEED=$sd timeout 1100 $T $S/fzs$sd > $S/fuzz$sd.log 2>&1; echo "rc $? wall $(( $(date +%s) - start )) s" >> $S/fuzz$sd.log )
  rm -rf $S/fzs$sd; touch $S/fz$sd.done
done
echo Q2DONE >> $S/gen/summary.txt
