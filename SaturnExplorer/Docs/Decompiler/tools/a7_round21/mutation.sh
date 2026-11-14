#!/bin/bash
# Area 7 round 21 / area 5 routed 9006019: the mutation runs. usage: mutation.sh <generator binary> (SCRATCH set; the binary deletes its out dir)
#   A7-162: grammar 2 seed 9002796's shrunk replay line fails with SE_A7_RULE_OFF=162 (n:map-const-join), passes with the rule on.
#   A5-142: grammar 2 seed 9006019 fails with SE_A7_RULE_OFF=5142 (n:folded-read-after-pointer-write), passes with the rule on.
#   The default run (every pin) with each rule off reports the pins' CHECK failures.
S=${SCRATCH:?set SCRATCH to a scratch directory}
B=$1
ulimit -v 6000000
count() { grep -o 'failing seeds=[0-9]*; findings=[0-9]*\|[0-9]* finding(s)' | tail -1; }
W162="2F86 2F96 2FA6 2FC6 0C2A 7FE8 6843 6953 6A63 7F18 4C2A 6CF6 6AF6 69F6 68F6 000B 0009 2FE6 7FE8 6EF3 E10E 2F16 E0AE 61F6 2019 E105 3412 8915 4408 C702 014E 412B 0009 0009 0600 9058 0600 905C 0600 905C 0600 9060 0600 9060 A005 0009 A003 0009 A001 0009 210B 6060 7E18 6FE3 6EF6 000B 0009"
run162() { env "$@" SE_GENERATED_WORDS="$W162" SE_GENERATED_FUNCS="0/0/22,22/22/72" SE_GENERATED_TABLES="3e:4:5:64:58,5c,5c,60,60" SE_GENERATED_RETURNS="0,1" SE_GENERATED_CANARY=0 "$B" "$S/mut162" 2>&1 | count; }
echo "A7-162 shrunk, rule on:  $(run162 X=1)"
echo "A7-162 shrunk, rule off: $(run162 SE_A7_RULE_OFF=162)"
echo "A5-142 seed 9006019, rule on:  $(SE_GENERATED_GRAMMAR=2 SE_GENERATED_SEED=9006019 SE_GENERATED_CANARY=0 SE_GENERATED_SHRINK=0 "$B" "$S/mut142" 2>&1 | count)"
echo "A5-142 seed 9006019, rule off: $(SE_A7_RULE_OFF=5142 SE_GENERATED_GRAMMAR=2 SE_GENERATED_SEED=9006019 SE_GENERATED_CANARY=0 SE_GENERATED_SHRINK=0 "$B" "$S/mut142" 2>&1 | count)"
for r in 162 5142; do
  echo "default run (pins), SE_A7_RULE_OFF=$r: $(SE_A7_RULE_OFF=$r "$B" "$S/mutpins" 2>&1 | grep -c 'CHECK failed') CHECK failure(s)"
done
echo "default run (pins), rules on: $("$B" "$S/mutpins" 2>&1 | grep -c 'CHECK failed') CHECK failure(s)"
