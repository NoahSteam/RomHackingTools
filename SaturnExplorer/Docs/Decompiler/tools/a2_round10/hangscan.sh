#!/bin/bash
# Area 2 round 9 instrument (A2-59): the functions of dagen.py that the engine does not finish in 10 seconds, one process each, three at a time.
# usage: GEN=<SaturnExplorerSh2GeneratedTests> hangscan.sh <first seed> <count> <family> <scratch dir>   -> the word lists of the functions that hang
here=$(cd "$(dirname "$0")" && pwd)
export GEN scratch=$4
python3 -I "$here/dagen.py" $1 $2 $3 | tr ';' '\n' | xargs -d '\n' -P 3 -n 1 bash -c '
  timeout 10 env SE_GENERATED_PRINTEDC=only SE_GENERATED_PRINTEDC_WORDS="$0" "$GEN" "$scratch/$$" >/dev/null 2>&1
  [ $? = 124 ] && echo "$0"; rm -rf "$scratch/$$"'
