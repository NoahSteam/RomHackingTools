#!/usr/bin/env bash
# run_v3.sh end to end against a stand-in emulator (FakeEmulator.cpp: real exporter, no BIOS),
# then analyze_v3.py on what it wrote. Checks the interleaved order, the files, and that every
# run is valid. Count-driven: every window holds exactly the frames asked for (--frames), so the outcome
# does not depend on how many frames per second the host manages (FAKE_FRAME_MS=40 must pass as well). usage: test_run_v3.sh FAKE_EMULATOR BENCH_CLIENT SOCKET_PATH
set -u
FAKE="$1"; BENCH="$2"; SOCK="$3"
HERE="$(cd "$(dirname "$0")" && pwd)"
# macOS sets TMPDIR with a trailing slash; a "//" in OUT would not match the paths run_v3.sh prints after it
# resolves a folder (the emulator's own, for one), so the slash is dropped here.
TMPBASE="${TMPDIR:-/tmp}"
OUT="$(mktemp -d "${TMPBASE%/}/se_v3_run.XXXXXX")"
trap 'rm -rf "$OUT"' EXIT
# The stand-in's frame period. The live ring keeps 16 records and drops the oldest (that is what the loss rule
# counts), so a client starved for 16 frame periods loses samples on ANY host; a 4 ms stand-in would turn a CPU
# starved test client into a "lost samples" invalid run. 20 ms leaves the client 320 ms. The test's outcome is
# otherwise independent of the period (FAKE_FRAME_MS=40 is run as well).
FAKE_FRAME_MS="${FAKE_FRAME_MS:-20}"; export FAKE_FRAME_MS
# Publish only while a client is attached, like the real emulator: configuration (a)'s second connection is then
# handed a stale frame, which the client must skip.
FAKE_ONLY_ATTACHED="${FAKE_ONLY_ATTACHED:-1}"; export FAKE_ONLY_ATTACHED
fail() { echo "FAIL: $*" >&2; exit 1; }
# The emulator's states folder: run_v3.sh refuses to start when no slot state exists there. Most runs below get it
# from MEDNAFEN_HOME (the environment path); the state-check tests at the end use --mednafen-home and other folders.
HOMEOK="$OUT/home_ok"; mkdir -p "$HOMEOK/mcs"; : > "$HOMEOK/mcs/Game.0123456789abcdef.mc0"
MEDNAFEN_HOME="$HOMEOK"; export MEDNAFEN_HOME

"$HERE/run_v3.sh" --emu "$FAKE" --game "$HERE/run_v3.sh" --slot 0 --out "$OUT" --bench "$BENCH" \
    --endpoint "$SOCK" --reps 2 --frames 120 --warmup-frames 10 --warmup 0.2 --duration 0.5 --settle 0.5 --cache full \
    > "$OUT/run.log" 2>&1 \
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
grep -qx "mednafen_base=$HOMEOK" "$OUT/run_meta.txt" || fail "run_meta lacks the states base"
grep -qx 'mednafen_base_from=MEDNAFEN_HOME' "$OUT/run_meta.txt" || fail "run_meta lacks where the base came from"
grep -q 'Game.0123456789abcdef.mc0' "$OUT/run.log" || fail "the state candidate was not printed"
grep -q '^attribution_requested=1' "$OUT/run01_c.summary" || fail "c did not request attribution"
grep -q '^attribution_requested=0' "$OUT/run01_b.summary" || fail "b requested attribution"
grep -q '^mode=bracket' "$OUT/run01_a.summary" || fail "a is not a bracket run"
# TEST VALUES, not the benchmark's: the real defaults (1000 frames, 5 s) stay in analyze_v3.py. Windows here are
# 120 frames by construction, so 100 passes at any host speed; the time floor is off because it would not.
python3 -I "$HERE/analyze_v3.py" "$OUT" --min-frames 100 --min-window-s 0 --bootstrap 200 > "$OUT/analysis.txt" \
    || { cat "$OUT/analysis.txt"; fail "analysis failed or a configuration has no valid run"; }
