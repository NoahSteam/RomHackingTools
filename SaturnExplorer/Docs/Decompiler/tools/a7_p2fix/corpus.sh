#!/bin/bash
# Area 7 P2 fixes: the token dumps of both images (one run each with SE_GAME_LEND=1, which dumps the image alone and lent), with the two
# product changes on and (the mutation build's SE_P2_OFF189=1 SE_P2_OFF190=1 SE_P2_OFF190c=1, master's behaviour) off, one run at a time; then each pair
# compared. usage: corpus.sh <decompiler test binary> <image dir> <scratch dir> [on|off|both]
B="$1"; H="$2"; D="$3"; W="${4:-both}"
ulimit -v 6000000
mkdir -p "$D/dump"
for mode in on off; do
  [ "$W" != both ] && [ "$W" != "$mode" ] && continue
  for img in hwram007 hwram008; do
    tag="${img}_${mode}"
    rm -rf "$D/run_$tag"; mkdir -p "$D/run_$tag"
    env $( [ $mode = off ] && echo SE_P2_OFF189=1 SE_P2_OFF190=1 SE_P2_OFF190c=1 ) SE_GAME_LEND=1 SE_GAME_HWRAM="$H/$img.bin" SE_GAME_TOKEN_DUMP="$D/dump/$tag" \
        timeout 2400 "$B" "$D/run_$tag" > "$D/corpus_$tag.log" 2>&1
    echo "$tag: exit $? $(grep -c 'CHECK failed' "$D/corpus_$tag.log") check(s) failed" | tee -a "$D/corpus.out"
    rm -rf "$D/run_$tag"
  done
done
if [ "$W" = both ]; then
  for img in hwram007 hwram008; do
    for pass in alone lent; do
      a="$D/dump/${img}_off_$pass.tsv"; b="$D/dump/${img}_on_$pass.tsv"
      if cmp -s "$a" "$b"; then echo "$img $pass: identical ($(wc -l < "$b") lines)"
      else echo "$img $pass: differs ($(diff "$a" "$b" | grep -c '^[<>]') changed lines)"; diff "$a" "$b" | head -100; fi
    done
  done | tee -a "$D/corpus.out"
fi
