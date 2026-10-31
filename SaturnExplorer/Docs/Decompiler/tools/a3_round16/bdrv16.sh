#!/bin/sh
# Builds the fixture driver `drv` from the suite's own Sh2DecompilerTests.cpp (main renamed, RunMachine::Poke added, drvmain16.inc appended) and the
# suite's link line. drvmain16.inc, asm.py, mk.py, sum.py and the *.py fuzzers sit in the directory OUT, with the *.src fixtures.
# usage: W=<worktree>/SaturnExplorer B=<build dir of a configured tree> OUT=<scratch dir> sh bdrv.sh
set -e
: "${W:?}" "${B:?}" "${OUT:?}"
sed 's/^int main(int argc, char\*\* argv)/int suite_main(int argc, char** argv)/; s|^    void Set(const char\* reg, uint32_t v) { mState->setValue(reg, v); }|&\n    void Poke(uint32_t a, uint32_t v) { mState->setValue(mRam, a, 4, v); }|' $W/FrontEnd/tests/Sh2DecompilerTests.cpp > $OUT/drv.cpp
# (A3-98) The sed above must have added Poke; a drifted Set line, or a `\&` for the match, leaves it out and the driver does not compile.
grep -q 'void Poke(uint32_t a, uint32_t v)' $OUT/drv.cpp || { echo "bdrv.sh: RunMachine::Poke was not added (the Set line of the suite changed?)" >&2; exit 1; }
echo '#include "drvmain16.inc"' >> $OUT/drv.cpp
c++ -O1 -g -DNDEBUG -std=gnu++14 -I$OUT -I$W/FrontEnd/src -I$W/FrontEnd/tests -I$W/include -I$W/FrontEnd/third_party/ghidra-decompiler/cpp -I$W/FrontEnd/third_party/zlib -c $OUT/drv.cpp -o $OUT/drv.o
cd $B
LINK=$(cat CMakeFiles/SaturnExplorerSh2DecompilerTests.dir/link.txt | sed "s#CMakeFiles/SaturnExplorerSh2DecompilerTests.dir/FrontEnd/tests/Sh2DecompilerTests.cpp.o#$OUT/drv.o#; s#-o bin/SaturnExplorerSh2DecompilerTests#-o $OUT/drv#")
eval "$LINK"
# ...and the driver must run a fixture it was given (a smoke function: rts ; mov #1,r0).
printf 'name smoke\nasm 06009000\nrts\nmov #1,r0\nendasm\nargs 0 0 0 0\nend\n' > $OUT/smoke.src
( cd $OUT && python3 mk.py smoke.src > smoke.src.spec && mkdir -p scr && ./drv $OUT/scr smoke.src.spec > smoke.out 2>&1 )
grep -q 'return 1;' $OUT/smoke.out || { echo "bdrv.sh: drv built but does not run a fixture (see $OUT/smoke.out)" >&2; exit 1; }
echo "bdrv.sh: drv built and runs a fixture"
