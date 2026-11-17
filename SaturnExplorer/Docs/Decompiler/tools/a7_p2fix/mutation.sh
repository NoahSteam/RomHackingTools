#!/bin/bash
# Area 7 P2 fixes (A7-189, A7-190): the pins as built and with each product change off, on the mutation build (this tree with a temporary
# switch SE_P2_OFF189 / SE_P2_OFF190 / SE_P2_OFF190c of each change; not in the final tree), and with the oracle's rule 190 off. Prints every pin of the default run with a finding.
# usage: mutation.sh <generator binary> <scratch dir>
B="$1"; S="$2"
ulimit -v 6000000
pins() {
  env "$@" SE_GENERATED_SEEDS=1 SE_GENERATED_CANARY=0 "$B" "$S/mut_out" > "$S/mut_pins.txt" 2>&1
  grep -E "^pin '.*: [1-9][0-9]* finding\(s\)$" "$S/mut_pins.txt" | sed 's/^/    /'
  echo "    ($(grep -c "^pin '" "$S/mut_pins.txt") pins; $(tail -1 "$S/mut_pins.txt"))"
}
echo "== as built"; pins
echo "== A7-189 off"; pins SE_P2_OFF189=1
echo "== A7-190 off (the operand)"; pins SE_P2_OFF190=1
echo "== A7-190 off (the copy)"; pins SE_P2_OFF190c=1
echo "== A7-190 off (the copy) and the oracle's rule 190 off"; pins SE_P2_OFF190c=1 SE_A7_RULE_OFF=190
echo "== the whole seeds, each change off"
for x in "10407403 189" "10403949 190" "10403949 190c" "10300327 190"; do
  set -- $x
  echo "  grammar 1 seed $1, $2 off: $(env SE_P2_OFF$2=1 SE_GENERATED_GRAMMAR=1 SE_GENERATED_SEED=$1 SE_GENERATED_CANARY=0 SE_GENERATED_SHRINK=0 "$B" "$S/mut_out" 2>&1 | grep -E '^ *\[[nv]:map' | cut -c1-160 | tr '\n' ' ')"
done
rm -rf "$S/mut_out" "$S/mut_pins.txt"
