#!/bin/bash
# A generator binary whose Sh2Decompiler.cpp is mutated by a sed expression; the worktree is untouched.
# usage: W=<worktree>/SaturnExplorer B=<configured build dir that built SaturnExplorerSh2GeneratedTests> OUT=<scratch dir> build_mut.sh <name> '<sed expression>'
# e.g.   build_mut.sh A 's/effects.storesBelow = mStoresBelow;/effects.storesBelow = 0;/'   (the round 11 instrument check: the generator stays at 0 failing seeds)
set -e
: "${W:?}" "${B:?}" "${OUT:?}"
n=$1; expr=$2
mkdir -p $OUT/$n
sed "$expr" $W/FrontEnd/src/Decompiler/Sh2Decompiler.cpp > $OUT/$n/Sh2Decompiler.cpp
diff $W/FrontEnd/src/Decompiler/Sh2Decompiler.cpp $OUT/$n/Sh2Decompiler.cpp > $OUT/$n/mutation.diff || true
test -s $OUT/$n/mutation.diff
c++ -I$W/FrontEnd/src/Decompiler -I$W/FrontEnd/src -I$W/FrontEnd/tests -I$W/include -I$W/FrontEnd/third_party/ghidra-decompiler/cpp -I$W/FrontEnd/third_party/zlib -O2 -g -DNDEBUG -std=gnu++14 -c $OUT/$n/Sh2Decompiler.cpp -o $OUT/$n/Sh2Decompiler.o
cd $B
LINK=$(sed "s#CMakeFiles/SaturnExplorerSh2GeneratedTests.dir/FrontEnd/src/Decompiler/Sh2Decompiler.cpp.o#$OUT/$n/Sh2Decompiler.o#; s#-o bin/SaturnExplorerSh2GeneratedTests#-o $OUT/$n/gen#" CMakeFiles/SaturnExplorerSh2GeneratedTests.dir/link.txt)
eval "$LINK"
echo built $n
