#!/bin/bash
# Area 8, round 5, A8-32 and A8-33: the status row's "(entry from ...)" label against the Entry line, when the call stack or the user's
# function starts change under an entry that stays the same. Prints, per step, the label, Uncertainties and the Copy marks.
#   probe5.sh <build dir>
B=${1:?build dir}
S=$(mktemp -d)
ulimit -v 3500000
SE_TRUST_PROBE5=1 timeout 300 $B/bin/SaturnExplorerDecompilerTrustTests $S/x 2>&1 | grep -A3 PROBE5
rm -rf "$S"
