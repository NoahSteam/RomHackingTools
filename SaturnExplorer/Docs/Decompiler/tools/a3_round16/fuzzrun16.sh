#!/bin/bash
# usage: D=<driver dir (drv, mk.py and the four files of this directory)> OUTD=<result dir> fuzzrun.sh esc|hb <first seed> <last seed> <count per seed> [drv name]
# Each seed is one piece (a .sum file); a seed with a .sum is skipped, so a restart resumes. Every run under ulimit -v 4000000 and a timeout.
kind=$1; a=$2; b=$3; n=$4; drv=${5:-drv}
mkdir -p $OUTD
for ((sd=a; sd<=b; sd++)); do
  [ -f $OUTD/$kind$sd.sum ] && continue
  ( cd $D
    if [ $kind = esc ]; then python3 escfuzz14.py $sd $n > $OUTD/$kind$sd.src; else python3 hbfuzz14.py $sd $n > $OUTD/$kind$sd.src; fi
    python3 mk.py $OUTD/$kind$sd.src > $OUTD/$kind$sd.spec
    ( ulimit -v 4000000; timeout 900 ./$drv $OUTD/scr_$kind $OUTD/$kind$sd.spec > $OUTD/$kind$sd.out 2>&1 )
    if [ $kind = esc ]; then python3 escsum14.py $OUTD/$kind$sd.out > $OUTD/$kind$sd.sum; else python3 hbsum14.py $OUTD/$kind$sd.out > $OUTD/$kind$sd.sum; fi )
  rm -f $OUTD/$kind$sd.spec
done
