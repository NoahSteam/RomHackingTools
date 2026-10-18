#!/bin/sh
# Builds the fixture driver `drv` from the suite's own Sh2DecompilerTests.cpp (main renamed, drvmain.inc appended) and the suite's link line.
# usage: W=<worktree>/SaturnExplorer B=<build dir> OUT=<scratch dir with these files> sh bdrv.sh
set -e
: "${W:?}" "${B:?}" "${OUT:?}"
sed 's/^int main(int argc, char\*\* argv)/int suite_main(int argc, char** argv)/' $W/FrontEnd/tests/Sh2DecompilerTests.cpp > $OUT/drv.cpp
echo '#include "drvmain.inc"' >> $OUT/drv.cpp
c++ -O1 -g -DNDEBUG -std=gnu++14 -I$OUT -I$W/FrontEnd/src -I$W/FrontEnd/tests -I$W/include -I$W/FrontEnd/third_party/ghidra-decompiler/cpp -I$W/FrontEnd/third_party/zlib -c $OUT/drv.cpp -o $OUT/drv.o
cd $B
LINK=$(cat CMakeFiles/SaturnExplorerSh2DecompilerTests.dir/link.txt | sed "s#CMakeFiles/SaturnExplorerSh2DecompilerTests.dir/FrontEnd/tests/Sh2DecompilerTests.cpp.o#$OUT/drv.o#; s#-o bin/SaturnExplorerSh2DecompilerTests#-o $OUT/drv#")
eval "$LINK"
