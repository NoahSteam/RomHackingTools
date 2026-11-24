#!/usr/bin/env bash
# V3 benchmark orchestration (Docs/ProfilerFixPlan.md, Phase 5, V3). See PROCEDURE.md.
#
# Runs N repetitions of three configurations against a patched Mednafen, interleaved
# (a b c a b c ...), each from a fresh emulator process loaded from the same save slot:
#   a  no client attached during the window (two short probe connections at its edges)
#   b  se-bench-client attached, function attribution off
#   c  se-bench-client attached, function attribution on
# and leaves raw CSV + summary files in --out for analyze_v3.py. It measures nothing itself.
#
# Needs: the patched emulator, its BIOS and a game (yours), a save slot made on the scene you
# chose, and a built se-bench-client. Linux/macOS shell; Windows: see PROCEDURE.md.
set -u

EMU=""; GAME=""; SLOT=""; OUT=""; BENCH=""; ENDPOINT=""
REPS=5; WARMUP=10; DURATION=60; SETTLE=3; THROTTLE=off; CACHE=""; EXTRA=""; SKIP_PREFLIGHT=0
usage() {
cat <<'EOF'
run_v3.sh --emu PATH --game PATH --slot N --out DIR --bench PATH/se-bench-client [options]
  --reps N          repetitions of the a/b/c block (default 5)
  --warmup SEC      discarded time before each window (default 10)
  --duration SEC    measured window (default 60)
  --settle SEC      wait after the slot load before the warm-up starts (default 3)
  --throttle off|on off: frame limiter disabled so host speed is measurable (default);
                    on: real-time pacing, only CPU-per-frame carries information
  --cache MODE      value for Mednafen's SH-2 cache emulation setting, if you pin one
                    (the setting is named in CACHE_SETTING, default ss.cpucache_emumode)
  --endpoint E      live endpoint; default is the driver's default
  --extra "ARGS"    extra emulator arguments, appended after the pinned ones
  --skip-preflight  do not check the emulator's -help for the pinned setting names
Environment: CACHE_SETTING, SDL_VIDEODRIVER and similar are passed through to the emulator.
EOF
}
while [ $# -gt 0 ]; do
  case "$1" in
    --emu) EMU="$2"; shift 2;;
    --game) GAME="$2"; shift 2;;
    --slot) SLOT="$2"; shift 2;;
    --out) OUT="$2"; shift 2;;
    --bench) BENCH="$2"; shift 2;;
    --reps) REPS="$2"; shift 2;;
    --warmup) WARMUP="$2"; shift 2;;
    --duration) DURATION="$2"; shift 2;;
    --settle) SETTLE="$2"; shift 2;;
    --throttle) THROTTLE="$2"; shift 2;;
    --cache) CACHE="$2"; shift 2;;
    --endpoint) ENDPOINT="$2"; shift 2;;
    --extra) EXTRA="$2"; shift 2;;
    --skip-preflight) SKIP_PREFLIGHT=1; shift;;
    -h|--help) usage; exit 0;;
    *) echo "unknown option $1" >&2; usage >&2; exit 1;;
  esac
done
for v in EMU GAME SLOT OUT BENCH; do
  if [ -z "${!v}" ]; then echo "missing --$(echo "$v" | tr 'A-Z' 'a-z')" >&2; usage >&2; exit 1; fi
done
[ -x "$EMU" ] || { echo "emulator not executable: $EMU" >&2; exit 1; }
[ -x "$BENCH" ] || { echo "bench client not executable: $BENCH" >&2; exit 1; }
[ -f "$GAME" ] || { echo "game not found: $GAME" >&2; exit 1; }
case "$THROTTLE" in on|off) ;; *) echo "--throttle takes on or off" >&2; exit 1;; esac
CACHE_SETTING="${CACHE_SETTING:-ss.cpucache_emumode}"
mkdir -p "$OUT" || exit 1

# ---- pinned emulator settings -------------------------------------------------------------
# Everything that could change speed between runs is fixed here. Names are Mednafen's; they are
# checked against `$EMU -help` below because they vary between versions.
PINNED=(-sound 0 -video.fs 0 -video.glvsync 0 -video.frameskip 0 -fps.autoenable 1 -cheats 0 -autosave 0)
[ "$THROTTLE" = off ] && PINNED+=(-nothrottle 1)
[ -n "$CACHE" ] && PINNED+=("-$CACHE_SETTING" "$CACHE")
# shellcheck disable=SC2206
[ -n "$EXTRA" ] && PINNED+=($EXTRA)

