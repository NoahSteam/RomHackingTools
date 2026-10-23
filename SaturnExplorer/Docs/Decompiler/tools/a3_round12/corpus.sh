#!/bin/bash
# The game corpus with lending, one image: prints the suite's counts; the log is $OUT/run<n>.log.
# usage: TESTS=<build>/bin/SaturnExplorerSh2DecompilerTests IMG=<hwram image> OUT=<dir> corpus.sh <label>
: "${TESTS:?}" "${IMG:?}" "${OUT:?}"
mkdir -p $OUT
( ulimit -v 4000000; start=$(date +%s); SE_GAME_HWRAM=$IMG SE_GAME_LEND=1 SE_GAME_LEND_LIST=1 timeout 1100 $TESTS $OUT/scr$1 > $OUT/run$1.log 2>&1; echo "rc $? wall $(( $(date +%s) - start )) s" >> $OUT/run$1.log )
grep -E "game code:|signatures lent|missing and said|all passed" $OUT/run$1.log | cut -c1-230
