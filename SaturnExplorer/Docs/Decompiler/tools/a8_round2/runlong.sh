#!/bin/bash
# One piece (at most 20 minutes) of a long run of the area 8 round 2 probe.
#   runlong.sh <build dir> <mix 0-7> <first seed> <programs> <concat>
# Full output in <build dir>/out_<mix>_<first>.txt, the summary lines appended to <build dir>/long_<mix>.log. The probe is built by
# build_probe.sh. SE_PROBE_CONTAIN_FORGETS_STOP=1 models what the panel does today with Decompile after a Stop (A8-18), so that a long run
# looks for what lies behind it; unset, the guide's reading is the model's and the run fails at it.
B=${1:?build dir}; mix=$2; first=$3; programs=$4; concat=$5
cd "$B"
ulimit -v 3500000
SE_PROBE_CONTAIN_FORGETS_STOP=${SE_PROBE_CONTAIN_FORGETS_STOP-1} timeout 1200 ./probe/a8r2_probe "$B/t_long_$mix" long "$mix" "$first" "$programs" "$concat" > "$B/out_${mix}_${first}.txt" 2>&1
echo "exit $?" >> "$B/out_${mix}_${first}.txt"
grep -E "^long mix|^status line|^FAILED|^grey-only" "$B/out_${mix}_${first}.txt" | cut -c1-200 >> "$B/long_$mix.log"
