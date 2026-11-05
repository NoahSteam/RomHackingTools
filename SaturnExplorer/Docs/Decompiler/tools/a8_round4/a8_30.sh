#!/bin/bash
# Area 8, round 4, A8-30: Follow PC turned off pins the entry the finder guessed, and the panel then shows it as certain. The window's
# status row and amber lines are printed after each step: "(entry from rts scan, 60%)" and "Entry: guessed from the previous rts; ..."
# before the click, "(entry from rts scan)" and no Entry line after it, for the same function on the same snapshot. The trust model
# accepts both (it expects no doubt for an Entry target), so the script ends "ok": the fixture is the text printed.
#   a8_30.sh <build dir>
B=${1:?build dir}
T=$B/bin/SaturnExplorerDecompilerTrustTests
S=$(mktemp -d)
ulimit -v 3500000
SE_TRUST_FOLLOW_DUMP=1 SE_TRUST_FOLLOW_SCRIPT="refresh; follow; refresh; follow" timeout 300 $T $S/x 2>&1 | grep -E "^\[|entry from|Entry:|^follow script"
rm -rf "$S"
