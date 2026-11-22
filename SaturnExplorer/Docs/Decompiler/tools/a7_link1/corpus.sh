#!/bin/bash
# Link model step 0: one game corpus image, alone and lent, with the token dump (now with the link model's S, K, N, C and O records) and the census.
# usage: corpus.sh <test binary> <hwram image> <scratch dir> <tag> [lend]   (lend is passed on to a7_round2/corpus.sh: SE_GAME_LEND=1)
#   writes <scratch>/dump/<tag>_alone.tsv and <tag>_lent.tsv (tools/a7_round2/corpus.sh's, plus the shadow model), <scratch>/census_<tag>.txt and
#   <scratch>/diff_<tag>_{alone,lent}.txt (linkdiff.py diff). To show a change did not move any old answer, run the image with the build before
#   the change as well and `linkdiff.py same <old dump> <new dump>`.
# The game images are not in the repository (SE_GAME_HWRAM names one); this has not been run in the cloud container the step 1 work was done in.
BIN=$1; IMG=$2; D=$3; TAG=$4
HERE=$(dirname "$0")
mkdir -p "$D"
SE_LINK_CENSUS="$D/census_${TAG}.txt" "$HERE/../a7_round2/corpus.sh" "$BIN" "$IMG" "$D" "$TAG" "$5"
for k in alone lent; do
  [ -f "$D/dump/${TAG}_$k.tsv" ] && python3 "$HERE/linkdiff.py" diff "$D/dump/${TAG}_$k.tsv" --list 8 > "$D/diff_${TAG}_$k.txt"
done
