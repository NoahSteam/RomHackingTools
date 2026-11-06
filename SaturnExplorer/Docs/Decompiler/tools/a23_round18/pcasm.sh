#!/bin/bash
# Area 2 round 18 / area 3 round 17 reviewer. W = the SaturnExplorer directory (its build/ holds the binaries); S = a scratch directory where
# the scripts and asm.py/mk.py of ../a3_round16 sit (the driver drv is built there by ../a3_round16/bdrv16.sh).
# usage: pcasm.sh file.s   (assembly lines) -> printed-C differential of the words, verbose
S=${S:-$(cd "$(dirname "$0")" && pwd)}
WORDS=$(cd $S && python3 -c "import sys,asm; print(' '.join('%04X'%x for x in asm.assemble(open(sys.argv[1]).read().splitlines())))" $1)
echo "WORDS $WORDS"
D=$(mktemp -d $S/pcw.XXXX)
( ulimit -v 3500000; SE_GENERATED_PRINTEDC=only SE_GENERATED_PRINTEDC_WORDS="$WORDS" SE_GENERATED_VERBOSE=${V:-1} ${EXTRA} timeout 120 ${W:?set W to the SaturnExplorer directory}/build/bin/SaturnExplorerSh2GeneratedTests $D ) 2>&1
rm -rf $D
