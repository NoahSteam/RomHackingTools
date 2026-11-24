# V3: profiler cost on the emulator, recording procedure

This is the harness for `Docs/ProfilerFixPlan.md` Phase 5, item V3, and the numbers it produces
are the input to the plan's "Conditional on measurement" items F12 and F13. **Nothing in this
directory has been run against a real emulator.** It was built and tested against the real
exporter (`se_export.c`) and driver with a stand-in emulator thread, which proves the harness
records, flags and computes correctly; it says nothing about how fast or slow the patched
Mednafen is. The overhead figures exist only after you run the procedure below.

## What is measured

| | Configuration | Client during the measured window |
|---|---|---|
| a | no client | none: two short probe connections at the window edges only |
| b | client, attribution off | `se-bench-client --mode stream --attribution off` |
| c | client, attribution on | `se-bench-client --mode stream --attribution on` |

Held constant, per the plan: same save slot (loaded by the emulator's own slot loader), no input
(the client sends none; there is no host input), rewind off (`REW 0` is sent by the client in
every configuration, including the probes), audio off, no video output beyond the emulator's
window, limiter state pinned, cache mode pinned if you choose to.

## Speed measurement: what was chosen and why it is comparable

Two metrics, both computed by `analyze_v3.py` with **one estimator for all three configurations**.

1. **Speed (emulated frames per host second).** The emulator stamps every frame with its own
   frame number (`frame_no`, committed once per emulated frame whether or not a client is attached:
   `SeExportEndFrame`; the first reply a new connection receives carries the newest published frame).
   A run defines a window `[ws, we]`. Speed is `(f(we) - f(ws)) / (we - ws)` where `f(t)` is the
   frame number of the newest frame observed at or before `t`.
   * b, c: the client stays connected, drains every 16 ms like the UI, and `ws`, `we` are drain
     instants (`warmup` seconds after attach, and `duration` seconds later).
   * a: the client is **not connected during the window**. `se-bench-client --mode bracket` sleeps
     `warmup`, connects, reads the newest frame number and the time, disconnects; sleeps `duration`;
     does it again. `ws`, `we` are those two sample instants.
   * Same formula, same frame counter, same host clock, same window length and the same time since
     the scene was loaded. Quantisation: a stream edge is accurate to the drain period (16 ms), a probe
     edge to its poll (about 1 ms plus connection latency, which is a few ms); against a 60 s window
     that is below 0.05 percent.
   * Only meaningful when the emulator is **not throttled**. With the limiter on, speed is pinned at
     real time and carries no overhead information; use metric 2.

2. **Emulator CPU milliseconds per emulated frame.** `/proc/<pid>/stat` utime+stime of the emulator
   process, sampled by `se-bench-client --pid` at exactly the window edges, divided by the frames
   between the two samples. Works throttled or not, so it also answers when the host is fast enough
   that unthrottled speed would exceed what the stream can carry (below). **Linux only**; elsewhere
   the columns are empty and the analyzer reports speed only. It counts every thread of the emulator,
   which is what an overhead on the emulator should be judged by, and none of the client's.

Overhead is reported as the median ratio of extra host time per emulated frame, `median(t_x) / median(t_a) - 1`
(`t = 1/speed` or `t = cpu ms/frame`), with a 95 percent percentile-bootstrap interval from resampling
the runs of each configuration (4000 draws, seed 1).

### The honest limitation of configuration (a)

A connection of any kind makes `SeExportHasClient()` true. From then on `SeExportSnapshot` copies
the frame (VDP1/VDP2 VRAM, CRAM, registers, work RAM windows) into the ring on every emulated frame,
and, unless the client has sent `REW 0`, takes a full savestate per frame. There is no protocol request
that costs less: every verb replies with a full snapshot, and the driver polls at about 120 Hz. So
"a client that wants nothing" does not exist, which is why the plan's "no client" cannot be a probe
that stays connected. The bracket makes (a) client-free for the whole window at the price of two short
connections inside it (a few hundred milliseconds of client-attached emulation per 60 s, reported as
`probe_hold_*_ms` in the summary; keep it under about 1 percent of the window).

What this cannot give you: an independent confirmation that (a) was unperturbed by the client at all.
The only fully independent clock is the emulator's own speed readout. The script enables it
(`-fps.autoenable 1`): read the on-screen figure during a config (a) window by eye, and record it in the
results below as a cross-check. It cannot be scripted from here.

### What b minus a means, and what it does not

The always-on counters of the patched build (the `SeSsPerfC` increments, the idle/bus counters F12 would
gate) run in **all three** configurations, the client being connected or not. So `b - a` measures the cost
of *being connected* (snapshot capture and perf delivery), not the cost of the counters. To judge F12 you
need the build pair the plan names: the patched build with instrumentation forced on versus gated off,
both measured as configuration (a) while disconnected. Run the procedure once per build into two output
directories and compare with `analyze_v3.py DIR_NEW --vs DIR_OTHER`, which compares the two (a) sets.
The same holds for base `6491e73` (speed baseline only) and for "guest counters with attribution off versus
on": this harness records no guest counters, so that comparison is not covered here.

### Limits you will meet

* **Stream ceiling.** The server keeps 16 records per connection and a reply carries at most 8; the driver
  polls at about 120 Hz, so at most about 960 frames per second can be delivered whole. An unthrottled
  emulator faster than that makes (b) and (c) lose records and the run is flagged invalid. Then use
  `--throttle on` (CPU per frame only) or accept that (b)/(c) speed cannot be measured above the ceiling.
* **Attribution on is meant to be several times slower** (it runs the per-instruction loop). That is the
  designed cost of the opt-in, not an F12 finding.
* **F13 (UI caching)** is about the panel's timing. There is no UI here; V3 yields nothing for F13 except
  the fact that the client-side drain is cheap. A visible UI cost needs a separate measurement of the
  UI frame.

## Thresholds

The plan gives none: F12 is "only if V3 shows a cost", F13 "only if timing shows a visible cost".
`analyze_v3.py` prints the numbers and the quotes and decides nothing. The only numeric defaults in the
tooling are validity rules of the harness, adjustable on the command line and not from the plan:
at least 1000 frames and 5 s in a window; repetitions of one configuration may differ by at most 3 percent
(range over median), and the last third of repetitions may not sit more than 3 percent from the first third.

## Validity rules (a run is excluded, and the reason printed)

Any sample lost in the window (server ring or client queue, exact counts from the record stream,
cross-checked against the driver's own totals); `sample_seq` not consecutive in the window; attribution
mode of any window frame different from the one requested; timeline change inside the window (a state
load); records without frame identity; too few frames or too short a window; the client's own verdict.
Losses during the discarded warm-up are reported but do not invalidate.

## Preparation

1. Build: `cmake --build build --target se-bench-client` (Windows and macOS build it too; the CPU column is Linux only).
2. Build the patched Mednafen (`Integration/Mednafen`), record `git -C _emu/mednafen rev-parse HEAD` and
   the patch revision. Use a build with the profiler (`SE_LIVE_CAP_PROFILER`) and function attribution
   (`SE_LIVE_CAP_PROF_FUNCTIONS`); the client refuses otherwise.
3. Pick the scene (see `Docs/ProfilerFixPlan.md` V2 for candidates), run it in the emulator, and save it to a
   numbered save slot with the emulator's own hotkey. Note the game revision and the slot.
4. Quiesce the machine: AC power, performance governor (`cpupower frequency-set -g performance`),
   no other heavy processes, same desktop state for every run.
5. The setting names in `run_v3.sh` are checked against `<emulator> -help` before anything runs; if your
   Mednafen names one differently the script stops. The pinned set is: `-sound 0`, `-nothrottle 1` (throttle off),
   `-video.fs 0`, `-video.glvsync 0`, `-video.frameskip 0`, `-fps.autoenable 1`, `-cheats 0`, `-autosave 0`, and with
   `--cache MODE` the SH-2 cache setting named by `CACHE_SETTING` (default `ss.cpucache_emumode`; check the name
   with `-help`). Video driver: set `SDL_VIDEODRIVER` in the environment if the machine is headless. Keep
   the window size and driver identical across runs.

## Run

```
Integration/Validation/V3/run_v3.sh --emu /path/mednafen --game /path/game.cue --slot 0 \
    --bench build/bin/se-bench-client --out runs/2026-xx-xx-patched --reps 5 --warmup 10 --duration 60 \
    --throttle off --cache 1
python3 -I Integration/Validation/V3/analyze_v3.py runs/2026-xx-xx-patched
```

Each repetition runs a, b, c (interleaved: a b c a b c ...), each from a fresh emulator process that is
placed on the scene by `se-bench-client --mode load-slot` and then left alone for `--settle` seconds.
Output per run: `runNN_X.csv` (one row per frame: kind, sample_seq, frame_no, timeline_id, mode, gap_count,
evict_count, arrival_ns, newest_seq), `runNN_X.summary` (exact per-cause loss counts, validity, CPU samples),
logs, and `run_meta.txt` (machine, governor, hashes, pinned arguments). Five repetitions at 60 s is about
twenty minutes. Use at least five; the bootstrap interval is not informative below three valid runs per
configuration.

Exit codes of `se-bench-client`: 0 valid, 1 error, 2 completed but invalid (lost samples).

### Windows

`run_v3.sh` needs a POSIX shell (Git Bash or WSL; under WSL the emulator and client must both run in WSL or
both outside, because the client uses the default named pipe on native Windows). Natively: the same
sequence by hand or in PowerShell for each repetition and each of a, b, c:

```
start mednafen.exe <the pinned arguments from run_meta.txt> -force_module ss game.cue
se-bench-client.exe --mode load-slot --slot 0
Start-Sleep 3
se-bench-client.exe --mode bracket|stream [--attribution on|off] --warmup 10 --duration 60 --csv run01_a.csv --summary run01_a.summary
```

Name the files `runNN_a|b|c`. There is no `--pid` CPU column on Windows, so use the speed metric
(throttle off) and the emulator's own fps readout; analysis is the same script. This path is untested.

## Results template

```
Machine:            CPU / RAM / OS / governor / power
Emulator revision:  Mednafen <ver> + patch <rev>      binary sha256:
Build flags:        (compiler, -O level, SE_* defines, cache mode build option)
Game revision:      (title, region, disc image sha256)
Scene / save slot:  (what it is, how it was reached, slot N)
Settings:           (run_meta.txt pinned_args, SDL driver, window size, throttle on/off, cache mode)
Repetitions:        N, warm-up s, window s, valid runs per config (a/b/c)
Mednafen on-screen fps cross-check for (a): ____ (read during a window)

                      speed fps (median, range%)     cpu ms/frame (median, range%)
(a) no client
(b) attribution off
(c) attribution on
Overhead b vs a:      __% [__, __]                   __% [__, __]
Overhead c vs a:      __% [__, __]                   __% [__, __]
Build comparison (--vs), if run: ...
Invalid / excluded runs and why:
Warnings printed by the analyzer:

Conclusion for F12 (plan: "only if V3 shows a cost"):
Conclusion for F13 (plan: "only if timing shows a visible cost"):
What this run did not cover:
```
