#!/bin/bash
# usage: corp.sh <label> <test binary>   (runs both images, texts in $R/out_<label>_<img>)
R=${A3R13_OUT:?}; H=${A3R13_IMG:?}   # R: output directory, H: the directory with hwram007.bin and hwram008.bin
lab=$1; bin=$2
for img in 007 008; do
  o=$R/out_${lab}_$img; rm -rf $o; mkdir -p $o
  ( ulimit -v 4000000; start=$(date +%s); SE_GAME_HWRAM=$H/hwram$img.bin SE_GAME_LEND=1 SE_GAME_LEND_LIST=1 SE_GAME_OUT=$o timeout 1150 $bin $o/scr > $o.log 2>&1; echo "rc $? wall $(( $(date +%s) - start )) s" >> $o.log )
  grep -E "rc [0-9]+ wall|signatures lent|missing and said|all passed|game code:" $o.log | cut -c1-250
done
