#!/bin/bash
# Area 7 round 23: the runs after the last code edit, one after the other (logs in $SCRATCH/r23/final_*.log).
# usage: final.sh <SaturnExplorer dir>   (SCRATCH set; $SCRATCH/h holds the two corpus images)
S=${SCRATCH:?set SCRATCH to a scratch directory}
cd "$1" || exit 1
ulimit -v 6000000
mkdir -p $S/r23/final
ctest --test-dir build --output-on-failure -j2 > $S/r23/final_ctest.log 2>&1; echo "EXIT $?" >> $S/r23/final_ctest.log
build/bin/SaturnExplorerDecompilerFaultTests $S/r23/final/fault > $S/r23/final_fault.log 2>&1; echo "EXIT $?" >> $S/r23/final_fault.log
SE_GENERATED_SEEDS=20000 build/bin/SaturnExplorerSh2GeneratedTests $S/r23/final/m20k > $S/r23/final_m20k.log 2>&1; echo "EXIT $?" >> $S/r23/final_m20k.log
SE_GENERATED_PRINTEDC=only SE_GENERATED_SEEDS=3000 build/bin/SaturnExplorerSh2GeneratedTests $S/r23/final/pc > $S/r23/final_printedc.log 2>&1; echo "EXIT $?" >> $S/r23/final_printedc.log
(cd build && bash ../Docs/Decompiler/tools/a7_round2/corpus.sh bin/SaturnExplorerSh2DecompilerTests $S/h/hwram007.bin $S/r23/corpus final007 lend) > $S/r23/final_c007.log 2>&1
(cd build && bash ../Docs/Decompiler/tools/a7_round2/corpus.sh bin/SaturnExplorerSh2DecompilerTests $S/h/hwram008.bin $S/r23/corpus final008 lend) > $S/r23/final_c008.log 2>&1
(cd .. && python3 -I .github/scripts/check-literal-size.py SaturnExplorer/FrontEnd SaturnExplorer/Drivers SaturnExplorer/include) > $S/r23/final_lint1.log 2>&1; echo "EXIT $?" >> $S/r23/final_lint1.log
(cd .. && python3 -I .github/scripts/check-vendored-tree.py lint SaturnExplorer/FrontEnd/third_party/ghidra-decompiler --tests-dir SaturnExplorer/FrontEnd/tests) > $S/r23/final_lint2.log 2>&1; echo "EXIT $?" >> $S/r23/final_lint2.log
# the replays of every failing seed of the round, on the final binary (each: failing seeds=0)
for a in "1 9600475" "1 9602179" "1 9603811" "1 9606064" "1 9606511" "1 9607450" "1 9608430" "2 9606539" "1 9703317" "2 9701412"; do
  set -- $a
  echo "grammar $1 seed $2: $(SE_GENERATED_GRAMMAR=$1 SE_GENERATED_SEED=$2 SE_GENERATED_CANARY=0 build/bin/SaturnExplorerSh2GeneratedTests $S/r23/final/rp 2>&1 | grep -o 'failing seeds=[0-9]*; findings=[0-9]*' | tail -1)" >> $S/r23/final_replays.log
done
echo ALLDONE > $S/r23/final_DONE
