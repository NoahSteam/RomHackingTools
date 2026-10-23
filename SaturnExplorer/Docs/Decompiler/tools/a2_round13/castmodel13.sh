#!/bin/bash
# usage: castmodel13.sh <SaturnExplorer dir> <scratch dir>
# Cuts the engine's reading (CxxHeader, the C++ classes of the printed-C oracle's second reading) out of Sh2GeneratedTests.cpp and runs castmodel13.cpp against it:
# every explicit cast `(T<N,S>)x` of a T<M,U> value, and of a CONCAT piece, for sizes 1 to 8 and the edge values, against C's own conversion and against what the
# engine's printer means by that cast (CastStrategyC::isZextCast / isSextCast: a cast prints for a ZEXT only from an unsigned or bool input, for a SEXT only from a
# signed or bool input; the CAST op keeps the size). Prints the cells where the reading differs from C, and from the printer.
se=$1; out=$2; mkdir -p "$out"
here=$(cd "$(dirname "$0")" && pwd)
python3 - "$se/FrontEnd/tests/Sh2GeneratedTests.cpp" "$out/cxxheader.inc" <<'PY'
import sys
s = open(sys.argv[1]).read()
a = s.index('R"CXX(') + len('R"CXX(')
b = s.index(')CXX"', a)
open(sys.argv[2], 'w').write(s[a:b])
PY
g++ -std=c++14 -O1 -I"$out" -I"$here" "$here/castmodel13.cpp" -o "$out/castmodel13" || exit 1
"$out/castmodel13"
