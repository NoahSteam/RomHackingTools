#!/bin/bash
# Area 7 review round 24: run each hand fixture of fixtures24.s through the generator's checks with the tokens printed
# (SE_GENERATED_TOKENS=3: after each function's text, the line a click on each body instruction goes to first).
# usage: run24.sh <generator binary> <scratch dir> [name-substring]   (SE_A7_RULE_OFF etc. pass through)
B="$1"; S="$2"; D=$(cd "$(dirname "$0")" && pwd)
ulimit -v 6000000
python3 -I "$D/asm24.py" "$D/fixtures24.s" "$3" | while IFS=$'\t' read -r name words extra; do
  echo "=== $name"
  echo "words: $words  $extra"
  env $(echo "$extra" | tr ';' '\n' | sed -n 's/^\(VOLATILE\|POLL\|ALIAS\|FUNCS\)=/SE_GENERATED_\1=/p') \
      SE_GENERATED_WORDS="$words" SE_GENERATED_TOKENS=3 SE_GENERATED_CANARY=0 "$B" "$S/run24_out" 2>&1 \
      | grep -v '^\s*$'
done
