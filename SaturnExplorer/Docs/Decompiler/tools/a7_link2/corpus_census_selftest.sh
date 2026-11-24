#!/bin/bash
# Self-test of corpus_census.sh's lending modes: a stub test binary logs SE_GAME_LEND for each run; the base and alone runs must be unlent and
# the lent run lent, whatever the caller's SE_GAME_LEND is. No image or engine is needed. Exits 0 when every check passes.
HERE=$(cd "$(dirname "$0")" && pwd)
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
cat > "$T/stub" <<'STUB'
#!/bin/bash
echo "${SE_GAME_HWRAM:-none} lend=${SE_GAME_LEND:-unset}" >> "$STUB_LOG"
[ -n "$SE_LINK_CENSUS" ] && : > "$SE_LINK_CENSUS"
exit 0
STUB
chmod +x "$T/stub"; : > "$T/img"
fail=0
for caller in unset 1; do
  : > "$T/log"; rm -rf "$T/out"
  if [ "$caller" = 1 ]; then SE_GAME_LEND=1 STUB_LOG="$T/log" bash "$HERE/corpus_census.sh" "$T/stub" "$T/img" "$T/out" t > /dev/null 2>&1
  else STUB_LOG="$T/log" bash "$HERE/corpus_census.sh" "$T/stub" "$T/img" "$T/out" t > /dev/null 2>&1; fi
  got=$(tr '\n' '|' < "$T/log")
  want="none lend=unset|$T/img lend=unset|$T/img lend=1|"
  if [ "$got" = "$want" ]; then echo "ok   caller SE_GAME_LEND=$caller: base unlent, alone unlent, lent lent"
  else echo "FAIL caller SE_GAME_LEND=$caller: got $got"; fail=1; fi
done
exit $fail
