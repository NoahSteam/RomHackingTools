#!/bin/bash
# usage: divopt_audit.sh <ghidra-decompiler/cpp dir> <engine objects dir> <out binary>   (cuts RuleDivOpt::calcDivisor out of ruleaction.cc and builds divopt_audit.cpp around the copy)
here=$(cd "$(dirname "$0")" && pwd); cpp=$1; objs=$2; out=$3; tmp=$(mktemp -d)
python3 -I - "$cpp/ruleaction.cc" "$tmp/calcdivisor_copy.inc" <<'PY'
import sys
s = open(sys.argv[1]).read()
i = s.index("uintb RuleDivOpt::calcDivisor(uint4 n,uint8 *y,uint4 xsize)")
j = s.index("\n}\n", i) + 3
open(sys.argv[2], "w").write("static " + s[i:j].replace("RuleDivOpt::calcDivisor", "calcDivisorCopy"))
PY
g++ -O1 -std=c++14 -w -I"$cpp" -I"$tmp" "$here/divopt_audit.cpp" $(find "$objs" -name "*.o" | grep -v "sleighc\|slgh_compile\|slghparse\|slghscan") -lz -o "$out"
rm -rf "$tmp"
