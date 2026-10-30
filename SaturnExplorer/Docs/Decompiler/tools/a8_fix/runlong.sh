#!/bin/bash
# One piece (at most 20 minutes) of a long run of the area 8 fixer's probe.
#   runlong.sh <build dir> <mix 0-7> <first seed> <programs> <concat>
# Full output in <build dir>/out_<mix>_<first>.txt, the summary lines appended to <build dir>/long_<mix>.log. The probe is built by
# build_probe.sh. SE_TRUST_WIDTH=537 runs the sequences in the default layout's narrowest panel. The Decompile operation, the Stop-and-
# interleave generator and the status-row check are the trust test's own now; unlike round 2's runlong.sh, nothing needs setting to model
# Decompile after a Stop (the panel asks again, A8-18).
B=${1:?build dir}; mix=$2; first=$3; programs=$4; concat=$5
cd "$B"
ulimit -v 3500000
timeout 1200 ./probe/a8fix_probe "$B/t_long_$mix" long "$mix" "$first" "$programs" "$concat" > "$B/out_${mix}_${first}.txt" 2>&1
echo "exit $?" >> "$B/out_${mix}_${first}.txt"
grep -E "^long mix|^status line|^FAILED|^exit" "$B/out_${mix}_${first}.txt" | cut -c1-200 >> "$B/long_$mix.log"
