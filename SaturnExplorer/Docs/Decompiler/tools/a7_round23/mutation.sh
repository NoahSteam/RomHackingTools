#!/bin/bash
# Area 7 round 23: the mutation runs. usage: mutation.sh <generator binary> (SCRATCH set; the binary deletes its out dir)
# Each row's seed (whole) and shrunk replay line fails with the fix off and passes with it on:
#   oracle rows, SE_A7_RULE_OFF=<row>: A7-172 (grammar 2 9606539), A5-149 (grammar 1 9603811 and grammar 2 9701412, 5149), A7-174 (grammar 1 9703317);
#   A7-173 (9600475 and 9606064 by A7-168, 9602179 and 9607450 by A7-169: already fixed, pins only);
#   product rows, with the TEMPORARY switch SE_R23_OFF=<conv|strad> of the build the mutation was made on (the switch is removed from the final
#   tree, so these two lines need that build): A7-171 (grammar 1 9608430, conv), A10-18 (grammar 1 9606511, strad).
S=${SCRATCH:?set SCRATCH to a scratch directory}
B=$1
ulimit -v 6000000
count() { grep -o 'failing seeds=[0-9]*; findings=[0-9]*\|[0-9]* finding(s)' | tail -1; }
seed() { # <var=value> <grammar> <seed>
  local on off
  on=$(env SE_GENERATED_GRAMMAR=$2 SE_GENERATED_SEED=$3 SE_GENERATED_CANARY=0 SE_GENERATED_SHRINK=0 "$B" "$S/mutseed" 2>&1 | count)
  off=$(env $1 SE_GENERATED_GRAMMAR=$2 SE_GENERATED_SEED=$3 SE_GENERATED_CANARY=0 SE_GENERATED_SHRINK=0 "$B" "$S/mutseed" 2>&1 | count)
  echo "seed $3 (grammar $2): fix on: $on; $1: $off"
}
shrunk() { # <label> <var=value> <words> [env assignments...]
  local label=$1 sw=$2 words=$3
  shift 3
  local on off
  on=$(env "$@" SE_GENERATED_WORDS="$words" SE_GENERATED_CANARY=0 "$B" "$S/mutshr" 2>&1 | count)
  off=$(env "$@" $sw SE_GENERATED_WORDS="$words" SE_GENERATED_CANARY=0 "$B" "$S/mutshr" 2>&1 | count)
  echo "$label shrunk: fix on: $on; $sw: $off"
}
seed SE_R23_OFF=conv 1 9608430
seed SE_A7_RULE_OFF=172 2 9606539
seed SE_A7_RULE_OFF=5149 1 9603811
seed SE_A7_RULE_OFF=5149 2 9701412
seed SE_R23_OFF=strad 1 9606511
seed SE_A7_RULE_OFF=174 1 9703317
seed SE_A7_RULE_OFF=168 1 9600475
seed SE_A7_RULE_OFF=168 1 9606064
seed SE_A7_RULE_OFF=169 1 9602179
seed SE_A7_RULE_OFF=169 1 9607450
shrunk A7-171 SE_R23_OFF=conv "D609 6341 3238 2508 8901 3310 8F01 2630 2400 2418 8B05 3030 8D01 2620 6241 000B 0009 AFFA 0009 0009 2600 1008" SE_GENERATED_VOLATILE=26001000-2600100f
shrunk A5-149 SE_A7_RULE_OFF=5149 "D603 046F D603 C208 8563 2402 000B 0009 2600 1008 2600 1000" SE_GENERATED_ALIAS=4:26003C04 SE_GENERATED_VOLATILE=26001000-2600100f
shrunk A10-18 SE_R23_OFF=strad "D604 D705 8177 2602 E004 006E 465F 2612 000B 0009 2600 1008 0600 1000" SE_GENERATED_VOLATILE=26001000-2600100f
shrunk A7-174 SE_A7_RULE_OFF=174 "D606 615D D706 2610 6071 C801 8BFB 4215 8900 2611 2650 000B 0009 0009 2600 1004 2600 1004" SE_GENERATED_VOLATILE=26001000-2600100f SE_GENERATED_POLL=26001004
shrunk A7-173/168 SE_A7_RULE_OFF=168 "D604 221B E000 0626 E008 0425 2632 000B 0009 0009 0600 1010" SE_GENERATED_VOLATILE=26001010-2600101f
shrunk A7-173/169 SE_A7_RULE_OFF=169 "D606 D707 6071 C801 8BFC E01C 0526 471B 0329 2600 2650 000B 0009 0009 2600 1014 2600 1014" SE_GENERATED_VOLATILE=26001010-2600101f SE_GENERATED_POLL=26001014
# the modules (function tables and the module's returns, clobbers, reads) are evaluated by the pins; their mutation is the default run with the rule off:
for rule in 172 174 5149; do
  n=$(env SE_A7_RULE_OFF=$rule SE_GENERATED_SEEDS=1 SE_GENERATED_CANARY=0 "$B" "$S/mutdef" 2>&1 | grep -c 'CHECK failed')
  echo "default run (every pin), SE_A7_RULE_OFF=$rule: $n CHECK failure(s)"
done
for sw in conv strad; do
  n=$(env SE_R23_OFF=$sw SE_GENERATED_SEEDS=1 SE_GENERATED_CANARY=0 "$B" "$S/mutdef" 2>&1 | grep -c 'CHECK failed')
  echo "default run (every pin), SE_R23_OFF=$sw: $n CHECK failure(s)"
done
