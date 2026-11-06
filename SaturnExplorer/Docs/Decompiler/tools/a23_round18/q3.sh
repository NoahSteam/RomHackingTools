#!/bin/bash
# Area 2 round 18 / area 3 round 17 reviewer. W = the SaturnExplorer directory (its build/ holds the binaries); S = a scratch directory where
# the scripts and asm.py/mk.py of ../a3_round16 sit (the driver drv is built there by ../a3_round16/bdrv16.sh).
S=${S:?set S to a scratch directory}
B=${W:?set W to the SaturnExplorer directory}/build/bin
T=${W:?set W to the SaturnExplorer directory}/Docs/Decompiler/tools
until grep -q Q2DONE $S/gen/summary.txt 2>/dev/null; do sleep 20; done
# dagen16/17 narrow-compare and control families at fresh seeds (1,000 each from 29,000,001)
bash $T/a2_round17/fam17.sh $B/SaturnExplorerSh2GeneratedTests $S/fam17 600 29000001 98 99 100 101 102 103 108 109 75 79 83 85
# memory families little-endian and big-endian
WIDTHS="4 2 1" bash $T/a2_round17/fam17.sh $B/SaturnExplorerSh2GeneratedTests $S/fam17le 200 29000001 91 92 93 94 97
BE=1 WIDTHS="4 2 1" bash $T/a2_round17/fam17.sh $B/SaturnExplorerSh2GeneratedTests $S/fam17be 200 29000001 104 105 106 110 111
# decode oracle with Mednafen, a fresh seed
if [ ! -f $S/dec.done ]; then
  ( ulimit -v 3500000; SE_MEDNAFEN_SS=$S/../mednafen/ss SE_DECODE_SEED=46180023 SE_DECODE_CASES=50000 timeout 1150 $B/SaturnExplorerSh2DecodeTests $S/decw > $S/dec.log 2>&1; echo "rc $?" >> $S/dec.log )
  touch $S/dec.done
fi
echo Q3DONE >> $S/gen/summary.txt
