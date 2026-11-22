#!/bin/bash
# Link model step 2b: the game-corpus census of one image, alone and lent, as the table step 1 made (the rows the owner re-runs: the game
# images are not in the cloud container the step 2 work was done in).
# usage: corpus_census.sh <SaturnExplorerSh2DecompilerTests> <hwram image> <scratch dir> <tag>
#   1. the binary alone (no image): the fixtures' census, which every image's run also counts;
#   2. a7_link1/corpus.sh for the image alone and lent (a7_round2/corpus.sh underneath: SE_CORPUS_TIMEOUT seconds a piece, default 1150;
#      a large image, NiGHTS, needs 3000 or more), each writing its census (SE_LINK_CENSUS);
#   3. census_merge.py --subtract: <scratch>/table_<tag>_alone.txt and table_<tag>_lent.txt, the game's own figures.
# The rows to compare with step 1's (corpus_results/ of a7_link1, Sakura Taisen 2: store survival 97.1% / 97.3%, lines that name a store and
# are left with no proven store 5.0% / 4.5%): "Lines that name a store today left with no proven store" (go at 2% or less), its "...of those,
# the ones with no proven Effect at all", "Lines left with no proven Effect, by the printed op's class", the records and the stores found
# through them, and "Walk bound hits"; and the store lines' click targets against the Effect set (CLASS store.* in the census files).
BIN=$1; IMG=$2; D=$3; TAG=$4
HERE=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$D"
if [ ! -f "$D/census_base.txt" ]; then
  ( ulimit -v 4500000; unset SE_GAME_LEND; SE_LINK_CENSUS="$D/census_base.txt" "$BIN" "$D/runbase" > "$D/base.log" 2>&1 )
fi
# The base and alone runs clear SE_GAME_LEND (a caller's own setting would make them lent); the lent run asks for it through the `lend`
# argument, which a7_link1/corpus.sh passes on to a7_round2/corpus.sh. corpus_census_selftest.sh checks the three modes.
# corpus.sh names the report census_<tag>.txt, for the alone and the lent pass together; to have the two apart it is run twice
( unset SE_GAME_LEND; "$HERE/../a7_link1/corpus.sh" "$BIN" "$IMG" "$D/alone" "$TAG" )
cp "$D/alone/census_${TAG}.txt" "$D/census_${TAG}_alone.txt" 2>/dev/null
"$HERE/../a7_link1/corpus.sh" "$BIN" "$IMG" "$D/lent" "${TAG}_lend" lend
cp "$D/lent/census_${TAG}_lend.txt" "$D/census_${TAG}_lent.txt" 2>/dev/null
for k in alone lent; do
  [ -f "$D/census_${TAG}_$k.txt" ] && python3 "$HERE/../a7_link1/census_merge.py" --title "$TAG $k" --subtract "$D/census_base.txt" "$D/census_${TAG}_$k.txt" > "$D/table_${TAG}_$k.txt"
done
