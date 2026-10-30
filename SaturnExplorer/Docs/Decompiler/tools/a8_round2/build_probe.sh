#!/bin/bash
# Builds a8r2_probe from the flags and link line of the trust test's own target in an existing build tree.
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
g++ $DEFS $INCS $FLAGS -I$SRC/FrontEnd/tests -I$HERE -c $HERE/a8r2_probe.cpp -o $B/probe/a8r2_probe.o
LINK=$(cat $D/link.txt)
OBJS=$(echo "$LINK" | tr ' ' '\n' | grep '\.o$' | grep -v 'DecompilerTrustTests.cpp.o' | tr '\n' ' ')
RESTSTART=$(echo "$LINK" | tr ' ' '\n' | grep -v '\.o$' | tr '\n' ' ')
# link line: compiler, flags (non-object words), objects but the trust test's, the probe's object
CXX=$(echo "$LINK" | awk '{print $1}')
LIBS=$(echo "$LINK" | tr ' ' '\n' | grep -E '^(-l|-Wl|-pthread|.*\.a$)' | tr '\n' ' ')
$CXX $FLAGS $OBJS $B/probe/a8r2_probe.o -o $B/probe/a8r2_probe $LIBS
echo built $B/probe/a8r2_probe
