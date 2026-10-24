#!/bin/sh
# usage: B=<build dir> OUT=<driver dir> sh relink.sh : relinks drv (drv.o from bdrv.sh) against the objects now in the build dir (after a mutation + rebuild)
set -e
: "${B:?}" "${OUT:?}"
cd $B
LINK=$(cat CMakeFiles/SaturnExplorerSh2DecompilerTests.dir/link.txt | sed "s#CMakeFiles/SaturnExplorerSh2DecompilerTests.dir/FrontEnd/tests/Sh2DecompilerTests.cpp.o#$OUT/drv.o#; s#-o bin/SaturnExplorerSh2DecompilerTests#-o $OUT/drv#")
eval "$LINK"
