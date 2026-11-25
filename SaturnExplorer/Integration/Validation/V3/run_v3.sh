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
REPS=5; WARMUP=10; DURATION=60; FRAMES=0; WARMUP_FRAMES=0; SETTLE=3; THROTTLE=off; CACHE=""; EXTRA=""; SKIP_PREFLIGHT=0
usage() {
cat <<'EOF'
run_v3.sh --emu PATH --game PATH --slot N --out DIR --bench PATH/se-bench-client [options]
  --reps N          repetitions of the a/b/c block (default 5)
  --warmup SEC      discarded time before each window (default 10)
  --duration SEC    measured window (default 60)
  --frames N        end every window after N emulated frames instead of --duration seconds (a fairer
                    window than a fixed time: it holds the same amount of emulation at any speed). With
                    it, --duration is only the minimum gap between config a's two probes
  --warmup-frames N end the warm-up after N frames instead of --warmup seconds
  --settle SEC      wait after the slot load before the warm-up starts (default 3)
  --throttle off|on off: frame limiter disabled so host speed is measurable (default);
                    on: real-time pacing, only CPU-per-frame carries information
  --cache MODE      pin the SH-2 cache emulation: data_cb | data | full | auto (ss.dbg_cem);
                    make the save slot under the same mode
  --endpoint E      live endpoint; default is the driver's default
  --extra "ARGS"    extra emulator arguments, appended after the pinned ones
  --skip-preflight  do not check the pinned settings against the emulator's -dump_settings_def
Environment: SDL_VIDEODRIVER, MEDNAFEN_HOME and similar are passed through to the emulator.
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
    --frames) FRAMES="$2"; shift 2;;
    --warmup-frames) WARMUP_FRAMES="$2"; shift 2;;
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
mkdir -p "$OUT" || exit 1
# A directory that already holds a run is refused: a failed attempt would otherwise leave the older
# CSVs in place under this run's metadata, and the analyzer would report them as this build's result.
for old in "$OUT"/run[0-9]*_[abc].* "$OUT/run_meta.txt" "$OUT/pinned.cfg"; do
  if [ -e "$old" ]; then
    echo "refusing --out $OUT: it already holds run files ($(basename "$old")). Use a new directory per run." >&2
    exit 1
  fi
done

# ---- pinned emulator settings -------------------------------------------------------------
# Everything that can change the emulator's speed between runs is fixed in an OVERRIDE settings file
# (-ovconfig): Mednafen keeps override values apart from the user's own and never writes them back to
# mednafen.cfg, whereas a setting given as `-name value` on the command line IS saved there for good
# (drivers/main.cpp:981 "Settings specified in this manner are automatically saved to the configuration
# file"). Names, types and legal values below were read from the pinned source (_emu/mednafen/src):
#   sound              BOOL  drivers/main.cpp:186      0: no audio device. Without it MDFND_Update takes the
#                                                       timer branch (main.cpp:2549) instead of the sound one
#   nothrottle         BOOL  drivers/main.cpp:181,2549 1: skip ers.Sync(), i.e. the frame limiter is off.
#                                                       Only honoured with sound disabled (see above).
#   video.frameskip    BOOL  drivers/main.cpp:167,1306 0: never skip rendering to catch up. Frameskip would
#                                                       drop VDP work and change what is being timed.
#   video.blit_timesync BOOL drivers/main.cpp:170,2682 0: blit before the sound sync, not after; pinned only to
#                                                       remove a wait. PassBlit does not block the emulation
#                                                       thread for long (main.cpp:2604-2616).
#   video.glvsync      BOOL  drivers/video.cpp:197,965 0: no vsync wait on the OpenGL page flip (the blit
#                                                       thread's, not the emulation thread's).
#   video.fs           BOOL  drivers/video.cpp:185     0: windowed.
#   fps.autoenable     BOOL  drivers/main.cpp:199      1: Mednafen's own fps readout, the one clock that is
#                                                       independent of the live protocol.
#   cheats             BOOL  mempatcher.cpp:998        0: no cheat patches.
#   autosave           BOOL  drivers/main.cpp:182      0: do not load or save a state on open / close.
#   ss.dbg_cem         ENUM  ss/ss.cpp:2572,1326       data_cb | data | full | auto: SH-2 cache emulation
#                                                       override (NONPERSISTENT, SUPPRESS_DOC); auto = the
#                                                       module's own choice. Needs no -help entry to work.
# Not pinned but worth recording: video.driver (ENUM default|opengl|softfb, video.cpp:183). `--extra "ARGS"`
# goes on the command line, which Mednafen saves into the user's mednafen.cfg.
PIN="$OUT/pinned.cfg"
{
  echo "sound 0"
  if [ "$THROTTLE" = off ]; then echo "nothrottle 1"; else echo "nothrottle 0"; fi
  echo "video.frameskip 0"
  echo "video.blit_timesync 0"
  echo "video.glvsync 0"
  echo "video.fs 0"
  echo "fps.autoenable 1"
  echo "cheats 0"
  echo "autosave 0"
  if [ -n "$CACHE" ]; then echo "ss.dbg_cem $CACHE"; fi
} > "$PIN"
PINNED=(-ovconfig "$PIN")
# shellcheck disable=SC2206
[ -n "$EXTRA" ] && PINNED+=($EXTRA)

