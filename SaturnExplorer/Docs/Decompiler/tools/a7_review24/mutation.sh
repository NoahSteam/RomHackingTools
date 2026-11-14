#!/bin/bash
# Area 7 review round 24 mutation: the A7-175 fixtures and pins with the product's new pass off (SE_R24_OFF, a temporary switch of the
# mutation build, removed from the final tree) and with the oracle's rule off (SE_A7_RULE_OFF=175).
# usage: mutation.sh <SaturnExplorer build dir> <scratch dir>
B="$1"; S="$2"
ulimit -v 6000000
echo "decompiler tests, as built:      $($B/bin/SaturnExplorerSh2DecompilerTests $S/m1 2>&1 | tail -1)"
echo "decompiler tests, product off:   $(SE_R24_OFF=1 $B/bin/SaturnExplorerSh2DecompilerTests $S/m1 2>&1 | grep -c 'CHECK failed') CHECK failures"
# (the rule off is checked inside TestArea7Review24: the damaged result passes with SE_A7_RULE_OFF=175)
for x in "1 9900754" "2s 10000825"; do
  set -- $x; g=$1; al=; [ $g = 2s ] && { g=2; al=specials; }
  on=$(env ${al:+SE_GENERATED_ALPHABET=$al} SE_GENERATED_GRAMMAR=$g SE_GENERATED_SEED=$2 SE_GENERATED_CANARY=0 SE_GENERATED_SHRINK=0 $B/bin/SaturnExplorerSh2GeneratedTests $S/m2 2>&1 | grep -o 'failing seeds=[0-9]*; findings=[0-9]*' | tail -1)
  off=$(env SE_R24_OFF=1 ${al:+SE_GENERATED_ALPHABET=$al} SE_GENERATED_GRAMMAR=$g SE_GENERATED_SEED=$2 SE_GENERATED_CANARY=0 SE_GENERATED_SHRINK=0 $B/bin/SaturnExplorerSh2GeneratedTests $S/m2 2>&1 | grep -o 'failing seeds=[0-9]*; findings=[0-9]*' | tail -1)
  both=$(env SE_R24_OFF=1 SE_A7_RULE_OFF=175 ${al:+SE_GENERATED_ALPHABET=$al} SE_GENERATED_GRAMMAR=$g SE_GENERATED_SEED=$2 SE_GENERATED_CANARY=0 SE_GENERATED_SHRINK=0 $B/bin/SaturnExplorerSh2GeneratedTests $S/m2 2>&1 | grep -o 'failing seeds=[0-9]*; findings=[0-9]*' | tail -1)
  echo "grammar $1 seed $2: fixed [$on]; product off [$off]; product and rule off [$both]"
done
# the hand fixtures (tools/a7_review24/fixtures24.s), product on and off: the A7-171 variants' findings
D=$(cd "$(dirname "$0")" && pwd)
for off in "" 1; do echo "fixtures24.s, SE_R24_OFF=$off:"; env ${off:+SE_R24_OFF=1} bash "$D/run24.sh" "$B/bin/SaturnExplorerSh2GeneratedTests" "$S" a7_171 2>&1 | grep -E '^=== |^  \[' | sed 's/\] .*/]/'; done
