#!/bin/bash
# A binary whose vendored-engine sources are mutated by sed expressions; the worktree is untouched. Compiles each mutated file with the engine's
# own flags and links the suite (tests), the generator (gen) or the fixture driver (drv, from $OUT/drv.o) with the objects swapped in.
# usage: W=<worktree>/SaturnExplorer B=<configured build dir> OUT=<scratch dir> build_emut.sh <name> <tests|gen|drv>... -- <file.cc>'<sed expression>' ...
#   (the first form of an expression is  heritage.cc@s/old/new/ ; several may be given; a file named twice gets both)
# e.g.  build_emut.sh M0 tests gen -- 'heritage.cc@s/^void FrameEscape::compute(const Funcdata \*fd,AddrSpace \*spc,bool preHeritage)$/&\n{ return; }\nvoid FrameEscape::computeOff(const Funcdata *fd,AddrSpace *spc,bool preHeritage)/'
set -e
: "${W:?}" "${B:?}" "${OUT:?}"
n=$1; shift
bins=(); while [ "$1" != "--" ]; do bins+=("$1"); shift; done; shift
mkdir -p $OUT/$n
G=$W/FrontEnd/third_party/ghidra-decompiler/cpp
declare -A done_files
for spec in "$@"; do
  f=${spec%%@*}; e=${spec#*@}
  [ -f $OUT/$n/$f ] || cp $G/$f $OUT/$n/$f
  sed -i "$e" $OUT/$n/$f
  done_files[$f]=1
done
objs=()
for f in "${!done_files[@]}"; do
  diff $G/$f $OUT/$n/$f > $OUT/$n/$f.diff || true
  test -s $OUT/$n/$f.diff
  c++ -I$G -I$W/FrontEnd/third_party/zlib -O2 -g -DNDEBUG -std=gnu++14 -Wno-sign-compare -Wno-unused-variable -Wno-unused-function -Wno-deprecated-declarations -c $OUT/$n/$f -o $OUT/$n/$f.o
  objs+=("$f")
done
cd $B
for bin in "${bins[@]}"; do
  case $bin in tests) lt=SaturnExplorerSh2DecompilerTests; src=bin/SaturnExplorerSh2DecompilerTests;; gen) lt=SaturnExplorerSh2GeneratedTests; src=bin/SaturnExplorerSh2GeneratedTests;; drv) lt=SaturnExplorerSh2DecompilerTests; src=bin/SaturnExplorerSh2DecompilerTests;; esac
  LINK=$(cat CMakeFiles/$lt.dir/link.txt)
  for f in "${objs[@]}"; do LINK=$(echo "$LINK" | sed "s#FrontEnd/third_party/ghidra-decompiler/CMakeFiles/SaturnExplorerGhidraDecomp.dir/cpp/$f.o#$OUT/$n/$f.o#"); done
  if [ $bin = drv ]; then LINK=$(echo "$LINK" | sed "s#CMakeFiles/SaturnExplorerSh2DecompilerTests.dir/FrontEnd/tests/Sh2DecompilerTests.cpp.o#$OUT/drv.o#"); fi
  LINK=$(echo "$LINK" | sed "s#-o $src#-o $OUT/$n/$bin#")
  eval "$LINK"
  echo built $n/$bin
done
