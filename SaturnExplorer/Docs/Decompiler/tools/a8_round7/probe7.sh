#!/bin/bash
# Area 8, round 7: hand-built sequences against A8-33 and A8-36 to A8-39 on the trust test's small rig. Prints, per step, the status row's
# "(entry from ...)" label, Uncertainties, the entry box, the stale tooltip, the red line and the Copy marks.
#   1  a pin across snapshots (the PC moves), then a start set at the pin, Enter on the untouched box, Follow PC on
#   2  a pin, a start set inside the function below the PC, then at the pin
#   3  an entry typed that the finder finds as well, Follow PC on and off (a pin), Enter on the untouched box
#   4  Follow PC stopped, Enter on the untouched box
#   5  a pin, the same entry typed, a name given and taken away
#   6  Decompile at A+2, Enter on the untouched box, a name, a start (A8-37, A8-33)
#   7  Follow PC off with nothing found, the PC moves into a function, Enter on the empty box (A8-38)
#   8  a pin and the CPU switched
#   9  the entry box has the focus while the output moves on, then Enter (A8-42)
#   10 the Volatile popup's cut of a saved list that is too long (A8-43)
#   probe7.sh <build dir>
B=${1:?build dir}
S=$(mktemp -d)
ulimit -v 3500000
SE_TRUST_PROBE7=1 timeout 300 $B/bin/SaturnExplorerDecompilerTrustTests $S/x 2>&1 | grep -A9 PROBE7
rm -rf "$S"
