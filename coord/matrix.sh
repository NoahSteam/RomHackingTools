#!/bin/bash
# Coordinator's merge matrix (rebuilt 2026-11-09). usage: [VENDOR=1] matrix.sh <outdir>; run from the repo root.
# Logs in <outdir>: ctest.log live.log fault.log gen_20k.log gen_g1.log gen_g2.log gen_pc.log c7.log c8.log vendor.log
S=/tmp/claude-0/-home-user-RomHackingTools/aae0ae36-3afa-5288-8786-5063bd965426/scratchpad
OUT=$1; ROOT=$(pwd); B=$ROOT/SaturnExplorer/build
mkdir -p "$OUT"; cd "$ROOT/SaturnExplorer" || exit 1
cmake -S . -B build -DCMAKE_BUILD_TYPE=RelWithDebInfo -DBUILD_TESTING=ON -DCMAKE_DISABLE_FIND_PACKAGE_SDL2=ON > "$OUT/cfg.log" 2>&1
if cmake --build build -j4 > "$OUT/build.log" 2>&1; then echo BUILD_OK; else echo BUILD_FAIL; tail -30 "$OUT/build.log"; exit 1; fi
ctest --test-dir build --output-on-failure -j3 -E '^SaturnExplorerLive' > "$OUT/ctest.log" 2>&1
ctest --test-dir build --output-on-failure -R '^SaturnExplorerLive' > "$OUT/live.log" 2>&1
timeout 1800 $B/bin/SaturnExplorerDecompilerFaultTests "$OUT/fault_scratch" > "$OUT/fault.log" 2>&1; rm -rf "$OUT/fault_scratch"
G=$B/bin/SaturnExplorerSh2GeneratedTests
( SE_GENERATED_SEEDS=20000 timeout 5400 $G "$OUT/g20k" > "$OUT/gen_20k.log" 2>&1 ) &
( SE_GENERATED_GRAMMAR=1 SE_GENERATED_SEEDS=3000 timeout 3600 $G "$OUT/gg1" > "$OUT/gen_g1.log" 2>&1 ) &
( SE_GENERATED_GRAMMAR=2 SE_GENERATED_SEEDS=3000 timeout 3600 $G "$OUT/gg2" > "$OUT/gen_g2.log" 2>&1 ) &
wait
SE_GENERATED_PRINTEDC=only timeout 3600 $G "$OUT/gpc" > "$OUT/gen_pc.log" 2>&1
( SE_GAME_LEND=1 SE_GAME_HWRAM=$S/h/hwram007.bin timeout 3600 $B/bin/SaturnExplorerSh2DecompilerTests "$OUT/r7" > "$OUT/c7.log" 2>&1 ) &
( SE_GAME_LEND=1 SE_GAME_HWRAM=$S/h/hwram008.bin timeout 3600 $B/bin/SaturnExplorerSh2DecompilerTests "$OUT/r8" > "$OUT/c8.log" 2>&1 ) &
wait
rm -rf "$OUT/r7" "$OUT/r8" "$OUT/g20k" "$OUT/gg1" "$OUT/gg2" "$OUT/gpc"
cd "$ROOT"
if [ "$VENDOR" = 1 ]; then
  V=SaturnExplorer/FrontEnd/third_party/ghidra-decompiler
  pin=$(sed -n 's/^PINNED_COMMIT = "\([0-9a-f]\{40\}\)".*/\1/p' $V/vendor.py)
  if [ "$(git -C $S/ghidra rev-parse HEAD 2>/dev/null)" != "$pin" ]; then
    rm -rf $S/ghidra
    git clone -q --filter=blob:none --no-checkout https://github.com/NationalSecurityAgency/ghidra.git $S/ghidra &&
    git -C $S/ghidra sparse-checkout set --no-cone /Ghidra/Features/Decompiler/src/decompile/cpp/ /Ghidra/Processors/SuperH/data/languages/ /LICENSE /NOTICE &&
    git -C $S/ghidra checkout -q "$pin"
  fi
  rm -rf $S/vend
  python3 -I .github/scripts/check-vendored-tree.py rebuild $V $S/ghidra $S/vend > "$OUT/vendor.log" 2>&1; rc=$?
  if [ $rc = 0 ]; then echo VENDOR_DIFF_LINES 0; else echo "VENDOR_DIFF_LINES $(wc -l < "$OUT/vendor.log") (rc $rc)"; fi
  rm -rf $S/vend
fi
echo ALL_DONE
