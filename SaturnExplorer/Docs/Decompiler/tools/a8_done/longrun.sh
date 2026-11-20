#!/bin/bash
# Area 8, done check: the overnight-length run of the trust indicators' generators, in chunks, one job at a time, each under
# ulimit -v 3500000 with its own log (the command, the test's output, `time`, the exit code).
#   longrun.sh <build dir> <log dir> <chunk>
# Chunks (each a piece that fits a two-hour slot; run them in order, never two at once):
#   1  Follow rig (mix 8, then mix 9), SE_TRUST_FOLLOW_FOCUS=1, hover on, seeds from 2,000,000; then with SE_TRUST_NOHOVER=1 from 2,100,000;
#      200,000 operations per mix
#   2  the same with SE_TRUST_FOLLOW_CPU=1, hover on, from 2,200,000; 400,000 operations per mix (as are 3 to 5)
#   3  focus, CPU, SE_TRUST_NOHOVER=1, from 2,300,000
#   4  no focus operation, hover on, from 2,400,000
#   5  no focus operation, CPU, SE_TRUST_NOHOVER=1, from 2,500,000
#   6  the area 1 model, SE_MODEL_SEEDS=10000 (it has no start seed: seeds 1 to 10000; 1 to 1200 were run before)
#   7  mixes 0 to 7 with SE_TRUST_HIDDEN_RENAME=1, hover on, from 2,600,000 (6,000 operations each, mix 2 4,000: it is the slow one)
# The binaries are copied to <log dir>/bin first, so a rebuild during the run changes nothing. A job that fails prints
# "FOLLOW FAILURE, seed N" (replay: SE_TRUST_FOLLOW_SEED=N, with SE_TRUST_FOLLOW_AUTO=1 for mix 9, and the job's own knobs),
# "MODEL FAILURE" for mixes 0 to 7 (SE_TRUST_MIX=k SE_TRUST_SEED=N) or "seed N: FAILED" for the model (SE_MODEL_SEED=N).
B=${1:?build dir}; L=${2:?log dir}; chunk=${3:?chunk}
mkdir -p "$L/bin"
for t in SaturnExplorerDecompilerTrustTests SaturnExplorerDecompilerModelTests; do
    [ -x "$L/bin/$t" ] || cp "$B/bin/$t" "$L/bin/$t" || exit 2
done
ulimit -v 3500000
failed=0   # jobs that exited nonzero (a failure or a timeout); the chunk exits 1 if any did, every job still runs
job() { # name, timeout in seconds, trust|model, VAR=value ...
    local name=$1 to=$2 kind=$3; shift 3
    local bin=$L/bin/SaturnExplorerDecompilerTrustTests
    [ "$kind" = model ] && bin=$L/bin/SaturnExplorerDecompilerModelTests
    local log=$L/$name.log t0
    t0=$(date +%s)
    echo "# $name: $* $kind, started $(date -u +%FT%TZ)" > "$log"
    ( time env "$@" timeout "$to" "$bin" "$L/scratch_$name" ) >> "$log" 2>&1
    local rc=$?
    [ "$rc" -ne 0 ] && failed=$((failed + 1))
    echo "# exit $rc, wall $(( $(date +%s) - t0 )) s" >> "$log"
    rm -rf "$L/scratch_$name"
    echo "$name exit $rc wall $(( $(date +%s) - t0 )) s: $(grep -E 'sequences, [0-9]+ failed|seed\(s\), [0-9]+ failed' "$log" | cut -c1-60)"
}
follow() { # name, first seed, operations per mix, VAR=value ...
    local name=$1 from=$2 steps=$3; shift 3
    for mix in 8 9; do
        job "${name}_mix$mix" 3500 trust SE_TRUST_MIX=$mix SE_TRUST_STEPS="$steps" SE_TRUST_FROM="$from" SE_TRUST_OOM_STRIDE=0 "$@"
    done
}
case $chunk in
1) follow a_focus_hover 2000000 200000 SE_TRUST_FOLLOW_FOCUS=1
   follow b_focus_nohover 2100000 200000 SE_TRUST_FOLLOW_FOCUS=1 SE_TRUST_NOHOVER=1 ;;
2) follow c_focus_cpu_hover 2200000 400000 SE_TRUST_FOLLOW_FOCUS=1 SE_TRUST_FOLLOW_CPU=1 ;;
3) follow d_focus_cpu_nohover 2300000 400000 SE_TRUST_FOLLOW_FOCUS=1 SE_TRUST_FOLLOW_CPU=1 SE_TRUST_NOHOVER=1 ;;
4) follow e_plain_hover 2400000 400000 SE_TRUST_HIDDEN_RENAME_UNUSED=0 ;;
5) follow f_plain_cpu_nohover 2500000 400000 SE_TRUST_FOLLOW_CPU=1 SE_TRUST_NOHOVER=1 ;;
6) job model_10000 7000 model SE_MODEL_SEEDS=10000 ;;
7) for mix in 0 1 3 4 5 6 7; do
       job g_mix$mix 850 trust SE_TRUST_MIX=$mix SE_TRUST_STEPS=6000 SE_TRUST_FROM=2600000 SE_TRUST_OOM_STRIDE=0 SE_TRUST_HIDDEN_RENAME=1
   done
   job g_mix2 1100 trust SE_TRUST_MIX=2 SE_TRUST_STEPS=4000 SE_TRUST_FROM=2600000 SE_TRUST_OOM_STRIDE=0 SE_TRUST_HIDDEN_RENAME=1 ;;
*) echo "chunk 1 to 7"; exit 2 ;;
esac
if [ "$failed" -ne 0 ]; then
    echo "chunk $chunk done: $failed job(s) failed"
    exit 1
fi
echo "chunk $chunk done"
