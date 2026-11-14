#!/bin/bash
# Area 7 review round 24: the width of the oracle relaxations of rounds 21 to 23. The sweep's first seeds with every one of them
# switched off; each seed that fails only so is a case a relaxation accepts, and its text and tokens are read by hand.
# usage: relaxed.sh <generator binary> <scratch dir> [seeds per grammar, default 4000]
B="$1"; S="$2"; N="${3:-4000}"
OFF=162,164,165,166,167,168,169,170,172,174,5142,5143,5144,5145,5147,5148,5149
ulimit -v 6000000
for cfg in "1 - 9900001" "2 - 9900001" "2 specials 10000001"; do
  set -- $cfg
  tag="g$1${2/-/}_off"
  env SE_A7_RULE_OFF=$OFF SE_GENERATED_GRAMMAR=$1 SE_GENERATED_FIRST=$3 SE_GENERATED_SEEDS=$N SE_GENERATED_CANARY=0 SE_GENERATED_SHRINK=0 \
      $( [ "$2" = specials ] && echo SE_GENERATED_ALPHABET=specials ) "$B" "$S/out_$tag" > "$S/relaxed_$tag.txt" 2>&1
  echo "$tag: $(grep -E 'failing seeds=' "$S/relaxed_$tag.txt" | grep -o 'failing seeds=[0-9]*; findings=[0-9]*')"
  rm -rf "$S/out_$tag"
done
