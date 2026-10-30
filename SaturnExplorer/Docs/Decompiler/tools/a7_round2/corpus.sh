#!/bin/bash
# Area 7 round 2: one corpus image, alone and lent, with the token dump.
# usage: corpus.sh <test binary> <hwram image> <scratch dir> <tag> [lend]
#   writes <scratch>/dump/<tag>_alone.tsv and <tag>_lent.tsv (see token_semantics.py) and <scratch>/corpus<tag>.log
# Rules of the machine: ulimit -v 3500000, one heavy job at a time, at most 20 minutes a piece (hwram007 is 3 minutes, hwram008 9).
BIN=$1; IMG=$2; D=$3; TAG=$4
mkdir -p "$D/dump" "$D/run$TAG"
ulimit -v 3500000
if [ "$5" = lend ]; then export SE_GAME_LEND=1; fi
SE_GAME_HWRAM=$IMG SE_GAME_TOKEN_DUMP=$D/dump/$TAG timeout 1150 "$BIN" "$D/run$TAG" > "$D/corpus$TAG.log" 2>&1
echo "done $?" >> "$D/corpus$TAG.log"
