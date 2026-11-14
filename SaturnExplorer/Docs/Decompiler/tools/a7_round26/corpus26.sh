#!/bin/bash
# Area 7 round 26: the token dumps of both images, alone and lent, with the round's product changes on and (the mutation build's
# SE_R26_OFF=all, master's behaviour) off, one run at a time. usage: corpus26.sh <decompiler test binary> <image dir> <scratch dir> [on|off|both]
B="$1"; H="$2"; D="$3"; W="${4:-both}"
ulimit -v 6000000
mkdir -p "$D/dump"
for mode in on off; do
  [ "$W" != both ] && [ "$W" != "$mode" ] && continue
  for img in hwram007 hwram008; do
    for lend in alone lent; do
      tag="${img}_${lend}_${mode}"
      rm -rf "$D/run_$tag"; mkdir -p "$D/run_$tag"
      env $( [ $mode = off ] && echo SE_R26_OFF=all ) $( [ $lend = lent ] && echo SE_GAME_LEND=1 ) SE_GAME_HWRAM="$H/$img.bin" SE_GAME_TOKEN_DUMP="$D/dump/$tag" \
          timeout 2400 "$B" "$D/run_$tag" > "$D/corpus_$tag.log" 2>&1
      echo "$tag: exit $? $(grep -c 'CHECK failed' "$D/corpus_$tag.log") check(s) failed" | tee -a "$D/corpus26.out"
      rm -rf "$D/run_$tag"
    done
  done
done
