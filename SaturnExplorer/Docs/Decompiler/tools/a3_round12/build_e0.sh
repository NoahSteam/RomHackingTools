#!/bin/bash
# The baseline for the cost-on-the-corpora comparison: the engine with patch 0061's escape walk switched off (FrameEscape::compute returns at once, so nothing escapes
# and no store is marked or widened). usage: W=... B=... OUT=... build_e0.sh   (builds $OUT/E0/{tests,gen})
set -e
here=$(cd "$(dirname "$0")" && pwd)
mkdir -p $OUT/E0
python3 $here/mutate.py $W/FrontEnd/third_party/ghidra-decompiler/cpp/heritage.cc $OUT/E0/heritage.cc "  const int4 maxMembers = 8192;===  return;\n  const int4 maxMembers = 8192;"
bash $here/build_emut.sh E0 tests gen -- heritage.cc