cat "$OUT/analysis.txt"
n="$(grep -c '^  rep .* valid$' "$OUT/analysis.txt")"
[ "$n" -eq 6 ] || fail "expected 6 valid runs, got $n"
# a directory that already holds a run is refused and left exactly as it was
before="$(cd "$OUT" && cksum run*_*.csv run*_*.summary run_meta.txt pinned.cfg)"
"$HERE/run_v3.sh" --emu "$FAKE" --game "$HERE/run_v3.sh" --slot 0 --out "$OUT" --bench "$BENCH" \
    --endpoint "$SOCK" --reps 1 --warmup 0.5 --duration 1 --settle 0.5 > "$OUT/reuse.log" 2>&1 && fail "reused --out accepted"
grep -q 'refusing --out' "$OUT/reuse.log" || fail "no refusal message for a reused directory"
[ "$before" = "$(cd "$OUT" && cksum run*_*.csv run*_*.summary run_meta.txt pinned.cfg)" ] || fail "refused run changed the directory"
# a client that fails is fatal: non-zero exit, a marker, and nothing the analyzer could count
printf '#!/bin/sh\nsleep 30\n' > "$OUT/emu_stub.sh"; printf '#!/bin/sh\nexit 1\n' > "$OUT/bench_fail.sh"
printf '#!/bin/sh\ncase "$*" in *load-slot*) exit 0;; esac\nwhile [ $# -gt 0 ]; do [ "$1" = --csv ] && echo partial > "$2"; shift; done\nexit 1\n' > "$OUT/bench_half.sh"
chmod +x "$OUT/emu_stub.sh" "$OUT/bench_fail.sh" "$OUT/bench_half.sh"
"$HERE/run_v3.sh" --emu "$OUT/emu_stub.sh" --game "$HERE/run_v3.sh" --slot 0 --out "$OUT/f1" --bench "$OUT/bench_fail.sh" \
    --reps 1 --settle 0 --skip-preflight > "$OUT/f1.log" 2>&1 && fail "failed slot load exited 0"
[ -s "$OUT/f1/ABORTED" ] || fail "no ABORTED marker after a failed slot load"
"$HERE/run_v3.sh" --emu "$OUT/emu_stub.sh" --game "$HERE/run_v3.sh" --slot 0 --out "$OUT/f2" --bench "$OUT/bench_half.sh" \
    --reps 1 --settle 0 --skip-preflight > "$OUT/f2.log" 2>&1 && fail "client error exited 0"
[ -s "$OUT/f2/ABORTED" ] || fail "no ABORTED marker after a client error"
ls "$OUT/f2"/run*.csv "$OUT/f2"/run*.summary > /dev/null 2>&1 && fail "a failed client left a CSV or summary behind"
grep -q 'done. Analyze' "$OUT/f1.log" "$OUT/f2.log" && fail "an aborted run printed done"

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
# ---- save-state preflight: a missing state fails early and names the folder; --mednafen-home is honoured ----
printf '#!/bin/sh\necho "$MEDNAFEN_HOME" > "%s/seen_home"\nsleep 30\n' "$OUT" > "$OUT/emu_home.sh"; chmod +x "$OUT/emu_home.sh"
EMPTY="$OUT/home_empty"; mkdir -p "$EMPTY/mcs"; : > "$EMPTY/mcs/Game.0123456789abcdef.mc3"   # a different slot only
rm -f "$OUT/seen_home"
"$HERE/run_v3.sh" --emu "$OUT/emu_home.sh" --game "$HERE/run_v3.sh" --slot 0 --out "$OUT/m1" --bench "$OUT/bench_fail.sh" \
    --mednafen-home "$EMPTY" --reps 1 --settle 0 --skip-preflight > "$OUT/m1.log" 2>&1 && fail "a missing slot state was accepted"
