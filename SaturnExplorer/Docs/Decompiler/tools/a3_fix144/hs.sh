#!/bin/bash
# (area 3 fixer, A3-144 to A3-153) paths are the fixer's scratch directory; set S to your own
S=/tmp/claude-0/-home-user-RomHackingTools/aae0ae36-3afa-5288-8786-5063bd965426/scratchpad/a3fix144
cd $S/dfix
for f in loop loop2 idx idx2 idx3 byte; do
 for b in base fix; do
  if [ $b = base ]; then G=$S/basebin/SaturnExplorerSh2GeneratedTests; else G=$S/build/bin/SaturnExplorerSh2GeneratedTests; fi
  echo "== $f $b"; python3 f16pc.py $G hs/$f.src $S/hsw_$b 2>&1
 done
done
