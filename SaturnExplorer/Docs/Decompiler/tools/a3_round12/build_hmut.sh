#!/bin/bash
# The generator (and optionally the suite) with Sh2Decompiler.cpp mutated; the mutated copy (made with mutate.py) is $OUT/$n/Sh2Decompiler.cpp.
# usage: W=<worktree>/SaturnExplorer B=<configured build dir> OUT=<scratch dir> build_hmut.sh <name> <gen|tests|drv>...
set -e
: "${W:?}" "${B:?}" "${OUT:?}"
n=$1; shift
test -s $OUT/$n/Sh2Decompiler.cpp
diff $W/FrontEnd/src/Decompiler/Sh2Decompiler.cpp $OUT/$n/Sh2Decompiler.cpp > $OUT/$n/mutation.diff || true
test -s $OUT/$n/mutation.diff
c++ -I$W/FrontEnd/src/Decompiler -I$W/FrontEnd/src -I$W/FrontEnd/tests -I$W/include -I$W/FrontEnd/third_party/ghidra-decompiler/cpp -I$W/FrontEnd/third_party/zlib -O2 -g -DNDEBUG -std=gnu++14 -c $OUT/$n/Sh2Decompiler.cpp -o $OUT/$n/Sh2Decompiler.o
cd $B
for bin in "$@"; do
  case $bin in tests|drv) lt=SaturnExplorerSh2DecompilerTests;; gen) lt=SaturnExplorerSh2GeneratedTests;; esac
  LINK=$(sed "s#CMakeFiles/$lt.dir/FrontEnd/src/Decompiler/Sh2Decompiler.cpp.o#$OUT/$n/Sh2Decompiler.o#; s#-o bin/[A-Za-z0-9]*#-o $OUT/$n/$bin#" CMakeFiles/$lt.dir/link.txt)
  if [ $bin = drv ]; then LINK=$(echo "$LINK" | sed "s#CMakeFiles/SaturnExplorerSh2DecompilerTests.dir/FrontEnd/tests/Sh2DecompilerTests.cpp.o#$OUT/drv.o#"); fi
  eval "$LINK"
  echo built $n/$bin
done
