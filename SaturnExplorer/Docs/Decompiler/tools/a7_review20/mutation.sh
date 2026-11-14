#!/bin/bash
# Area 7 review round 20: the oracle rules of the round with their row off (SE_A7_RULE_OFF) on the words that need them.
# usage: mutation.sh <generator binary built on master 47d5f39b's product with this round's oracle> <this round's generator binary>
#   (SCRATCH set; the binary deletes its out dir). A7-158: the store-dead rule finds master's tokens and passes them with the rule off;
#   A7-160: grammar 1 seed 9000706 fails with the rule off and passes with it on.
S=${SCRATCH:?set SCRATCH to a scratch directory}
HEAD=$1
NOW=$2
ulimit -v 6000000
count() { grep -o 'failing seeds=[0-9]*; findings=[0-9]*\|[0-9]* finding(s)' | tail -1; }
W158="E200 D704 2448 8B00 325C 2710 2720 000B 0009 0009 2600 1018"
echo "A7-158 master's tokens, rule on:  $(SE_GENERATED_WORDS="$W158" SE_GENERATED_CANARY=0 "$HEAD" "$S/r20/mut158" 2>&1 | count)"
echo "A7-158 master's tokens, rule off: $(SE_A7_RULE_OFF=158 SE_GENERATED_WORDS="$W158" SE_GENERATED_CANARY=0 "$HEAD" "$S/r20/mut158" 2>&1 | count)"
echo "A7-158 this round's tokens:       $(SE_GENERATED_WORDS="$W158" SE_GENERATED_CANARY=0 "$NOW" "$S/r20/mut158" 2>&1 | count)"
echo "A7-160 seed 9000706, rule on:     $(SE_GENERATED_GRAMMAR=1 SE_GENERATED_SEED=9000706 SE_GENERATED_CANARY=0 SE_GENERATED_SHRINK=0 "$NOW" "$S/r20/mut160" 2>&1 | count)"
echo "A7-160 seed 9000706, rule off:    $(SE_A7_RULE_OFF=160 SE_GENERATED_GRAMMAR=1 SE_GENERATED_SEED=9000706 SE_GENERATED_CANARY=0 SE_GENERATED_SHRINK=0 "$NOW" "$S/r20/mut160" 2>&1 | count)"
