#!/bin/bash
# Area 8, round 8: the A8-42 and A8-43 fixes attacked on the trust test's small rig. Prints, per step, the status row's "(entry from ...)"
# label, Uncertainties, the entry box (and whether it has the focus), the stale tooltip, the red line and the Copy marks; for the Volatile
# popup, its drawn text, the saved list and the box.
#   E1-E3  Follow PC on C; the box clicked, '1' typed, the edit taken back (Escape, Backspace, Ctrl+Z); Enter   (A8-46)
#   E4     the box focused while A installs; '1' typed and deleted (the box reads C); Enter
#   E5     the box focused; the PC to A with the engine held; Stop; Enter on the untouched box
#   E6     the box focused; the CPU switched (PC in A) without a click; Enter; the CPU back
#   E7     the box focused; C named; Enter; the name erased
#   E8     the box focused; the PC into the BIOS; Enter
#   E9     A typed; Follow PC on (C); the box focused; the PC to A; Enter
#   E10    the box focused with an older address than the output; the box's own tooltip; Enter
#   V2046-V2049  the Volatile popup drawn and driven: saved lists of 2046 to 2049 characters that parse; Apply unchanged, an edit made and
#          taken back, Escape                                                                              (A8-48)
#   Vmb    a saved list cut in the middle of a two-byte character
#   probe8.sh <build dir>
B=${1:?build dir}
S=$(mktemp -d)
ulimit -v 3500000
SE_TRUST_PROBE8=1 SE_TRUST_OOM_STRIDE=0 timeout 600 $B/bin/SaturnExplorerDecompilerTrustTests $S/x 2>&1 | grep -A8 PROBE8
rm -rf "$S"
