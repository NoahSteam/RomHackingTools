#!/bin/bash
# usage: oraclevar13.sh <SaturnExplorer dir> <build dir> <out dir> <variant>
# Builds a scratch copy of the generator test (Sh2GeneratedTests.cpp) whose second reading (CxxHeader, the engine's reading of the printed text) is changed, to
# see which of the reading's special cases a text depends on; the tree does not change. Variants:
#   ccast   `(uint8)x` of a signed 4-byte x is C's conversion (the sign extension), not A2-19's zero extension
#   cpiece  the cast of a 5 to 7 byte CONCAT piece is by its sign, not A2-79's zero extension
#   both    both
se=$1; b=$2; out=$3; v=$4
mkdir -p "$out"
python3 - "$se/FrontEnd/tests/Sh2GeneratedTests.cpp" "$out/Sh2GeneratedTests_$v.cpp" "$v" <<'PY'
import sys
s = open(sys.argv[1]).read(); v = sys.argv[3]
a = "(N > M ? ((N == 8 && M == 4 && !S) ? o.Zext() : o.Ext()) : o.Zext())"
assert s.count(a) == 1
b_ = "(N > M ? ((!S && M >= 5) ? o.Zext() : ((N == 8 && M == 4 && !S) ? o.Zext() : o.Ext())) : o.Zext())"
assert s.count(b_) == 1
if v in ("ccast", "both"): s = s.replace(a, "(N > M ? o.Ext() : o.Zext())")
if v in ("cpiece", "both"): s = s.replace(b_, "(N > M ? o.Ext() : o.Zext())")
open(sys.argv[2], 'w').write(s)
PY
t=SaturnExplorerSh2GeneratedTests
fl=$(grep '^CXX_FLAGS' $b/CMakeFiles/$t.dir/flags.make | sed 's/CXX_FLAGS = //'); inc=$(grep '^CXX_INCLUDES' $b/CMakeFiles/$t.dir/flags.make | sed 's/CXX_INCLUDES = //')
c++ $fl $inc -c "$out/Sh2GeneratedTests_$v.cpp" -o "$out/Sh2GeneratedTests_$v.o" || exit 1
cd $b && link=$(cat CMakeFiles/$t.dir/link.txt | sed -E "s#CMakeFiles/$t.dir/FrontEnd/tests/Sh2GeneratedTests.cpp.o#$out/Sh2GeneratedTests_$v.o#; s#-o bin/$t#-o $out/gen_$v#")
eval "$link"
echo "built $out/gen_$v"
