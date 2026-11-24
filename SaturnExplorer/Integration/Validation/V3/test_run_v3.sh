#!/usr/bin/env bash
# run_v3.sh end to end against a stand-in emulator (FakeEmulator.cpp: real exporter, no BIOS),
# then analyze_v3.py on what it wrote. Checks the interleaved order, the files, and that every
# run is valid. usage: test_run_v3.sh FAKE_EMULATOR BENCH_CLIENT SOCKET_PATH
set -u
FAKE="$1"; BENCH="$2"; SOCK="$3"
HERE="$(cd "$(dirname "$0")" && pwd)"
OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT
fail() { echo "FAIL: $*" >&2; exit 1; }

"$HERE/run_v3.sh" --emu "$FAKE" --game "$HERE/run_v3.sh" --slot 0 --out "$OUT" --bench "$BENCH" \
    --endpoint "$SOCK" --reps 2 --warmup 0.5 --duration 2 --settle 0.5 --cache full > "$OUT/run.log" 2>&1 \
    || { cat "$OUT/run.log"; fail "run_v3.sh failed"; }
cat "$OUT/run.log"

order="$(grep '^== ' "$OUT/run.log" | tr -d '\n')"
[ "$order" = "== run01_a== run01_b== run01_c== run02_a== run02_b== run02_c" ] || fail "not interleaved: $order"
for r in 01 02; do for c in a b c; do
  [ -s "$OUT/run${r}_${c}.csv" ] && [ -s "$OUT/run${r}_${c}.summary" ] || fail "missing output for run${r}_${c}"
done; done
grep -q '^throttle=off' "$OUT/run_meta.txt" || fail "meta lacks throttle"
grep -qx 'nothrottle 1' "$OUT/pinned.cfg" || fail "pinned.cfg lacks nothrottle 1"
grep -qx 'ss.dbg_cem full' "$OUT/pinned.cfg" || fail "pinned.cfg lacks the cache mode"
grep -q -- '-ovconfig' "$OUT/run_meta.txt" || fail "emulator not given -ovconfig"
grep -q '^attribution_requested=1' "$OUT/run01_c.summary" || fail "c did not request attribution"
grep -q '^attribution_requested=0' "$OUT/run01_b.summary" || fail "b requested attribution"
grep -q '^mode=bracket' "$OUT/run01_a.summary" || fail "a is not a bracket run"
python3 -I "$HERE/analyze_v3.py" "$OUT" --min-frames 200 --min-window-s 1 --bootstrap 200 > "$OUT/analysis.txt" \
    || { cat "$OUT/analysis.txt"; fail "analysis failed or a configuration has no valid run"; }
cat "$OUT/analysis.txt"
n="$(grep -c '^  rep .* valid$' "$OUT/analysis.txt")"
[ "$n" -eq 6 ] || fail "expected 6 valid runs, got $n"
# a server without function attribution: the client must refuse to run config c rather than record a lie
"$FAKE" --no-functions > /dev/null 2>&1 &
NOFN=$!
"$BENCH" --endpoint "$SOCK" --attribution on --warmup 0.2 --duration 1 --connect-timeout 20 > "$OUT/nofn.log" 2>&1
rc=$?
kill $NOFN 2>/dev/null; wait $NOFN 2>/dev/null
[ "$rc" -eq 1 ] || { cat "$OUT/nofn.log"; fail "attribution request against a server without the capability exited $rc, expected 1"; }
grep -q 'lacks SE_LIVE_CAP_PROF_FUNCTIONS' "$OUT/nofn.log" || fail "no capability message"
# preflight: a setting the emulator does not have, and a value it does not accept, must stop the script
# before any run is started (an override file would otherwise ignore the unknown name silently)
FAKE_DUMP_OMIT=ss.dbg_cem "$HERE/run_v3.sh" --emu "$FAKE" --game "$HERE/run_v3.sh" --slot 0 --out "$OUT/p1" --bench "$BENCH" \
    --endpoint "$SOCK" --reps 1 --cache full > "$OUT/p1.log" 2>&1 && fail "missing setting accepted"
grep -q "has no setting 'ss.dbg_cem'" "$OUT/p1.log" || fail "preflight did not name the missing setting"
ls "$OUT/p1"/run01_a.csv > /dev/null 2>&1 && fail "a run started despite the failed preflight"
"$HERE/run_v3.sh" --emu "$FAKE" --game "$HERE/run_v3.sh" --slot 0 --out "$OUT/p2" --bench "$BENCH" \
    --endpoint "$SOCK" --reps 1 --cache bogus > "$OUT/p2.log" 2>&1 && fail "illegal enum value accepted"
grep -q "accepts data_cb|data|full|auto" "$OUT/p2.log" || fail "preflight did not list the legal cache values"
# an emulator that does not list a pinned setting must stop the script before any run
"$HERE/run_v3.sh" --emu "$FAKE" --game "$HERE/run_v3.sh" --slot 0 --out "$OUT/x" --bench "$BENCH" \
    --endpoint "$SOCK" --reps 1 --cache 1 --throttle bogus > /dev/null 2>&1 && fail "bad --throttle accepted"
echo ok
