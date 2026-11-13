#!/bin/bash
# Area 7 round 17: each oracle rule of this round off (SE_A7_RULE_OFF) on its whole seed: the seed fails again.
# usage: mutation.sh <bin dir>   (SCRATCH set; the binary deletes its out dir)
S=${SCRATCH:?set SCRATCH to a scratch directory}
B=$(cd "$1" && pwd)/SaturnExplorerSh2GeneratedTests
ulimit -v 6000000
run() {  # row grammar seed [specials]
  local out
  out=$(env SE_A7_RULE_OFF=$1 SE_GENERATED_GRAMMAR=$2 SE_GENERATED_SEED=$3 ${4:+SE_GENERATED_ALPHABET=specials} SE_GENERATED_CANARY=0 SE_GENERATED_SHRINK=0 \
        "$B" "$S/r17/mut_$1" 2>&1 | grep -o 'failing seeds=[0-9]*; findings=[0-9]*' | tail -1)
  local on
  on=$(env SE_GENERATED_GRAMMAR=$2 SE_GENERATED_SEED=$3 ${4:+SE_GENERATED_ALPHABET=specials} SE_GENERATED_CANARY=0 SE_GENERATED_SHRINK=0 \
        "$B" "$S/r17/mut_$1" 2>&1 | grep -o 'failing seeds=[0-9]*; findings=[0-9]*' | tail -1)
  echo "A7-$1 grammar $2 seed $3${4:+ (specials)}: rule on: $on; rule off: $out"
}
run 142 2 7607636
run 143 2 7402123
run 146 1 8002899 s
