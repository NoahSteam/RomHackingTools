#!/bin/bash
# (area 3 fixer, A3-144 to A3-153) paths are the fixer's scratch directory; set S to your own
# usage: f16pc_run.sh <gen binary> <tag> <seed> <count> [mode]
S=/tmp/claude-0/-home-user-RomHackingTools/aae0ae36-3afa-5288-8786-5063bd965426/scratchpad/a3fix144
G=$1; tag=$2; sd=$3; n=$4; mode=$5
cd $S/dfix
mkdir -p $S/f16
src=$S/f16/f16_${sd}_${mode:-all}.src
if [ -n "$mode" ]; then F16_MODE=$mode F16_PC=1 F16_NOCALL=1 python3 frame16.py $sd $n > $src; else F16_PC=1 F16_NOCALL=1 python3 frame16.py $sd $n > $src; fi
python3 f16pc.py $G $src $S/f16/w_${tag}_${sd} > $S/f16/pc_${tag}_${sd}_${mode:-all}.out 2> $S/f16/pc_${tag}_${sd}_${mode:-all}.err
echo "$tag $sd ${mode:-all}: $(cat $S/f16/pc_${tag}_${sd}_${mode:-all}.err | tail -1)"