grep -q "no slot 0 save state in $EMPTY/mcs" "$OUT/m1.log" || { cat "$OUT/m1.log"; fail "the missing state's folder was not named"; }
grep -q 'mednafen exe dir' "$OUT/m1.log" || fail "no hint about the GUI's states folder"
grep -q -- '--mednafen-home' "$OUT/m1.log" || fail "no --mednafen-home suggestion"
[ ! -e "$OUT/m1" ] && [ ! -e "$OUT/seen_home" ] || fail "something was created or started before the state check failed"
# Mednafen's own order when MEDNAFEN_HOME is unset: HOME/.mednafen, never USERPROFILE. A state under HOME is
# found although USERPROFILE points somewhere empty (the failing client then ends the run, after the check).
mkdir -p "$OUT/shellhome/.mednafen/mcs" "$OUT/userprofile/.mednafen/mcs"; : > "$OUT/shellhome/.mednafen/mcs/Game.0123.mc0"
env -u MEDNAFEN_HOME HOME="$OUT/shellhome" USERPROFILE="$OUT/userprofile" "$HERE/run_v3.sh" --emu "$OUT/emu_home.sh" --game "$HERE/run_v3.sh" --slot 0 \
    --out "$OUT/m6" --bench "$OUT/bench_fail.sh" --reps 1 --settle 0 --skip-preflight > "$OUT/m6.log" 2>&1
grep -q "state check: base $OUT/shellhome/.mednafen (from HOME)" "$OUT/m6.log" || { cat "$OUT/m6.log"; fail "HOME/.mednafen was not the base when USERPROFILE differs"; }
grep -q 'no slot 0 save state' "$OUT/m6.log" && fail "a valid state under HOME was rejected"
rm -f "$OUT/seen_home"
# and the reverse: a state only under USERPROFILE is NOT where Mednafen looks
env -u MEDNAFEN_HOME HOME="$OUT/userprofile/nohome" USERPROFILE="$OUT/shellhome" "$HERE/run_v3.sh" --emu "$OUT/emu_home.sh" --game "$HERE/run_v3.sh" --slot 0 \
    --out "$OUT/m7" --bench "$OUT/bench_fail.sh" --reps 1 --settle 0 --skip-preflight > "$OUT/m7.log" 2>&1 && fail "a state under USERPROFILE was accepted"
grep -q "no slot 0 save state in $OUT/userprofile/nohome/.mednafen/mcs" "$OUT/m7.log" || { cat "$OUT/m7.log"; fail "USERPROFILE was consulted"; }
# with HOME unset and a Windows emulator (.exe), the base is the emulator's own folder
WEXE="$OUT/winexe"; mkdir -p "$WEXE/mcs"; : > "$WEXE/mcs/Game.0123.mc0"; cp "$OUT/emu_home.sh" "$WEXE/mednafen.exe"
env -u MEDNAFEN_HOME -u HOME USERPROFILE="$OUT/userprofile" "$HERE/run_v3.sh" --emu "$WEXE/mednafen.exe" --game "$HERE/run_v3.sh" --slot 0 \
    --out "$OUT/m8" --bench "$OUT/bench_fail.sh" --reps 1 --settle 0 --skip-preflight > "$OUT/m8.log" 2>&1
grep -q "state check: base $WEXE (from the emulator's folder" "$OUT/m8.log" || { cat "$OUT/m8.log"; fail "exe folder not used with HOME unset"; }
rm -f "$OUT/seen_home"
# nothing set but HOME: its .mednafen is named when it holds no state
mkdir -p "$OUT/fakehome"
env -u MEDNAFEN_HOME -u USERPROFILE HOME="$OUT/fakehome" "$HERE/run_v3.sh" --emu "$OUT/emu_home.sh" --game "$HERE/run_v3.sh" --slot 0 \
    --out "$OUT/m2" --bench "$OUT/bench_fail.sh" --reps 1 --settle 0 --skip-preflight > "$OUT/m2.log" 2>&1 && fail "default folder without a state accepted"
