#!/bin/bash
# Area 7 round 26 mutation runs on the mutation build (this tree with SE_R26_OFF, a temporary switch of each product change: 181, 182a,
# 183, 186, 189; not in the final tree). usage: mutation.sh <generator binary> <scratch dir>
B="$1"; S="$2"
ulimit -v 6000000
pins() {  # the default run's pins of this round: which fail
  env "$@" "$B" "$S/mut_out" 2>&1 | grep -E "^pin 'area 7 round 26|^pin 'A7-18[1-9]" | grep -v ": 0 finding" | sed 's/^/    /'
}
echo "== as built"; pins
echo "== product off (SE_R26_OFF=all)"; pins SE_R26_OFF=all
for p in 181 182a 183 186 189; do echo "== product change $p off"; pins SE_R26_OFF=$p; done
for r in 184 185 186 187 188; do echo "== oracle rule $r off"; pins SE_A7_RULE_OFF=$r; done
echo "== sweep seeds with an oracle rule off"
for x in "1 10402034 187" "1 10405012 188"; do
  set -- $x
  echo "  grammar $1 seed $2 rule $3 off: $(SE_A7_RULE_OFF=$3 SE_GENERATED_GRAMMAR=$1 SE_GENERATED_SEED=$2 SE_GENERATED_CANARY=0 SE_GENERATED_SHRINK=0 "$B" "$S/mut_out" 2>&1 | grep -o 'failing seeds=[0-9]*; findings=[0-9]*')  on: $(SE_GENERATED_GRAMMAR=$1 SE_GENERATED_SEED=$2 SE_GENERATED_CANARY=0 SE_GENERATED_SHRINK=0 "$B" "$S/mut_out" 2>&1 | grep -o 'failing seeds=[0-9]*; findings=[0-9]*')"
done
echo "== sweep seed 10407403's shrunk words, product change 189 on and off"
for p in none 189; do
  echo "  189 off=$p: $(SE_R26_OFF=$p SE_GENERATED_WORDS="D609 4315 8D01 6203 6055 2601 C840 8901 E00B CD81 2108 8901 8802 8D00 2621 E00E 025D 000B 0009 0009 2600 1028" SE_GENERATED_VOLATILE=26001020-2600102f SE_GENERATED_CANARY=0 "$B" "$S/mut_out" 2>&1 | grep -E '^ *\[.:map' | cut -c1-120)"
done
rm -rf "$S/mut_out"
