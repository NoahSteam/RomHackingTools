#!/bin/bash
# Area 7 round 21: the runs after the last code edit, one after the other (logs in $SCRATCH/r21/final_*.log).
# usage: final.sh <SaturnExplorer dir>   (SCRATCH set; $SCRATCH/h holds the two corpus images)
S=${SCRATCH:?set SCRATCH to a scratch directory}
cd "$1" || exit 1
ulimit -v 6000000
mkdir -p $S/r21/final
ctest --test-dir build --output-on-failure -j2 > $S/r21/final_ctest.log 2>&1; echo "EXIT $?" >> $S/r21/final_ctest.log
build/bin/SaturnExplorerDecompilerFaultTests $S/r21/final/fault > $S/r21/final_fault.log 2>&1; echo "EXIT $?" >> $S/r21/final_fault.log
SE_GENERATED_SEEDS=20000 build/bin/SaturnExplorerSh2GeneratedTests $S/r21/final/m20k > $S/r21/final_m20k.log 2>&1; echo "EXIT $?" >> $S/r21/final_m20k.log
SE_GENERATED_PRINTEDC=only SE_GENERATED_SEEDS=3000 build/bin/SaturnExplorerSh2GeneratedTests $S/r21/final/pc > $S/r21/final_printedc.log 2>&1; echo "EXIT $?" >> $S/r21/final_printedc.log
(cd build && bash ../Docs/Decompiler/tools/a7_round2/corpus.sh bin/SaturnExplorerSh2DecompilerTests $S/h/hwram007.bin $S/r21/corpus final007 lend) > $S/r21/final_c007.log 2>&1
(cd build && bash ../Docs/Decompiler/tools/a7_round2/corpus.sh bin/SaturnExplorerSh2DecompilerTests $S/h/hwram008.bin $S/r21/corpus final008 lend) > $S/r21/final_c008.log 2>&1
(cd .. && python3 -I .github/scripts/check-literal-size.py SaturnExplorer/FrontEnd SaturnExplorer/Drivers SaturnExplorer/include) > $S/r21/final_lint1.log 2>&1; echo "EXIT $?" >> $S/r21/final_lint1.log
(cd .. && python3 -I .github/scripts/check-vendored-tree.py lint SaturnExplorer/FrontEnd/third_party/ghidra-decompiler --tests-dir SaturnExplorer/FrontEnd/tests) > $S/r21/final_lint2.log 2>&1; echo "EXIT $?" >> $S/r21/final_lint2.log
echo ALLDONE > $S/r21/final_DONE
