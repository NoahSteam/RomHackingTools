#!/bin/bash
# Builds a8fix_probe from the flags and link line of the trust test's own target in an existing build tree.
#   build_probe.sh <build dir> [-j]
# The tree must have SaturnExplorerDecompilerTrustTests built (its objects are reused; only the probe's translation unit is new).
set -e
B=${1:?build dir}
HERE=$(cd "$(dirname "$0")" && pwd)
SRC=$(cd "$HERE/../../../.." && pwd)        # SaturnExplorer/
D=$B/CMakeFiles/SaturnExplorerDecompilerTrustTests.dir
FLAGS=$(grep '^CXX_FLAGS' $D/flags.make | sed 's/^CXX_FLAGS = //')
DEFS=$(grep '^CXX_DEFINES' $D/flags.make | sed 's/^CXX_DEFINES = //')
INCS=$(grep '^CXX_INCLUDES' $D/flags.make | sed 's/^CXX_INCLUDES = //')
mkdir -p $B/probe
cd $B
# the probe's own object, in place of the trust test's translation unit
eval g++ $DEFS $INCS $FLAGS -I$SRC/FrontEnd/tests -I$HERE -c $HERE/a8fix_probe.cpp -o $B/probe/a8fix_probe.o
# link line of the trust test, with its own translation unit replaced by the probe's
sed -e 's|CMakeFiles/SaturnExplorerDecompilerTrustTests.dir/FrontEnd/tests/DecompilerTrustTests.cpp.o|probe/a8fix_probe.o|' \
    -e 's|bin/SaturnExplorerDecompilerTrustTests|probe/a8fix_probe|' $D/link.txt > $B/probe/link.sh
bash $B/probe/link.sh
echo built $B/probe/a8fix_probe
