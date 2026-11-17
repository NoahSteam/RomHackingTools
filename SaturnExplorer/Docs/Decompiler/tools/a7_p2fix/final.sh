#!/bin/bash
# Area 7 P2 fixes: the runs after the last code edit, one after another on the final build (the worktree's RelWithDebInfo build).
# usage: final.sh <build dir> <image dir> <scratch dir>     (writes <scratch>/final_*.log and final.out; deletes the out dirs it makes)
B="$1"; H="$2"; D="$3"
ulimit -v 6000000
mkdir -p "$D"
cd "$B/.." || exit 1
note() { echo "$*" | tee -a "$D/final.out"; }
t0=$(date +%s); ctest --test-dir "$B" --output-on-failure -j2 > "$D/final_ctest.log" 2>&1
note "ctest: $(grep -E 'tests passed|tests failed' "$D/final_ctest.log" | tail -1) ($(( $(date +%s) - t0 )) s)"
rm -rf "$D/fault"; t0=$(date +%s); "$B/bin/SaturnExplorerDecompilerFaultTests" "$D/fault" > "$D/final_fault.log" 2>&1
note "fault: $(grep -o 'fault matrix: [0-9]* of [0-9]* job(s) failed' "$D/final_fault.log") ($(( $(date +%s) - t0 )) s)"; rm -rf "$D/fault"
t0=$(date +%s); SE_GENERATED_SEEDS=20000 "$B/bin/SaturnExplorerSh2GeneratedTests" "$D/gen20k" > "$D/final_20k.log" 2>&1
note "20k: $(grep -o 'failing seeds=[0-9]*; findings=[0-9]*' "$D/final_20k.log" | tail -1); $(tail -1 "$D/final_20k.log") ($(( $(date +%s) - t0 )) s)"; rm -rf "$D/gen20k"
t0=$(date +%s); SE_GENERATED_PRINTEDC=only SE_GENERATED_SEEDS=3000 "$B/bin/SaturnExplorerSh2GeneratedTests" "$D/pc" > "$D/final_pc.log" 2>&1
note "printed-C: $(grep -oE 'agrees=[0-9]+' "$D/final_pc.log" | tail -1); $(tail -1 "$D/final_pc.log") ($(( $(date +%s) - t0 )) s)"; rm -rf "$D/pc"
for img in hwram007 hwram008; do
  rm -rf "$D/c_$img"; t0=$(date +%s)
  SE_GAME_LEND=1 SE_GAME_HWRAM="$H/$img.bin" SE_GAME_TOKEN_DUMP="$D/final_$img" "$B/bin/SaturnExplorerSh2DecompilerTests" "$D/c_$img" > "$D/final_corpus_$img.log" 2>&1
  note "corpus $img (lent): exit $?, $(grep -c 'CHECK failed' "$D/final_corpus_$img.log") check(s) failed; $(grep -oE '[0-9]+ missing and said' "$D/final_corpus_$img.log" | sort -u | tr '\n' ' '); $(grep -E '\[after\] real entries' "$D/final_corpus_$img.log" | head -1 | sed 's/^ *//') ($(( $(date +%s) - t0 )) s)"
  rm -rf "$D/c_$img"
done
