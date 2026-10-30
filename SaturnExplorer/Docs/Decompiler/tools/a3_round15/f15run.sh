#!/bin/bash
# usage: D=<drv dir> OUTD=<dir> f15run.sh <seed> <count> [drv]  (env F15_* pass through)
cd $D; mkdir -p $OUTD
sd=$1; n=$2; drv=${3:-drv}; tag=${F15_TAG:-f15}
python3 frame15.py $sd $n > $OUTD/$tag$sd.src
python3 mk.py $OUTD/$tag$sd.src > $OUTD/$tag$sd.spec
( ulimit -v 3500000; timeout 900 ./$drv $OUTD/scr_$tag $OUTD/$tag$sd.spec > $OUTD/$tag$sd.out 2>&1 )
python3 f15sum.py $OUTD/$tag$sd.out > $OUTD/$tag$sd.sum
rm -f $OUTD/$tag$sd.spec
head -1 $OUTD/$tag$sd.sum
