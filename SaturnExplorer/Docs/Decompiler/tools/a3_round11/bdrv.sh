#!/bin/sh
# Builds the fixture driver `drv` from the suite's own Sh2DecompilerTests.cpp (main renamed, RunMachine::Poke added, drvmain.inc appended) and the
# suite's link line. drvmain.inc, asm.py, mk.py, sum.py and the *.py fuzzers sit in the directory OUT, with the *.src fixtures.
# usage: W=<worktree>/SaturnExplorer B=<build dir of a configured tree> OUT=<scratch dir> sh bdrv.sh
set -e
: "${W:?}" "${B:?}" "${OUT:?}"
sed 's/^int main(int argc, char\*\* argv)/int suite_main(int argc, char** argv)/; s|^    void Set(const char\* reg, uint32_t v) { mState->setValue(reg, v); }|\&\n    void Poke(uint32_t a, uint32_t v) { mState->setValue(mRam, a, 4, v); }|' $W/FrontEnd/tests/Sh2DecompilerTests.cpp > $OUT/drv.cpp
echo '#include "drvmain.inc"' >> $OUT/drv.cpp
c++ -O1 -g -DNDEBUG -std=gnu++14 -I$OUT -I$W/FrontEnd/src -I$W/FrontEnd/tests -I$W/include -I$W/FrontEnd/third_party/ghidra-decompiler/cpp -I$W/FrontEnd/third_party/zlib -c $OUT/drv.cpp -o $OUT/drv.o
cd $B
LINK=$(cat CMakeFiles/SaturnExplorerSh2DecompilerTests.dir/link.txt | sed "s#CMakeFiles/SaturnExplorerSh2DecompilerTests.dir/FrontEnd/tests/Sh2DecompilerTests.cpp.o#$OUT/drv.o#; s#-o bin/SaturnExplorerSh2DecompilerTests#-o $OUT/drv#")
eval "$LINK"
