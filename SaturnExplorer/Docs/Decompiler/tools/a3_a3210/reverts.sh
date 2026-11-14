#!/bin/bash
# Area 3, A11-33: the oracle against the reverted fixes. Each of A3-199, A3-200, A3-205, A3-206 and A3-207 was switched off in turn in
# Sh2Decompiler.cpp by a temporary switch (SE_A3_REVERT=<row>, not in the branch; the lines the record names), and the binary run on the first
# 2,000 seeds of the caution range of grammar 2. This script is that run for a binary that has such a switch.
# usage: reverts.sh <test binary> <scratch dir> [rows="0 199 200 205 206 207"]
set -u
BIN=${1:?binary}; OUT=${2:?scratch dir}; ROWS=${3:-"0 199 200 205 206 207"}
mkdir -p "$OUT"
for rv in $ROWS; do
    ( ulimit -v 3000000
      SE_A3_REVERT=$rv SE_GENERATED_PINS=0 SE_GENERATED_GRAMMAR=2 SE_GENERATED_FIRST=12000001 SE_GENERATED_SEEDS=2000 SE_GENERATED_CANARY=0 \
          SE_GENERATED_SHRINK=0 "$BIN" "$OUT/run_$rv" > "$OUT/rv_$rv.log" 2>&1 )
    echo "revert $rv: $(grep -o 'failing seeds=[0-9]*; findings=[0-9]*' "$OUT/rv_$rv.log") $(grep '^finding classes' "$OUT/rv_$rv.log")"
done
