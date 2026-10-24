#!/bin/bash
# usage: replayfix13.sh <rulefuzz13 built by build13.sh against the engine under test> <scratch dir>
# Replays the rule-level seeds of ledger rows A2-82 to A2-87 and A2-90 (one graph each, depth 3) with the environment each was found with. On an engine with patch 0064
# every seed has 0 wrong applications; on the engine of round 13's head (0062 only) each of the 16 has 1. Exit status 1 if any seed is wrong.
bin=$1; scratch=$2; mkdir -p "$scratch"; bad=0
run() { # row, environment, seed
  local row=$1 envs=$2 seed=$3
  local line
  line=$( ( ulimit -v 4000000; env $envs timeout 300 "$bin" "$scratch/x" $seed 1 3 2>&1 ) | grep '^rulefuzz13')
  local wrong; wrong=$(echo "$line" | sed -E 's/.*graphs, ([0-9]+) wrong applications.*/\1/')
  echo "$row $envs seed $seed: $wrong wrong"
  [ "$wrong" = "0" ] || bad=1
}
run A2-82 "RF_WIDE=1 RF_CONSUME=1" 1059
run A2-82 "RF_TMPL=1 RF_CONSUME=1" 34219
run A2-83 "RF_CONSUME=1" 373
run A2-83 "RF_TMPL=1 RF_CONSUME=1" 6495
run A2-84 "RF_CONSUME=1" 11955
run A2-85 "RF_CONSUME=1" 266405
run A2-85 "RF_CONSUME=1" 410191
run A2-85 "RF_WIDE=1" 3949
run A2-85 "RF_WIDE=1" 5940
run A2-85 "RF_WIDE=1" 793
run A2-85 "RF_WIDE=1" 154892
run A2-85 "RF_WIDE=1" 102166
run A2-86 "RF_CONSUME=1" 685387
run A2-87 "RF_CONSUME=1" 304378
run A2-87 "RF_CONSUME=1" 411738
run A2-90 "RF_WIDE=1" 301341
exit $bad
