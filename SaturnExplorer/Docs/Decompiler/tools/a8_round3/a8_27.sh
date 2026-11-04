#!/bin/bash
# Area 8, round 3, A8-27: the Follow PC seeds that fail NOT REFRESHED (Stop, then the Follow PC box clicked onto the question that was
# stopped: the Stop is forgotten and nothing is asked). All eight pass with `mAskAgain = true;` after the checkbox's SetTarget.
#   a8_27.sh <build dir>        (SaturnExplorerDecompilerTrustTests built in it)
B=${1:?build dir}
T=$B/bin/SaturnExplorerDecompilerTrustTests
S=$(mktemp -d)
ulimit -v 3500000
for s in 20149 20442 21097; do SE_TRUST_FOLLOW_SEED=$s timeout 300 $T $S/x 2>&1 | grep -E "^follow seed|TRUST" | cut -c1-200; done
for s in 20297 20729 20793 21058 21275; do SE_TRUST_FOLLOW_AUTO=1 SE_TRUST_FOLLOW_SEED=$s timeout 300 $T $S/x 2>&1 | grep -E "^follow seed|TRUST" | cut -c1-200; done
rm -rf "$S"
