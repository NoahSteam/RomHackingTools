#!/bin/bash
# A binary whose vendored-engine sources are mutated; the worktree is untouched. The mutated files (made with mutate.py) are in $OUT/$n/ under their
# own names (heritage.cc, varmap.cc); each is compiled with the engine's flags and swapped for its object in the link of the suite (tests), the
# generator (gen) or the fixture driver (drv, from $OUT/drv.o).
# usage: W=<worktree>/SaturnExplorer B=<configured build dir> OUT=<scratch dir> build_emut.sh <name> <tests|gen|drv>... -- <file.cc>...
set -e
: "${W:?}" "${B:?}" "${OUT:?}"
n=$1; shift
bins=(); while [ "$1" != "--" ]; do bins+=("$1"); shift; done; shift
G=$W/FrontEnd/third_party/ghidra-decompiler/cpp
for f in "$@"; do
  test -s $OUT/$n/$f
  diff $G/$f $OUT/$n/$f > $OUT/$n/$f.diff || true
  test -s $OUT/$n/$f.diff
  c++ -I$G -I$W/FrontEnd/third_party/zlib -O2 -g -DNDEBUG -std=gnu++14 -Wno-sign-compare -Wno-unused-variable -Wno-unused-function -Wno-deprecated-declarations -c $OUT/$n/$f -o $OUT/$n/$f.o
done
cd $B
for bin in "${bins[@]}"; do
  case $bin in tests|drv) lt=SaturnExplorerSh2DecompilerTests;; gen) lt=SaturnExplorerSh2GeneratedTests;; esac
  LINK=$(cat CMakeFiles/$lt.dir/link.txt)
  for f in "$@"; do LINK=$(echo "$LINK" | sed "s#FrontEnd/third_party/ghidra-decompiler/CMakeFiles/SaturnExplorerGhidraDecomp.dir/cpp/$f.o#$OUT/$n/$f.o#"); done
  if [ $bin = drv ]; then LINK=$(echo "$LINK" | sed "s#CMakeFiles/SaturnExplorerSh2DecompilerTests.dir/FrontEnd/tests/Sh2DecompilerTests.cpp.o#$OUT/drv.o#"); fi
  LINK=$(echo "$LINK" | sed -E "s#-o bin/[A-Za-z0-9]+#-o $OUT/$n/$bin#")
  eval "$LINK"
  echo built $n/$bin
done
