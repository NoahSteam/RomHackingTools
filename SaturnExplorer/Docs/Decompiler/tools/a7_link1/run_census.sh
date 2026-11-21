#!/bin/bash
# Link model step 1: the generator census, one grammar, in chunks (a single background command is killed at 2 hours).
# usage: run_census.sh <generator binary> <scratch dir> <grammar 1|2> <first seed> <chunks> <seeds per chunk>
#   each chunk writes <scratch>/census_g<grammar>_<n>.txt (tests/LinkCensus.h's report) and a log; add them with
#   census_merge.py. SE_GENERATED_PINS=0 keeps the pins out of the seed sweep (they are run once, by pins.sh).
BIN=$1; D=$2; G=$3; FIRST=$4; CHUNKS=$5; PER=$6
mkdir -p "$D"
ulimit -v 4500000
for ((n = 0; n < CHUNKS; n++)); do
  S=$((FIRST + n * PER))
  SE_GENERATED_GRAMMAR=$G SE_GENERATED_FIRST=$S SE_GENERATED_SEEDS=$PER SE_GENERATED_PINS=0 SE_GENERATED_CANARY=0 \
    SE_LINK_CENSUS="$D/census_g${G}_$n.txt" timeout 7000 "$BIN" "$D/scratch_g${G}_$n" > "$D/log_g${G}_$n.txt" 2>&1
  echo "chunk $n first $S rc=$?" >> "$D/chunks_g$G.txt"
done
