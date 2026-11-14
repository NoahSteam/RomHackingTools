#!/bin/bash
# Area 7 review round 20: the runs after the last code edit, one after the other (logs in $SCRATCH/r20/final_*.log).
# usage: final.sh <SaturnExplorer dir>   (SCRATCH set; $SCRATCH/h holds the two corpus images)
S=${SCRATCH:?set SCRATCH to a scratch directory}
cd "$1" || exit 1
ulimit -v 6000000
mkdir -p $S/r20/final
ctest --test-dir build --output-on-failure -j2 > $S/r20/final_ctest.log 2>&1; echo "EXIT $?" >> $S/r20/final_ctest.log
build/bin/SaturnExplorerDecompilerFaultTests $S/r20/final/fault > $S/r20/final_fault.log 2>&1; echo "EXIT $?" >> $S/r20/final_fault.log
SE_GENERATED_SEEDS=20000 build/bin/SaturnExplorerSh2GeneratedTests $S/r20/final/m20k > $S/r20/final_m20k.log 2>&1; echo "EXIT $?" >> $S/r20/final_m20k.log
SE_GENERATED_PRINTEDC=only SE_GENERATED_SEEDS=3000 build/bin/SaturnExplorerSh2GeneratedTests $S/r20/final/pc > $S/r20/final_printedc.log 2>&1; echo "EXIT $?" >> $S/r20/final_printedc.log
(cd build && bash ../Docs/Decompiler/tools/a7_round2/corpus.sh bin/SaturnExplorerSh2DecompilerTests $S/h/hwram007.bin $S/r20/corpus final007 lend)
(cd build && bash ../Docs/Decompiler/tools/a7_round2/corpus.sh bin/SaturnExplorerSh2DecompilerTests $S/h/hwram008.bin $S/r20/corpus final008 lend)
bash Docs/Decompiler/tools/a7_review20/sweep.sh build/bin/SaturnExplorerSh2GeneratedTests final
echo ALLDONE > $S/r20/final_DONE