grep -q "no slot 0 save state in $OUT/fakehome/.mednafen/mcs" "$OUT/m2.log" || { cat "$OUT/m2.log"; fail "the default folder was not named"; }
# a state next to the emulator is pointed out, and not used implicitly
EXE="$OUT/exedir"; mkdir -p "$EXE/mcs"; : > "$EXE/mcs/Game.0123456789abcdef.mc0"; cp "$OUT/emu_home.sh" "$EXE/mednafen.sh"
env -u MEDNAFEN_HOME -u USERPROFILE HOME="$OUT/fakehome" "$HERE/run_v3.sh" --emu "$EXE/mednafen.sh" --game "$HERE/run_v3.sh" --slot 0 \
    --out "$OUT/m3" --bench "$OUT/bench_fail.sh" --reps 1 --settle 0 --skip-preflight > "$OUT/m3.log" 2>&1 && fail "implicit use of the exe folder"
grep -q "A slot 0 state exists in $EXE/mcs" "$OUT/m3.log" || { cat "$OUT/m3.log"; fail "the exe folder's state was not pointed out"; }
[ ! -e "$OUT/seen_home" ] || fail "the emulator was started although no state was found"
# --mednafen-home wins over the environment and is exported to the emulator it launches (the failing client ends the run)
# (the client waits for the emulator stub to have written what it saw: the stub is a shell that may not have run yet)
printf '#!/bin/sh\ni=0\nwhile [ ! -s "%s/seen_home" ] && [ $i -lt 400 ]; do sleep 0.05; i=$((i+1)); done\nexit 1\n' "$OUT" > "$OUT/bench_wait.sh"; chmod +x "$OUT/bench_wait.sh"
MEDNAFEN_HOME="$EMPTY" "$HERE/run_v3.sh" --emu "$OUT/emu_home.sh" --game "$HERE/run_v3.sh" --slot 0 --out "$OUT/m4" --bench "$OUT/bench_wait.sh" \
    --mednafen-home "$EXE" --reps 1 --settle 0 --skip-preflight > "$OUT/m4.log" 2>&1 && fail "the failing client was not fatal"
[ "$(cat "$OUT/seen_home" 2>/dev/null)" = "$EXE" ] || { cat "$OUT/m4.log"; fail "the emulator did not get MEDNAFEN_HOME=--mednafen-home"; }
grep -qx "mednafen_base=$EXE" "$OUT/m4/run_meta.txt" || fail "run_meta lacks the --mednafen-home base"
grep -qx 'mednafen_base_from=--mednafen-home' "$OUT/m4/run_meta.txt" || fail "run_meta lacks the source"
"$HERE/run_v3.sh" --emu "$OUT/emu_home.sh" --game "$HERE/run_v3.sh" --slot 0 --out "$OUT/m5" --bench "$OUT/bench_fail.sh" \
    --mednafen-home "$OUT/no_such_dir" --reps 1 > "$OUT/m5.log" 2>&1 && fail "a nonexistent --mednafen-home was accepted"
grep -q 'not a directory' "$OUT/m5.log" || fail "no message for a nonexistent --mednafen-home"
# the documented defaults reach the client: frame windows, no --duration / --warmup, a timeout
printf '#!/bin/sh\ncase "$*" in *load-slot*) exit 0;; esac\necho "$*" > "%s/bench_args"\nexit 1\n' "$OUT" > "$OUT/bench_args.sh"; chmod +x "$OUT/bench_args.sh"
"$HERE/run_v3.sh" --emu "$OUT/emu_stub.sh" --game "$HERE/run_v3.sh" --slot 0 --out "$OUT/d1" --bench "$OUT/bench_args.sh" \
    --reps 1 --settle 0 --skip-preflight > "$OUT/d1.log" 2>&1 && fail "stub client succeeded"
grep -q -- '--frames 1500 --warmup-frames 300 --timeout 900' "$OUT/bench_args" || { cat "$OUT/bench_args"; fail "default frame window not passed"; }
if grep -q -e '--duration' -e '--warmup ' "$OUT/bench_args"; then fail "a frame window was given --duration or --warmup"; fi
# the analyzer's floors are read from it by run_v3.sh, not copied
[ "$(python3 -I "$HERE/analyze_v3.py" --print-floors)" = "1000 5" ] || fail "analyzer floors changed or unreadable"
echo ok
