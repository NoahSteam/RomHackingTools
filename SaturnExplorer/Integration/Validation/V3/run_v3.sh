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
REPS=5; WARMUP=10; DURATION=60; FRAMES=1500; WARMUP_FRAMES=300; TIMEOUT=900; SETTLE=3; THROTTLE=off; CACHE=""; EXTRA=""; SKIP_PREFLIGHT=0
MEDHOME=""; SKIP_STATE_CHECK=0
usage() {
cat <<'EOF'
run_v3.sh --emu PATH --game PATH --slot N --out DIR --bench PATH/se-bench-client [options]
  --reps N          repetitions of the a/b/c block (default 5)
  --frames N        measured window in emulated frames (default 1500; 0 = use --duration). Every
                    configuration then measures the same amount of emulation at any speed, so a run
                    at 10 fps and one at 100 fps are both valid. --duration is NOT used with it
  --warmup-frames N discarded frames before the window (default 300; 0 = use --warmup)
  --timeout SEC     per client run, a window that cannot finish fails (default 900; a frame window of
                    1500 + 300 frames needs 1800 / fps seconds, so 900 covers down to about 2 fps)
  --duration SEC    measured window in seconds, only with --frames 0 (default 60)
  --warmup SEC      discarded seconds, only with --warmup-frames 0 (default 10)
  --settle SEC      wait after the slot load before the warm-up starts (default 3)
  --throttle off|on off: frame limiter disabled so host speed is measurable (default);
                    on: real-time pacing, only CPU-per-frame carries information
  --cache MODE      pin the SH-2 cache emulation: data_cb | data | full | auto (ss.dbg_cem);
                    make the save slot under the same mode
  --endpoint E      live endpoint; default is the driver's default
  --extra "ARGS"    extra emulator arguments, appended after the pinned ones
  --mednafen-home DIR  run the emulator with MEDNAFEN_HOME=DIR (its config, firmware and states live there).
                    The Saturn Explorer GUI does this with the emulator's own folder, so states made
                    through the GUI are in <emulator dir>/mcs
  --skip-preflight  do not check the pinned settings against the emulator's -dump_settings_def
  --skip-state-check  do not check that a slot state file exists in the emulator's states folder
Before anything is started the script checks that <home>/mcs/*.mc<slot> exists, <home> being --mednafen-home,
else $MEDNAFEN_HOME, else $HOME/.mednafen (on Windows too; with HOME unset there, the emulator's own folder).
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
    --timeout) TIMEOUT="$2"; shift 2;;
    --mednafen-home) MEDHOME="$2"; shift 2;;
    --skip-state-check) SKIP_STATE_CHECK=1; shift;;
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
case "$SLOT" in ''|*[!0-9]*) echo "--slot takes a slot number 0-9" >&2; exit 1;; esac

# ---- save-state preflight ------------------------------------------------------------------
# Mednafen reads and writes states in <base>/mcs/, <base> being $MEDNAFEN_HOME when set, else the platform
# default. The Saturn Explorer GUI launches Mednafen with MEDNAFEN_HOME = the emulator exe's folder
# (FrontEnd WindowsPlatform.cpp), so a state made through the GUI is in <exe dir>/mcs, where a shell run
# WITHOUT MEDNAFEN_HOME does not look. The same variable also moves mednafen.cfg and firmware/ (BIOS): with a
# different base the emulator reads another config and another BIOS folder (and creates a default mednafen.cfg
# there if none exists). The state file name carries the disc's MD5 (<game>.<md5>.mc<N>), which is not computed
# here, so the check is that at least one .mc<N> exists, and every candidate is listed. Nothing is copied or moved.
if [ -n "$MEDHOME" ]; then
  [ -d "$MEDHOME" ] || { echo "--mednafen-home: not a directory: $MEDHOME" >&2; exit 1; }
  export MEDNAFEN_HOME="$MEDHOME"
  HOME_SRC="--mednafen-home"
elif [ -n "${MEDNAFEN_HOME:-}" ]; then
  HOME_SRC="MEDNAFEN_HOME"
else
  HOME_SRC="platform default"
fi
# The same order as Mednafen's GetBaseDirectory() (src/drivers/main.cpp): a non-empty MEDNAFEN_HOME; else
# $HOME/.mednafen whenever HOME is set, on Windows too (it never reads USERPROFILE); else, on Windows, the
# emulator exe's own folder, and elsewhere the account's home directory.
is_windows_emu() { case "$(uname -s 2>/dev/null)" in MINGW*|MSYS*|CYGWIN*) return 0;; esac; case "$EMU" in *.exe|*.EXE) return 0;; esac; return 1; }
if [ -n "${MEDNAFEN_HOME:-}" ]; then
  BASE="$MEDNAFEN_HOME"
elif [ -n "${HOME+set}" ]; then
  BASE="$HOME/.mednafen"; HOME_SRC="HOME"
elif is_windows_emu; then
  BASE="$(cd "$(dirname "$EMU")" 2>/dev/null && pwd)"; HOME_SRC="the emulator's folder (HOME is not set)"
else
  BASE="$(eval echo "~$(id -un 2>/dev/null)")/.mednafen"; HOME_SRC="the account's home directory (HOME is not set)"
fi
list_states() { # dir -> one line per state file
  local f
  for f in "$1"/mcs/*.mc"$SLOT"; do if [ -f "$f" ]; then echo "$f"; fi; done
}
STATES="$(list_states "$BASE")"
if [ "$SKIP_STATE_CHECK" -eq 0 ]; then
  if [ -z "$STATES" ]; then
    echo "preflight: no slot $SLOT save state in $BASE/mcs (looked for *.mc$SLOT; base from $HOME_SRC)." >&2
    echo "  Mednafen reads states from MEDNAFEN_HOME when set, else HOME/.mednafen. States made through the" >&2
    echo "  Saturn Explorer GUI are in <mednafen exe dir>/mcs, because the GUI sets MEDNAFEN_HOME to that folder." >&2
    EMUDIR="$(cd "$(dirname "$EMU")" 2>/dev/null && pwd)"
    if [ -n "$EMUDIR" ] && [ -n "$(list_states "$EMUDIR")" ]; then
      echo "  A slot $SLOT state exists in $EMUDIR/mcs: try --mednafen-home \"$EMUDIR\"" >&2
    else
      echo "  Try --mednafen-home \"<mednafen exe dir>\" (here: \"${EMUDIR:-$(dirname "$EMU")}\"), or make the state under the folder named above." >&2
    fi
    echo "  (Nothing was copied or started. --skip-state-check disables this check.)" >&2
    exit 1
  fi
  echo "state check: base $BASE (from $HOME_SRC); slot $SLOT state candidate(s) in $BASE/mcs:"
  echo "$STATES" | sed 's/^/  /'
  echo "  Mednafen loads the one whose name carries this disc's MD5; the others are ignored."
  [ -f "$BASE/mednafen.cfg" ] || echo "  note: no $BASE/mednafen.cfg: Mednafen will create a default one there and look for the BIOS in $BASE/firmware" >&2
fi
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
  echo "timeout_s=$TIMEOUT"
  echo "mednafen_base=$BASE"
  echo "mednafen_base_from=$HOME_SRC"
  echo "slot_state_candidates=$(echo "$STATES" | tr '\n' ';')"
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

# The analyzer's floors (frames and seconds in a window) are read from it, not copied: a run under either is
# reported INVALID there, so say so now, while the operator can still change the window.
FLOORS="$(python3 -I "$(dirname "$0")/analyze_v3.py" --print-floors 2>/dev/null)"
floor_note() { # summary
  [ -n "$FLOORS" ] && [ -f "$1" ] || return 0
  local fl="${FLOORS%% *}" sl="${FLOORS##* }" wf ws we
  wf="$(sed -n 's/^window_frames=//p' "$1")"; ws="$(sed -n 's/^window_start_ns=//p' "$1")"; we="$(sed -n 's/^window_end_ns=//p' "$1")"
  if [ -n "$wf" ] && [ "$wf" -lt "$fl" ] 2>/dev/null; then
    echo "  note: only $wf frames in the window; the analyzer needs $fl. Raise --frames (now $FRAMES) so the SLOWEST configuration holds at least $fl." >&2
  fi
  if [ -n "$ws" ] && [ -n "$we" ] && awk -v a="$ws" -v b="$we" -v m="$sl" 'BEGIN{exit !((b-a)/1e9 < m)}'; then
    echo "  note: the window lasted under $sl s; the analyzer needs $sl s. Raise --frames (now $FRAMES) so the FASTEST configuration takes at least that long." >&2
  fi
  return 0
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
  # Frame windows take no --duration and frame warm-ups no --warmup (the client ignores them in both modes).
  local COUNT=()
  if [ "$FRAMES" -gt 0 ]; then COUNT+=(--frames "$FRAMES"); else COUNT+=(--duration "$DURATION"); fi
  if [ "$WARMUP_FRAMES" -gt 0 ]; then COUNT+=(--warmup-frames "$WARMUP_FRAMES"); else COUNT+=(--warmup "$WARMUP"); fi
  COUNT+=(--timeout "$TIMEOUT")
  case "$cfg" in
    a) "$BENCH" --mode bracket --tag a ${COUNT[@]+"${COUNT[@]}"} --pid "$PID" \
         --csv "$name.csv" --summary "$name.summary" ${BENCH_EP[@]+"${BENCH_EP[@]}"} > "$name.client.log" 2>&1 || rc=$?;;
    b) "$BENCH" --mode stream --attribution off --tag b ${COUNT[@]+"${COUNT[@]}"} --pid "$PID" \
         --csv "$name.csv" --summary "$name.summary" ${BENCH_EP[@]+"${BENCH_EP[@]}"} > "$name.client.log" 2>&1 || rc=$?;;
    c) "$BENCH" --mode stream --attribution on --tag c ${COUNT[@]+"${COUNT[@]}"} --pid "$PID" \
         --csv "$name.csv" --summary "$name.summary" ${BENCH_EP[@]+"${BENCH_EP[@]}"} > "$name.client.log" 2>&1 || rc=$?;;
  esac
  case "$rc" in
    0) echo "  ok";;
    2) echo "  completed but INVALID (lost samples or similar): $(grep -E '^LOSS' "$name.client.log")"
       grep -E '^invalid_reason=' "$name.summary" 2>/dev/null | sed 's/^/    /';;
    *) echo "  client error (exit $rc), see $name.client.log" >&2
       # whatever it wrote before failing is not a measurement
       rm -f "$name.csv" "$name.summary"
       cleanup; PID=""; abort_run "$tag: client error (exit $rc)";;
  esac
  cleanup; PID=""
  floor_note "$name.summary"
  sleep 2   # let the OS and the endpoint settle before the next process
  return 0
}

rep=1
while [ "$rep" -le "$REPS" ]; do
  for cfg in a b c; do run_config "$rep" "$cfg"; done
  rep=$((rep + 1))
done
echo "done. Analyze with: python3 -I $(dirname "$0")/analyze_v3.py $OUT"
