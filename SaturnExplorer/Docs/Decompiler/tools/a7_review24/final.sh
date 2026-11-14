#!/bin/bash
# Area 7 review round 24: the runs after the last code edit, in two streams (logs in $SCRATCH/r24/final_*.log).
# usage: final.sh <SaturnExplorer dir> a|b   (SCRATCH set; $SCRATCH/h holds the two corpus images)
#   a: ctest, fault suite, printed-C, the corpus (both images, alone and lent, with the token dumps), lints
#   b: the 20,000-seed matrix, then this round's sweeps (sweep.sh)
S=${SCRATCH:?set SCRATCH to a scratch directory}
cd "$1" || exit 1
ulimit -v 6000000
mkdir -p $S/r24/final
if [ "$2" = a ]; then
  ctest --test-dir build --output-on-failure -j2 > $S/r24/final_ctest.log 2>&1; echo "EXIT $?" >> $S/r24/final_ctest.log
  build/bin/SaturnExplorerDecompilerFaultTests $S/r24/final/fault > $S/r24/final_fault.log 2>&1; echo "EXIT $?" >> $S/r24/final_fault.log
  SE_GENERATED_PRINTEDC=only SE_GENERATED_SEEDS=3000 build/bin/SaturnExplorerSh2GeneratedTests $S/r24/final/pc > $S/r24/final_printedc.log 2>&1; echo "EXIT $?" >> $S/r24/final_printedc.log
  (cd build && bash ../Docs/Decompiler/tools/a7_round2/corpus.sh bin/SaturnExplorerSh2DecompilerTests $S/h/hwram007.bin $S/r24/corpus final007 lend) > $S/r24/final_c007.log 2>&1
  (cd build && bash ../Docs/Decompiler/tools/a7_round2/corpus.sh bin/SaturnExplorerSh2DecompilerTests $S/h/hwram008.bin $S/r24/corpus final008 lend) > $S/r24/final_c008.log 2>&1
  (cd .. && python3 -I .github/scripts/check-literal-size.py SaturnExplorer/FrontEnd SaturnExplorer/Drivers SaturnExplorer/include) > $S/r24/final_lint1.log 2>&1; echo "EXIT $?" >> $S/r24/final_lint1.log
  (cd .. && python3 -I .github/scripts/check-vendored-tree.py lint SaturnExplorer/FrontEnd/third_party/ghidra-decompiler --tests-dir SaturnExplorer/FrontEnd/tests) > $S/r24/final_lint2.log 2>&1; echo "EXIT $?" >> $S/r24/final_lint2.log
  echo ALLDONE > $S/r24/final_a_DONE
else
  SE_GENERATED_SEEDS=20000 build/bin/SaturnExplorerSh2GeneratedTests $S/r24/final/m20k > $S/r24/final_m20k.log 2>&1; echo "EXIT $?" >> $S/r24/final_m20k.log
  bash Docs/Decompiler/tools/a7_review24/sweep.sh build/bin/SaturnExplorerSh2GeneratedTests $S/r24 _final > $S/r24/final_sweeps.log 2>&1
  echo ALLDONE > $S/r24/final_b_DONE
fi
