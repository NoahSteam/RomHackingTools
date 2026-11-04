#!/bin/bash
# (area 3 fixer, A3-144 to A3-153) paths are the fixer's scratch directory; set S to your own
# usage: runcorp.sh <binary> <tag> : both images with texts out, one at a time
S=/tmp/claude-0/-home-user-RomHackingTools/aae0ae36-3afa-5288-8786-5063bd965426/scratchpad
for n in 007 008; do
  o=$S/a3fix144/txt_$2_$n; rm -rf $o $S/a3fix144/scr_$2_$n; mkdir -p $o $S/a3fix144/scr_$2_$n
  ( ulimit -v 3500000; SE_GAME_HWRAM=$S/h/hwram$n.bin SE_GAME_LEND=1 SE_GAME_LEND_LIST=1 SE_GAME_OUT=$o timeout 1150 $1 $S/a3fix144/scr_$2_$n > $S/a3fix144/t_$2_$n.log 2>&1; echo "rc $?" >> $S/a3fix144/t_$2_$n.log )
done
echo done > $S/a3fix144/t_$2.done
