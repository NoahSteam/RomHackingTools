#!/bin/bash
# Area 3, outOrigins 7004216: replay the routed seed (grammar 1) and its shrunk words, and the A3-190 caller module.
# usage: replay.sh <generated-tests binary> <scratch dir>   (the binary deletes the scratch dir)
set -u
BIN=${1:?binary}; OUT=${2:?scratch dir}
SE_GENERATED_GRAMMAR=1 SE_GENERATED_SEED=7004216 SE_GENERATED_CANARY=0 "$BIN" "$OUT" | grep -E 'claim-wrong|failing seeds|all passed|failure'
SE_GENERATED_WORDS="D108 251B 605C 7170 E301 1738 4310 8BFC 621D 4215 8D03 4124 D703 000B 0009 000B 0009 0009 4000 0000 0600 1018" \
    "$BIN" "$OUT" | grep -E 'claim-wrong|finding\(s\)'
SE_GENERATED_WORDS="2742 2448 8B02 E700 000B 0009 000B 0009" "$BIN" "$OUT" | grep -E 'claim-wrong|finding\(s\)'
SE_GENERATED_WORDS="4F22 7FF8 E105 2F12 67F3 B007 0009 B00C 0009 60F2 7F08 4F26 000B 0009 2448 8B02 E700 000B 0009 000B 0009 E207 323C 2722 000B 0009" \
SE_GENERATED_FUNCS="0/0/1c,1c/1c/2a,2a/2a/34" SE_GENERATED_RETURNS="1,0,0" SE_GENERATED_CANARY=0 "$BIN" "$OUT" | grep -E 'return|finding\(s\)'
