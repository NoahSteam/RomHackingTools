#!/bin/bash
# Area 7 round 13: generator ranges in pieces, one after the other, under the machine's rules.
# usage: ranges.sh <generator test binary> <scratch dir> <tag>, then lines "grammar alphabet first count" on stdin
#   (alphabet: plain or specials); writes <scratch>/<tag>-g<grammar>-<alphabet>-<first>.log and prints each piece's summary.
# The binary deletes its out dir: each piece gets its own under the scratch dir.
BIN=$1; S=$2; TAG=$3
ulimit -v 4000000
while read g alpha first n; do
  name=$TAG-g$g-$alpha-$first
  if [ "$alpha" = specials ]; then export SE_GENERATED_ALPHABET=specials; else unset SE_GENERATED_ALPHABET; fi
  SE_GENERATED_GRAMMAR=$g SE_GENERATED_FIRST=$first SE_GENERATED_SEEDS=$n SE_GENERATED_CANARY=0 timeout 7000 "$BIN" "$S/out-$name" > "$S/$name.log" 2>&1
  echo "$name exit=$? $(grep -o 'failing seeds=[0-9]*; findings=[0-9]*' "$S/$name.log" | tail -n 1)"
  rm -rf "$S/out-$name"
done
