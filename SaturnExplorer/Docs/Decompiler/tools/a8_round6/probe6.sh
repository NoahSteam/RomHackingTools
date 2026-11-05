#!/bin/bash
# Area 8, round 6: A8-36 to A8-39 on the trust test's small rig. Prints, per step, the status row's "(entry from ...)" label,
# Uncertainties, the entry box, the stale tooltip, the red line and the Copy marks.
#   A, I  Enter on the untouched entry box after a Follow PC pin, as the Stopped / NoRegisters tooltip says (A8-36); B the same under Follow PC
#   F, G  a pinned entry asked again after its symbol is removed / its call stack is gone (A8-37)
#   C, D  Follow PC off with nothing found and nothing shown (A8-38)
#   H     the PC moves where no function is: the stale output's own doubt (A8-39)
#   E     a Decompile target, the call stack gone: call-site target 80% against callee 70%, no amber line (A8-32)
#   probe6.sh <build dir>
B=${1:?build dir}
S=$(mktemp -d)
ulimit -v 3500000
SE_TRUST_PROBE6=1 timeout 300 $B/bin/SaturnExplorerDecompilerTrustTests $S/x 2>&1 | grep -A9 PROBE6
rm -rf "$S"
