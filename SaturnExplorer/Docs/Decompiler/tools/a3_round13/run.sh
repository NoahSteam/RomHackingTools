#!/bin/sh
# usage: OUT=<dir> sh run.sh file.src [ONLY] : assemble, run the driver under a memory limit, print the summary (sum.py)
: "${OUT:?}"
cd $OUT
python3 mk.py "$1" > "$1.spec" || exit 1
[ -n "$2" ] && export ONLY="$2"
( ulimit -v 4000000; timeout 600 ./drv $OUT/scr "$1.spec" > "$1.out" 2>&1 )
python3 sum.py "$1.out"
