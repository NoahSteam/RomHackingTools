#!/bin/bash
ulimit -v 4000000
# usage: rehang.sh <generator> <mode> <family> <first> <count>  -> prints the word lists of functions that need more than 20 s alone
gen=$1; mode=$2; fam=$3; first=$4; n=$5
here=$(cd "$(dirname "$0")" && pwd)
python3 -I "$here/dagen10.py" $first $n $fam | tr ';' '\n' | while read -r w; do
  d=$(mktemp -d)
  SE_EXACT_STRESS=$mode SE_REPEAT_LIMIT=60 SE_GENERATED_PRINTEDC=only SE_GENERATED_PRINTEDC_WORDS="$w" timeout 20 "$gen" "$d/w" > /dev/null 2>&1
  [ $? = 124 ] && echo "HANG: $w"
  rm -rf "$d"
done
echo "rehang $fam $first $n done"
