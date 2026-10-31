#!/bin/bash
# usage: R=<rev dir> O=<old dir> cmp16.sh seed count : call-free printed-C differential of frame16.py fixtures (F16_PC=1 F16_NOCALL=1) on both builds
sd=$1; n=$2
cd $R
F16_PC=1 F16_NOCALL=1 python3 frame16.py $sd $n > o/pc$sd.src
for b in rev old; do
  d=$R/../a3r16$b; 
  python3 f16pc.py $d/bin/SaturnExplorerSh2GeneratedTests o/pc$sd.src o/pcw_$b 2>&1 | cut -d'|' -f1,2 > o/pc$sd.$b.txt
done
echo "seed $sd n $n: rev $(grep -vc '^functions' o/pc$sd.rev.txt) findings, old $(grep -vc '^functions' o/pc$sd.old.txt) findings"
