#!/bin/sh
# usage: W=<worktree SaturnExplorer dir> B=<build> OUT=<driver dir> sh mutant.sh <name> <python edit script> : applies the edit to the tree, rebuilds the decompiler test target, relinks drv under the name drv_<name>, restores the tree (git checkout).
set -e
: "${W:?}" "${B:?}" "${OUT:?}"
name=$1; edit=$2
cd $W
python3 $edit
( cmake --build $B -j2 --target SaturnExplorerSh2DecompilerTests > $OUT/mut_$name.build.log 2>&1 ) || { git checkout -- . ; echo "build failed"; tail -5 $OUT/mut_$name.build.log; exit 1; }
B=$B OUT=$OUT sh $(dirname $0)/relink.sh
mv $OUT/drv $OUT/drv_$name
git checkout -- .
echo "mutant $name built"
