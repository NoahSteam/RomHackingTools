#!/bin/bash
# Area 7 round 25 mutation: the product's changes of this round with a temporary switch (SE_R25_OFF, a switch of the mutation build, removed from the final tree)
# and the oracle's rules off (SE_A7_RULE_OFF=176,178,180).
# usage: mutation.sh <SaturnExplorer build dir> <scratch dir>
B="$1"; S="$2"
ulimit -v 6000000
echo "decompiler tests, as built:      $($B/bin/SaturnExplorerSh2DecompilerTests $S/m1 2>&1 | tail -1)"
echo "decompiler tests, product off:   $(SE_R25_OFF=1 $B/bin/SaturnExplorerSh2DecompilerTests $S/m1 2>&1 | grep -c 'CHECK failed') CHECK failures"
for x in "1 9905215" "1 10105433" "2 10107462"; do
  set -- $x
  run() { env "$@" SE_GENERATED_GRAMMAR=$g SE_GENERATED_SEED=$sd SE_GENERATED_CANARY=0 SE_GENERATED_SHRINK=0 $B/bin/SaturnExplorerSh2GeneratedTests $S/m2 2>&1 | grep -o 'failing seeds=[0-9]*; findings=[0-9]*' | tail -1; }
  g=$1; sd=$2
  echo "grammar $1 seed $2: fixed [$(run X=1)]; product off [$(run SE_R25_OFF=1)]; oracle rules off [$(run SE_A7_RULE_OFF=180)]; both off [$(run SE_R25_OFF=1 SE_A7_RULE_OFF=176,178,180)]"
done
D=$(cd "$(dirname "$0")" && pwd)
for off in "" 1; do echo "fixtures25.s, SE_R25_OFF=$off:"; env ${off:+SE_R25_OFF=1} bash "$D/run25.sh" "$B/bin/SaturnExplorerSh2GeneratedTests" "$S" a7_178 2>&1 | grep -E '^=== |^  \[' | sed 's/\] .*/]/'; done
