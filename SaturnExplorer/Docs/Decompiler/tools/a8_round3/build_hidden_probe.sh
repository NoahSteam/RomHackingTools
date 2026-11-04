#!/bin/bash
# Area 8, round 3: the trust test as it would run once A8-25 is fixed -- a rename undone while the panel is hidden in the generated
# sequences -- without editing area 1's file. Copies DecompilerTrustTests.cpp and DecompilerModelTests.cpp into <build dir>/a8r3_hidden,
# adds A8-25's proposed excuse to the copy of area 1's STALE FOREVER check and drops the generator's guard in the copy of the trust test,
# and builds them with the trust test's own flags and link line.
#   build_hidden_probe.sh <build dir>        (SaturnExplorerDecompilerTrustTests must be built in it)
#   then: SE_TRUST_MIX=<k> SE_TRUST_STEPS=<n> SE_TRUST_FROM=<seed> <build dir>/a8r3_hidden/probe <scratch dir>
set -e
B=${1:?build dir}
HERE=$(cd "$(dirname "$0")" && pwd)
SRC=$(cd "$HERE/../../../.." && pwd)        # SaturnExplorer/
T=$SRC/FrontEnd/tests
O=$B/a8r3_hidden
D=$B/CMakeFiles/SaturnExplorerDecompilerTrustTests.dir
mkdir -p $O
cp $T/DecompilerTrustTests.cpp $T/DecompilerModelTests.cpp $O/
# A8-25's fix as filed: a moved names generation is excused for a panel that is not asked (hidden).
sed -i 's/const bool excused = (haveStopped \&\& SigOf(held).Same(stopped)) || runner.Disabled() || noEntry != 0;/const bool excused = (haveStopped \&\& SigOf(held).Same(stopped)) || runner.Disabled() || noEntry != 0 || (!visible \&\& namesGeneration != shownNamesGen);/' $O/DecompilerModelTests.cpp
grep -q '(!visible && namesGeneration != shownNamesGen)' $O/DecompilerModelTests.cpp
# ...and the guard it allows to go.
sed -i 's/if (!hidden \&\& rng.Chance(35))/if (rng.Chance(35))/' $O/DecompilerTrustTests.cpp
! grep -q 'if (!hidden && rng.Chance(35))' $O/DecompilerTrustTests.cpp
FLAGS=$(grep '^CXX_FLAGS' $D/flags.make | sed 's/^CXX_FLAGS = //')
DEFS=$(grep '^CXX_DEFINES' $D/flags.make | sed 's/^CXX_DEFINES = //')
INCS=$(grep '^CXX_INCLUDES' $D/flags.make | sed 's/^CXX_INCLUDES = //')
cd $B
eval g++ $DEFS -I$O $INCS $FLAGS -c $O/DecompilerTrustTests.cpp -o $O/probe.o
sed -e "s|CMakeFiles/SaturnExplorerDecompilerTrustTests.dir/FrontEnd/tests/DecompilerTrustTests.cpp.o|$O/probe.o|" \
    -e "s|-o bin/SaturnExplorerDecompilerTrustTests|-o $O/probe|" $D/link.txt > $O/link.sh
bash $O/link.sh
echo built $O/probe
