#!/bin/bash
# (area 3 fixer, A3-144 to A3-153) paths are the fixer's scratch directory; set S to your own
# usage: pc.sh BIN "words" ; runs the printed-C differential on the words
BIN=$1; W="$2"; D=$(mktemp -d /tmp/claude-0/-home-user-RomHackingTools/aae0ae36-3afa-5288-8786-5063bd965426/scratchpad/a3fix144/pcw.XXXX)
( ulimit -v 3500000; SE_GENERATED_PRINTEDC=only SE_GENERATED_PRINTEDC_WORDS="$W" SE_GENERATED_VERBOSE=${V:-0} timeout 120 $BIN $D ) 2>&1
rm -rf $D
