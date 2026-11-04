#!/bin/bash
# (area 3 fixer, A3-144 to A3-153) paths are the fixer's scratch directory; set S to your own
S=/tmp/claude-0/-home-user-RomHackingTools/aae0ae36-3afa-5288-8786-5063bd965426/scratchpad/a3fix144
G=$S/gen_fix5; D=$S/dec_fix5
cd $S
for f in 4800001 4801001 4802001; do bash $S/gen_range.sh $G $S/ranges pc_$f 0 $f 1000 pc >> $S/ranges/summary.txt; done
bash $S/gen_range.sh $G $S/ranges g1_4800001 1 4800001 5000 >> $S/ranges/summary.txt
bash $S/gen_range.sh $G $S/ranges g2_4800001 2 4800001 5000 >> $S/ranges/summary.txt
for sd in 161 162 163 164; do $S/f16pc_run.sh $G fix $sd 300 >> $S/ranges/summary.txt; done
for seed in 17 18; do mkdir -p $S/fz$seed; ( ulimit -v 3500000; SE_A3FUZZ_PROGRAMS=5000 SE_A3FUZZ_SEED=$seed timeout 1150 $D $S/fz$seed > $S/ranges/fuzz$seed.log 2>&1 ); echo "fuzz $seed: $(grep 'a3 fuzz:' $S/ranges/fuzz$seed.log) $(tail -1 $S/ranges/fuzz$seed.log)" >> $S/ranges/summary.txt; rm -rf $S/fz$seed; done
echo ALLDONE >> $S/ranges/summary.txt