if [ "$SKIP_PREFLIGHT" -eq 0 ]; then
  HELP="$("$EMU" -help 2>&1 || true)"
  missing=0
  for name in sound video.fs video.glvsync video.frameskip fps.autoenable cheats autosave $([ "$THROTTLE" = off ] && echo nothrottle) $([ -n "$CACHE" ] && echo "$CACHE_SETTING"); do
    if ! printf '%s' "$HELP" | grep -q -- "-$name"; then echo "preflight: this emulator does not list -$name" >&2; missing=1; fi
  done
  if [ "$missing" -ne 0 ]; then
    echo "preflight failed: fix the names in run_v3.sh or pass --skip-preflight after checking by hand" >&2
    exit 1
  fi
fi

BENCH_EP=()
[ -n "$ENDPOINT" ] && BENCH_EP=(--endpoint "$ENDPOINT")

# ---- environment record -----------------------------------------------------------------
{
  echo "date=$(date -u +%Y-%m-%dT%H:%M:%SZ)"
  echo "host=$(uname -a)"
  echo "emulator=$EMU"
  echo "emulator_sha256=$( (sha256sum "$EMU" 2>/dev/null || shasum -a 256 "$EMU" 2>/dev/null) | cut -d' ' -f1)"
  echo "game=$GAME"
  echo "game_sha256=$( (sha256sum "$GAME" 2>/dev/null || shasum -a 256 "$GAME" 2>/dev/null) | cut -d' ' -f1)"
  echo "slot=$SLOT"
  echo "throttle=$THROTTLE"
  echo "cache=${CACHE:-unpinned}"
  echo "pinned_args=${PINNED[*]}"
  echo "reps=$REPS"
  echo "warmup_s=$WARMUP"
  echo "duration_s=$DURATION"
  echo "settle_s=$SETTLE"
  echo "cpus=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null)"
  echo "cpu_model=$(grep -m1 'model name' /proc/cpuinfo 2>/dev/null | cut -d: -f2- | sed 's/^ //')"
  echo "governor=$(cat /sys/devices/system/cpu/cpu0/cpufreq/scaling_governor 2>/dev/null || echo unknown)"
  echo "loadavg_at_start=$(cut -d' ' -f1-3 /proc/loadavg 2>/dev/null || uptime)"
  echo "bench_client=$BENCH"
} > "$OUT/run_meta.txt"
gov="$(grep '^governor=' "$OUT/run_meta.txt" | cut -d= -f2)"
if [ "$gov" != "performance" ] && [ "$gov" != "unknown" ]; then
  echo "warning: CPU governor is '$gov'; frequency scaling adds drift. Consider: cpupower frequency-set -g performance" >&2
fi

PID=""
cleanup() { if [ -n "$PID" ] && kill -0 "$PID" 2>/dev/null; then kill "$PID" 2>/dev/null; wait "$PID" 2>/dev/null; fi; }
trap cleanup EXIT INT TERM

run_config() { # rep cfg
  local rep="$1" cfg="$2" tag name
  tag=$(printf 'run%02d_%s' "$rep" "$cfg"); name="$OUT/$tag"
  echo "== $tag"
  "$EMU" "${PINNED[@]}" -force_module ss "$GAME" > "$name.emulator.log" 2>&1 &
  PID=$!
  # One fresh process per run, each placed on the same scene by the emulator's own slot loader.
  if ! "$BENCH" --mode load-slot --slot "$SLOT" --connect-timeout 60 "${BENCH_EP[@]}" > "$name.load.log" 2>&1; then
    echo "  slot load failed, see $name.load.log" >&2
    cleanup; PID=""; return 1
  fi
  sleep "$SETTLE"
  local rc=0
  case "$cfg" in
    a) "$BENCH" --mode bracket --tag a --warmup "$WARMUP" --duration "$DURATION" --pid "$PID" \
         --csv "$name.csv" --summary "$name.summary" "${BENCH_EP[@]}" > "$name.client.log" 2>&1 || rc=$?;;
    b) "$BENCH" --mode stream --attribution off --tag b --warmup "$WARMUP" --duration "$DURATION" --pid "$PID" \
         --csv "$name.csv" --summary "$name.summary" "${BENCH_EP[@]}" > "$name.client.log" 2>&1 || rc=$?;;
    c) "$BENCH" --mode stream --attribution on --tag c --warmup "$WARMUP" --duration "$DURATION" --pid "$PID" \
         --csv "$name.csv" --summary "$name.summary" "${BENCH_EP[@]}" > "$name.client.log" 2>&1 || rc=$?;;
  esac
  case "$rc" in
    0) echo "  ok";;
    2) echo "  completed but INVALID (lost samples or similar): $(grep -E '^LOSS' "$name.client.log")";;
    *) echo "  client error (exit $rc), see $name.client.log" >&2;;
  esac
  cleanup; PID=""
  sleep 2   # let the OS and the endpoint settle before the next process
  return 0
}

rep=1
while [ "$rep" -le "$REPS" ]; do
  for cfg in a b c; do run_config "$rep" "$cfg"; done
  rep=$((rep + 1))
done
echo "done. Analyze with: python3 -I $(dirname "$0")/analyze_v3.py $OUT"
