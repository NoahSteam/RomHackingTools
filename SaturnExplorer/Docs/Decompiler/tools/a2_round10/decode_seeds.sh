#!/bin/bash
# usage: decode_seeds.sh <SaturnExplorerSh2DecodeTests> <out dir> <cases> <seed>...   -> one log per seed, summary line each
bin=$1; out=$2; cases=$3; shift 3
mkdir -p "$out"
for s in "$@"; do
  SE_MEDNAFEN_SS=${SE_MEDNAFEN_SS:?} SE_DECODE_SEED=$s SE_DECODE_CASES=$cases nice -n 5 "$bin" "$out/work_$s" > "$out/dec_$s.log" 2>&1
  echo "seed $s cases $cases exit $? : $(grep -E 'differential execution' "$out/dec_$s.log" | head -1) | $(grep -c 'disagreements with Mednafen' "$out/dec_$s.log") decode lines | $(grep -E 'SLEIGH: ' "$out/dec_$s.log" | head -1) | $(tail -1 "$out/dec_$s.log")"
done