# Preflight: an override file silently ignores unknown names, so check every pinned name, its type and its
# value against the emulator's own settings dump before anything is run. (`-help` lists no settings.)
if [ "$SKIP_PREFLIGHT" -eq 0 ]; then
  "$EMU" -dump_settings_def "$OUT/settings_def.txt" > "$OUT/preflight.log" 2>&1 || true
  if [ ! -s "$OUT/settings_def.txt" ]; then
    echo "preflight: $EMU wrote no settings dump (-dump_settings_def); see $OUT/preflight.log" >&2
    echo "pass --skip-preflight only after checking $PIN against your emulator by hand" >&2
    exit 1
  fi
  python3 -I "$(dirname "$0")/check_settings.py" "$OUT/settings_def.txt" "$PIN" || exit 1
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
  echo "pinned_settings=$(tr '\n' ';' < "$PIN")"
  echo "reps=$REPS"
  echo "warmup_s=$WARMUP"
  echo "duration_s=$DURATION"
  echo "frames=$FRAMES"
  echo "warmup_frames=$WARMUP_FRAMES"
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

# A fatal failure stops the whole run and marks the directory, so an incomplete set can neither be
# mistaken for a finished one nor be topped up by a second invocation (see the refusal above).
abort_run() {
  echo "ABORTED: $1" | tee "$OUT/ABORTED" >&2
  exit 1
}

run_config() { # rep cfg
  local rep="$1" cfg="$2" tag name
  tag=$(printf 'run%02d_%s' "$rep" "$cfg"); name="$OUT/$tag"
  echo "== $tag"
  "$EMU" "${PINNED[@]}" -force_module ss "$GAME" > "$name.emulator.log" 2>&1 &
  PID=$!
  # One fresh process per run, each placed on the same scene by the emulator's own slot loader.
  if ! "$BENCH" --mode load-slot --slot "$SLOT" --connect-timeout 60 ${BENCH_EP[@]+"${BENCH_EP[@]}"} > "$name.load.log" 2>&1; then
    echo "  slot load failed, see $name.load.log" >&2
    cleanup; PID=""; abort_run "$tag: slot load failed"
  fi
  sleep "$SETTLE"
  local rc=0
  local COUNT=()
  [ "$FRAMES" -gt 0 ] && COUNT+=(--frames "$FRAMES")
  [ "$WARMUP_FRAMES" -gt 0 ] && COUNT+=(--warmup-frames "$WARMUP_FRAMES")
  case "$cfg" in
    a) "$BENCH" --mode bracket --tag a --warmup "$WARMUP" --duration "$DURATION" ${COUNT[@]+"${COUNT[@]}"} --pid "$PID" \
         --csv "$name.csv" --summary "$name.summary" ${BENCH_EP[@]+"${BENCH_EP[@]}"} > "$name.client.log" 2>&1 || rc=$?;;
    b) "$BENCH" --mode stream --attribution off --tag b --warmup "$WARMUP" --duration "$DURATION" ${COUNT[@]+"${COUNT[@]}"} --pid "$PID" \
         --csv "$name.csv" --summary "$name.summary" ${BENCH_EP[@]+"${BENCH_EP[@]}"} > "$name.client.log" 2>&1 || rc=$?;;
    c) "$BENCH" --mode stream --attribution on --tag c --warmup "$WARMUP" --duration "$DURATION" ${COUNT[@]+"${COUNT[@]}"} --pid "$PID" \
         --csv "$name.csv" --summary "$name.summary" ${BENCH_EP[@]+"${BENCH_EP[@]}"} > "$name.client.log" 2>&1 || rc=$?;;
  esac
  case "$rc" in
    0) echo "  ok";;
    2) echo "  completed but INVALID (lost samples or similar): $(grep -E '^LOSS' "$name.client.log")";;
    *) echo "  client error (exit $rc), see $name.client.log" >&2
       # whatever it wrote before failing is not a measurement
       rm -f "$name.csv" "$name.summary"
       cleanup; PID=""; abort_run "$tag: client error (exit $rc)";;
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
