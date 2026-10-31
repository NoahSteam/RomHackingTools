#!/bin/bash
# The generator ranges of area 3 round 16, one piece at a time (each <= 20 minutes), resumable: a piece with a .done file is skipped.
# usage: GEN=<SaturnExplorerSh2GeneratedTests> OUT=<log dir> matrix16.sh
here=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$OUT"
piece() {   # label grammar first seeds [printedc] [env]
  [ -f "$OUT/$1.done" ] && return
  if [ -n "$6" ]; then env $6 bash "$here/gen_range.sh" "$GEN" "$OUT" "$1" "$2" "$3" "$4" "$5" > "$OUT/$1.sum" 2>&1
  else bash "$here/gen_range.sh" "$GEN" "$OUT" "$1" "$2" "$3" "$4" "$5" > "$OUT/$1.sum" 2>&1; fi
  touch "$OUT/$1.done"
}
for f in 4600001 4605001 4610001 4615001; do piece g2_$f 2 $f 5000; done
for f in 4600001 4605001; do piece g1_$f 1 $f 5000; done
for f in 4600001 4602001 4604001 4606001 4608001 4610001 4612001; do piece pc_$f 0 $f 2000 pc; done
piece sp_4700001 2 4700001 5000 "" SE_GENERATED_ALPHABET=specials
echo MATRIXDONE > "$OUT/MATRIXDONE"
