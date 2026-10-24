#!/bin/bash
# usage: runexh14.sh <divexh14 (the tree)> <divexh14_old (the head before patch 0064, or "-")> <scratch dir> <out dir> <mode opt|term> <xs> <total constants> <chunk>
# divexh14 over the constant list in chunks (each under 20 minutes); a finished chunk leaves <out>/<mode><xs>_<first>.done and is skipped on a restart.
# Old and new outputs are compared with divexh_cmp14.py.
new=$1; old=$2; scratch=$3; out=$4; mode=$5; xs=$6; total=$7; chunk=$8
mkdir -p "$out"
for ((f=0; f<total; f+=chunk)); do
  tag=${mode}${xs}_$f
  [ -f "$out/$tag.done" ] && continue
  ( ulimit -v 4000000; timeout 1100 "$new" "$scratch/n" $mode $xs $f $chunk "$out/$tag.new.bin" > "$out/$tag.new.log" 2>&1 )
  if [ "$old" != "-" ]; then ( ulimit -v 4000000; timeout 1100 "$old" "$scratch/o" $mode $xs $f $chunk "$out/$tag.old.bin" > "$out/$tag.old.log" 2>&1 ); fi
  echo "$tag: $(grep -h '^divexh14' "$out/$tag.new.log" | cut -c1-160)" >> "$out/summary.txt"
  touch "$out/$tag.done"
done
echo ALLDONE >> "$out/summary.txt"
