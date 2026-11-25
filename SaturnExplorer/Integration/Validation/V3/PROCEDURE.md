# V3: profiler cost on the emulator, recording procedure

This is the harness for `Docs/ProfilerFixPlan.md` Phase 5, item V3, and the numbers it produces
are the input to the plan's "Conditional on measurement" items F12 and F13. The harness was built and
tested against the real exporter (`se_export.c`) and driver with a stand-in emulator thread, which proves it
records, flags and computes correctly; it says nothing about how fast or slow the patched Mednafen is. It has
been run once against a real emulator, on a machine that was not quiesced: see "First real run, 2026-11-25"
at the end of this file, which is provisional and not a result.

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
   `SeExportEndFrame`). Perf records, by contrast, are published only while a client is attached, so the first
   reply a new connection receives carries the newest publication, which is the PREVIOUS connection's last
   frame and stale: a bracket probe skips it and takes as its edge the first frame published after it attached
   (`sample_seq` greater than the `newest_seq` of that first reply).
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
that costs less: every verb replies with a full snapshot, and the driver polls with an 8 ms sleep between requests (measured here: about 80 replies per second over the unix socket). So
"a client that wants nothing" does not exist, which is why the plan's "no client" cannot be a probe
that stays connected. The bracket makes (a) client-free for the whole window at the price of two short
connections inside it (a few hundred milliseconds of client-attached emulation per 60 s, reported as
`probe_hold_*_ms` in the summary; keep it under about 1 percent of the window). A probe stays attached until
it has read one fresh frame (one emulated frame period or two longer than a stale-frame probe would) and the
reply to its `REW 0`; with a 60 fps emulator that is roughly 20 to 40 ms per probe, plus the wait for the second
reply, so configuration (a) carries slightly more client time than the hold figures of earlier runs showed.
The figures in `probe_hold_*_ms` count exactly this (from the fresh frame to hang-up). A probe that sees no fresh
frame within `--connect-timeout` (a paused emulator) fails the run with exit 1 instead of hanging. Each bracket
also carries `bracket_probe_inconsistent` (fresh frames whose numbers do not follow their sample sequence) and is
invalid when the end probe is not later than the start probe. The distance of the two `sample_seq` values is
not a check: the sequence stands still while detached, so it is small by construction.

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

### One directory per run

`run_v3.sh` refuses an `--out` directory that already holds run files, and a failed slot load or client error
stops the whole run, deletes that attempt's partial CSV and leaves an `ABORTED` marker. `analyze_v3.py` counts
nothing from a directory with that marker. Start again in a new directory.

### Limits you will meet

* **Stream ceiling, and TCP.** The server keeps 16 records per connection and a reply carries at most 8, so the
  frame rate that can be delivered whole is 8 divided by the driver's poll period. Measured with
  `se-v3-poll-probe` (real exporter and driver, a stand-in emulator, one machine; the stand-in's snapshots are
  empty, so a real emulator's period will be longer, not shorter):

  | transport | poll period | replies/s | ceiling (8 per reply) | frames lost at 226 fps |
  |---|---|---|---|---|
  | unix socket | 12.4 ms | 81 | about 650 fps | 0 |
  | TCP before `TCP_NODELAY` was set | 30 ms | 33 | about 265 fps | 61 to 84 in 3 s |
  | TCP now (`TCP_NODELAY` set by the exporter and the driver) | 11.9 ms | 84 | about 670 fps | 0 |

  The exporter writes each reply as roughly twenty `send()` calls, several of them 4 bytes; with Nagle's
  algorithm on, each small segment waited for the peer's delayed ACK, which stretched every poll round trip
  over TCP. Both ends now set `TCP_NODELAY` (the exporter's TCP accept loop in `se_export.c`, the TCP connect in
  `LiveDriver.cpp`), and the probe measures the same period on both transports. An emulator patched before
  that change still has the 30 ms period. The named pipe used by default on Windows has **not** been measured.
  An unthrottled emulator faster than the transport's ceiling makes (b) and (c) lose records and the run is
  flagged invalid; only `--throttle on` (CPU per frame) works then.
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
3. Pick the scene (see `Docs/ProfilerFixPlan.md` V2 for candidates), run it in the emulator under the same
   `--cache` mode you will benchmark (see "The save state and the cache mode" below), and save it with Mednafen's own save-state key into a numbered slot (the
   selected slot changes with its `state_slot_dec` / `state_slot_inc` keys, `-` and `=` by default,
   `drivers/input.cpp:1357`). Note the game revision and the slot. `se-bench-client --mode load-slot --slot N` asks
   the patched emulator to load that slot with its own loader (ELS verb; `apply.py:650-660` builds the path with
   `MDFN_MakeFName(MDFNMKF_STATE, slot)`, which includes the disc's MD5, so the slot only loads for the same
   disc image). `-autosave` is not used and is pinned to 0.

   **Where the state lives.** Mednafen keeps states in `<base>/mcs/<game>.<md5>.mc<N>`, `<base>` being
   `$MEDNAFEN_HOME` when that is set, else `$HOME/.mednafen` whenever `HOME` is set (on Windows too: it never
   reads `USERPROFILE`; Git Bash sets `HOME`, a plain `cmd` window usually does not), else on Windows the
   emulator exe's own folder. Saturn Explorer's GUI launches Mednafen with `MEDNAFEN_HOME` set to the emulator exe's folder
   (`WindowsPlatform.cpp:446`), so **a state made through the GUI is in `<exe dir>/mcs`**, which a shell run
   without `MEDNAFEN_HOME` does not read (the first real run lost time to exactly this). `run_v3.sh` therefore
   checks, before it starts anything, that `<base>/mcs/*.mc<slot>` exists, prints the candidates it found (the
   file name carries the disc's MD5, which it does not compute; Mednafen loads the matching one), and otherwise
   stops naming the folder it looked in and suggesting `--mednafen-home <exe dir>`. `--mednafen-home DIR` exports
   `MEDNAFEN_HOME=DIR` to the emulator it launches. It never copies or moves a state. Setting `MEDNAFEN_HOME`
   moves more than states: `mednafen.cfg` (read and, for settings not in the override file, written) and
   `firmware/` (the BIOS) are looked for under the same base, so with a base that has no `mednafen.cfg` Mednafen
   creates a default one there and will not find a BIOS unless `firmware/` is in it. The pinned settings still go
   in through `-ovconfig` and `-dump_settings_def` is run with the same environment, so the settings preflight
   is unaffected; the base used is recorded in `run_meta.txt` (`mednafen_base`, `mednafen_base_from`).
   `--skip-state-check` turns the check off.

   **The save state and the cache mode.** The state must be made under the same SH-2 cache mode the run pins:
   if you pin `--cache full` (`ss.dbg_cem`), make the state with that mode in force, because the emulator's
   cache contents are part of the scene and a state made under another mode (the GUI's default mode, say) runs a
   different workload, or not the same one, under a pinned mode. The first real run was made with the
   cache **unpinned** because its state came from the GUI's default. Unpinned is allowed: leave `--cache` off and
   `run_meta.txt` records `cache=unpinned`; the result is then "speed under whatever mode the GUI state was made
   under", and the results note must say so. Do not pin a mode and load a state from another one.
4. Quiesce the machine: AC power, performance governor (`cpupower frequency-set -g performance`),
   no other heavy processes, same desktop state for every run.
5. Settings. `run_v3.sh` writes the pinned settings to `OUT/pinned.cfg` and gives it to the emulator with
   `-ovconfig`, so they are neither saved into your `mednafen.cfg` nor left behind (a setting given as
   `-name value` on the command line is saved there by Mednafen, `drivers/main.cpp:981`). Before any run it asks the
   emulator for its settings (`-dump_settings_def`; `-help` lists no settings, and an override file silently
   ignores unknown names) and checks every pinned name, type and value with `check_settings.py`. Each entry was
   **checked against the pinned Mednafen source** (`_emu/mednafen/src`), not recalled:

   | Setting | Type, legal values | Pinned | Source | Why |
   |---|---|---|---|---|
   | `sound` | BOOL | 0 | `drivers/main.cpp:186` | no audio device; without it `MDFND_Update` takes the timer branch at `main.cpp:2549` |
   | `nothrottle` | BOOL | 1 (0 with `--throttle on`) | `drivers/main.cpp:181`, used at `2549-2552` | skips `ers.Sync()`; the description says "when sound is disabled", and the code only reaches it then |
   | `video.frameskip` | BOOL | 0 | `drivers/main.cpp:167`, used at `1306` | with the limiter off the emulator is always "behind"; frameskip would drop VDP work |
   | `video.blit_timesync` | BOOL | 0 | `drivers/main.cpp:170`, used at `2682-2696` | only the order of blit and sound sync; pinned to remove a wait |
   | `video.glvsync` | BOOL | 0 | `drivers/video.cpp:197`, used at `965` | OpenGL swap interval of the blit thread |
   | `video.fs` | BOOL | 0 | `drivers/video.cpp:185` | windowed |
   | `fps.autoenable` | BOOL | 1 | `drivers/main.cpp:199` | the on-screen fps cross-check |
   | `cheats` | BOOL | 0 | `mempatcher.cpp:998` | no patches |
   | `autosave` | BOOL | 0 | `drivers/main.cpp:182` | no state load or save on open or close |
   | `ss.dbg_cem` | ENUM `data_cb`, `data`, `full`, `auto` | `--cache MODE`, else unpinned | `ss/ss.cpp:2572` (list `2475-2483`), applied at `1326` | SH-2 cache emulation override; non-persistent and undocumented, so it appears in no manual |

   What can still cap the emulation thread, read from `drivers/main.cpp` and `drivers/video.cpp`: only
   `ers.Sync()` (removed by the two settings above). The blit thread never blocks it for more than the 100 ms
   starvation guard in `PassBlit` (`main.cpp:2604-2616`), and `video.driver` (`default|opengl|softfb`,
   `video.cpp:183`) is recorded, not pinned: choose one, keep it, set `SDL_VIDEODRIVER` if headless. There is no
   `ss.videoip` setting in this source. The per-run record `run_meta.txt` holds the settings file and hashes.

## Run

```
Integration/Validation/V3/run_v3.sh --emu /path/mednafen --game /path/game.cue --slot 0 \
    --bench build/bin/se-bench-client --out runs/2026-xx-xx-patched --reps 5 \
    --throttle off --cache full [--mednafen-home /path/to/mednafen/exe/dir]
python3 -I Integration/Validation/V3/analyze_v3.py runs/2026-xx-xx-patched
```

The default window is counted in frames, the same for all three configurations: `--frames 1500
--warmup-frames 300 --timeout 900`. `--warmup` and `--duration` are not used with it (only with `--frames 0` /
`--warmup-frames 0`, the seconds windows). Why 1500: the analyzer's validity floors are 1000 frames and 5 s in a
window and are not lowered. The slowest configuration (c, about 10 fps on the first real run) needs at least
1000 frames, and 1500 leaves the end-of-window overshoot and any shortfall a margin; the fastest (a, about
100 fps) then takes 15 s, above the 5 s floor, so it stays valid up to about 300 fps. A host faster than that
needs a larger `--frames`; one whose slow configuration is slower than 1 fps hits `--timeout`.

Expected wall time per repetition (a, b, c, each in a fresh emulator): window plus warm-up is 1800 frames, so
at 100 fps 18 s and at 10 fps 180 s, plus about 10 s per configuration of start, slot load, `--settle` and
cool-down. With (a) 100 fps, (b) 92 fps, (c) 10 fps that is about 18 + 20 + 180 + 30, roughly 4 minutes per
repetition and 20 minutes for five. At 10 fps for all three it is about 3.5 minutes per configuration.

If a run is invalid for too few frames or too short a window, `run_v3.sh` says so right after the run and
`analyze_v3.py` says so in its table, in both cases naming the change: raise `--frames` (so the slowest
configuration reaches the frame floor, or the fastest one the time floor). Raising the floor in the analyzer is
not the fix.

Each repetition runs a, b, c (interleaved: a b c a b c ...), each from a fresh emulator process that is
placed on the scene by `se-bench-client --mode load-slot` and then left alone for `--settle` seconds. Run it from a
shell where nothing else is going on: the first real run's (a) missed the repeatability threshold with the
machine at load average 9.65.
Output per run: `runNN_X.csv` (one row per frame: kind, sample_seq, frame_no, timeline_id, mode, gap_count,
evict_count, arrival_ns, newest_seq), `runNN_X.summary` (exact per-cause loss counts, validity, CPU samples),
logs, and `run_meta.txt` (machine, governor, hashes, pinned arguments). Use at least five; the bootstrap interval is not informative below three valid runs per
configuration.

Exit codes of `se-bench-client`: 0 valid, 1 error, 2 completed but invalid (lost samples).

### Windows counted in frames

`--frames N` (with `--warmup-frames W`) ends the measured window after N emulated frames, and the warm-up after
W, instead of after a number of seconds, and is `run_v3.sh`'s default. Every run then measures the same amount
of emulation whatever the speed, and a slow configuration is not compared over a different stretch of the game
than a fast one. The speed estimator is unchanged: frames divided by the time they took, from the same two
window edges. In stream mode the window also opens only once the frames carry the attribution mode that was
requested, so a warm-up shorter than the request's latency cannot make a run invalid.

**What `--frames` and `--duration` mean together (the same in both modes).** With `--frames`, `--duration`
is not used at all: the window is the frame count. (The first real run found bracket mode treating it as a
minimum gap with a 60 s default, which made a 3000-frame request a 3593-frame window, and `--duration 1`
made it poll 128 times.) The seconds options apply only when no frame count is given. `--timeout SEC`
(default 600; `run_v3.sh` passes 900) turns a run that cannot get there into an error (exit 1) instead of a hang.
The validity floors of the analyzer (1000 frames, 5 s) apply to either kind of window.

**How the bracket finds the end of a frame window.** The same way it finds the end of a frame warm-up: the
window opens at the start probe; then it sizes each sleep with the pacing rule of "Warm-up convergence" below
(the warm-up's observations are the baseline, so the first end look is already based on a long one; the rate
assumption is tighter here, 1.5 instead of 2, because every extra look is a connection inside the measured window),
never under 20 ms, and probes again, until the count is reached. The remaining frames shrink by a factor of about
1.5 to 2 per look, so the number of extra looks grows with the logarithm of the count (about 2 to 3 for 1500
frames), not with the window's length. Bounds: `--timeout`, 64 extra looks, and a probe that sees no fresh frame fails after
`--connect-timeout`. The end probe is the first look at or past the target, so the window overshoots by less than
the frames between the last two looks; the summary reports it as `window_overshoot_frames` (frames past the
target, both modes; stream: less than one drain period of frames) next to `window_by=frames|seconds`, and
`bracket_extra_probes` counts the looks that found the count not yet reached (each a short connection, kept in
the CSV; the perturbation of configuration (a) is these plus `bracket_warmup_probes`).

**What the warm-up means, in every configuration.** `--warmup-frames W` is counted from the *origin*: the first
frame published after the client attached (a new connection's first reply is the previous connection's last
publication and is not counted), and the window opens at the first observation at least W frames past it. If
`--warmup SEC` is given as well, the frame count wins and the seconds are not used (stream and bracket alike;
run_v3.sh passes both to all three configurations). Configuration (a) is disconnected between probes, and the
emulator publishes only while a client is attached, so its progress is read with extra short probes: an origin
probe, then probes after sleeps sized by the pacing rule of "Warm-up convergence" below (at least 20 ms), until
the count is reached. The probe that reaches it is the window's start
probe, so a frame warm-up costs `bracket_warmup_probes` connections (origin included, typically 4 to 6) and
`probe_hold_warmup_ms` of client-attached time before the window, each about 20 to 40 ms; that perturbs the scene
before the window opens, never inside it. Bounds: `--timeout` for the whole warm-up, 64 probes, and a probe that
sees no fresh frame (paused emulator) fails after `--connect-timeout`; all end in exit 1 with a message. A
timeline change during the warm-up (a state load) is also an error: load the slot first, with a separate
`--mode load-slot` run, as run_v3.sh does; the bracket itself never loads or saves a state and every probe sends
`REW 0`. Granularity: the window opens in `[origin + W, origin + W + step)`, where step is the frames between the
last two observations: one drain period for the stream (`--drain-ms`), the last probe interval for the bracket
(shrinking towards the 20 ms floor plus the connection latency). The summary records `warmup_frames_requested`,
`window_origin_frame`, `window_open_frame` and `warmup_overshoot_frames` (open - origin - requested) for both modes. With `--warmup SEC` only (no frame count) the
bracket sleeps SEC and takes one probe, as before. (The stream's origin used to be the first frame received,
which after a pause could be a stale publication from the load-slot connection; it is now the first fresh one.)

**Warm-up convergence.** The first version of the bracket's warm-up slept 90 percent of the remaining time
estimated from the origin probe and one probe about 50 ms later. A look's frame is the first one published after
it attached and its arrival is the drain instant, so each carries a latency (poll period, connection set-up, an
emulator that is slower while it serves a new client); over 50 ms the frame delta is a handful of frames and the
time between the looks is wrong by as much as itself. Arithmetic of the real run (origin 233, target 533, window
opened at 927 after 2 warm-up probes, emulator about 93 fps): the last sleep produced 694 minus the second probe's
delta, about 690 frames, about 7.4 s; with sleep = 0.9 * (300 - df) * dt / df that needs dt / df of about 28 ms per
frame, a rate estimate of about 36 fps against 93, a 2.6 times underestimate (for example 2 frames over 56 ms
where 5 were expected; inferred, the CSV of that run was not kept). The overshoot is proportional to the error
times the remainder, and nothing bounded it. The window end was fine only because its baseline was already long.
The stand-in reproduces it with an attach stall of 40 ms (`BenchClientTests.cpp`, `TestWarmupOvershootWithAttachStall`):
the old rule opened the window +183 frames late in 2 probes; the same run now overshoots by single digits.

The rule (`Pacer` in `BenchClient.cpp`), used for the warm-up and the window end:

* A rate is trusted only when its baseline spans at least 8 frames **and** 250 ms. The first sleep, with no baseline,
  is 250 ms (rate-free). 250 ms is five times the 50 ms bound on the latency difference between two looks, so a
  trusted rate is off by at most k = dt / (dt - 50 ms), 1.25 at the shortest baseline; 8 frames keeps one slow frame
  from dominating a baseline when the emulator is slow (at or above 32 fps the time condition decides, below it the
  frame condition does). Until trusted, a pessimistic factor of 4 is used.
* The rate is the one over the longest baseline (earliest look of the current regime to the newest). When the newest
  interval is itself trusted and its rate differs from the baseline's by more than the allowed variation either
  way, the baseline restarts at the previous look (a several-fold regime change).
* Each sleep is sized so that, were the true rate k * rho times the estimate, the frames in it still end at the
  target plus a budget B (5 percent of the request): aim = min(R + B/2, (R + B) / (k * rho)) frames, R the remaining
  frames, rho = 2 for the warm-up (1.5 for the window end). As the target nears, R falls below B / (k * rho - 1)
  and the aim crosses the target: the last look lands within about B/2 past it. The 20 ms floor and the
  deadline still apply.

Guarantee: **overshoot <= B + r * (L + 20 ms) + 1 frames**, where r is the true rate and L the latency of the last
look (poll plus connection plus up to one frame), provided (i) the latency difference between two looks is at most
50 ms and (ii) the mean rate over a sleep does not exceed rho times the baseline's rate. For the 300-frame warm-up,
B is 15: at most about 23 frames at 100 fps and 37 at 300 fps, against a typical few. Violating (i) with a long
stall, or (ii) with a rate that rises more than rho-fold inside one sleep (the stand-in's 10 times speed-up,
`wu_fast10`: +433, for a 40 ms to 4 ms step 12 frames in), is what can still produce a large overshoot; it is then
reported, not hidden (next paragraph). Slowing down is safe for the bound (the sleeps yield fewer frames than
planned): a 10 times slow-down (attribution turned on) took 7 looks in the stand-in, +2.

Expected `bracket_warmup_probes` for a 300-frame warm-up (a simulation of the rule with 30 ms of latency noise,
300 seeds, for 10, 60, 100 and 300 fps: 6, 6, 6 and 5 looks, overshoot at most 4, 8, 10 and 16 frames); the stand-in
with a 40 ms attach stall used 5. The window end of 1500 frames adds 2 to 3 extra looks besides the end probe (was 2).

**The warm-up overshoot is reported, and flagged.** `warmup_overshoot_frames` in every summary is open - origin -
requested. `analyze_v3.py` prints a FLAG on a run whose warm-up overshot by more than 20 percent of the request (60
frames of 300), and on every configuration of a repetition whose configurations differ in overshoot by more than that
(the stream's is a drain period, the bracket's the guarantee above, so a healthy repetition differs by well under
it). It is a flag and a warning, not an invalidation: the window is counted from its own opening frame, so the speed
estimate is unaffected; what is at stake is whether the configurations began measuring at comparable points of a
changing workload, which depends on the scene and is the operator's call. No validity floor was changed.

The tests use this: every V3 test counts frames and holds no assertion about frames per second, so they pass on a
loaded or slow host (`V3_TEST_FRAME_MS` and `FAKE_FRAME_MS` slow the two stand-ins down to check that). The script test runs its stand-in at 20 ms per frame by default: the live ring holds 16 records, so a stand-in much faster than the client can be drained loses samples whenever the client is starved, which is a correct "invalid" verdict about the run but not what that test is about.

### Windows

`run_v3.sh` needs a POSIX shell (Git Bash or WSL; under WSL the emulator and client must both run in WSL or
both outside, because the client uses the default named pipe on native Windows). Natively: the same
sequence by hand or in PowerShell for each repetition and each of a, b, c:

```
start mednafen.exe <the pinned arguments from run_meta.txt> -force_module ss game.cue
se-bench-client.exe --mode load-slot --slot 0
Start-Sleep 3
se-bench-client.exe --mode bracket|stream [--attribution on|off] --warmup-frames 300 --frames 1500 --timeout 900 --csv run01_a.csv --summary run01_a.summary
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
Settings:           (pinned.cfg and run_meta.txt, video.driver, SDL driver, window size, throttle on/off, cache mode)
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

## First real run, 2026-11-25 (provisional; not a pass/fail result)

Recorded as the operator reported it. **Provisional**: the machine was not quiesced (load average 9.65), the
save state had been made under the GUI's default cache mode so the cache setting was unpinned, and the windows
were not the ones this procedure now defaults to. Nothing here is evidence for or against F12 or F13.

* Machine and build: Windows 10, i7-7700, patched Mednafen 1.32.1, Panzer Dragoon II, `run_v3.sh` from Git Bash.
* A. `se-bench-client --mode bracket --warmup-frames 300 --frames 3000` produced windows of about 3,593 frames,
  not 3,000: with `--frames`, bracket mode treated `--duration` as a minimum gap and it defaults to 60 s. Adding
  `--duration 1` gave 3,001 frames but 128 extra probes (`bracket_extra_probes`), a heavy perturbation of the
  "no client" configuration. (Fixed afterwards: see "Windows counted in frames".)
* B. `run_v3.sh --slot 0 --reps 5 --warmup 10 --duration 60 --throttle off`:
  * (a) no client: 90 to 110 fps, median 106.0, cv 7.6 percent. It missed the 3 percent repeatability threshold.
  * (b) attribution off: median 92.0 fps, cv 3.9 percent.
  * (b) versus (a): +15.3 percent host time per frame, 95 percent interval [-2.0 percent, +23.5 percent], which
    includes 0.
  * (c) attribution on: about 10 fps, real, every frame delivered, 0 lost. About 9 times slower than (b) on that
    scene (92 to 10 fps). 60 s gave only about 600 frames, so all five (c) runs were INVALID under the analyzer's
    1,000-frame floor (not a loss problem).
* C. The slot-0 state was not where the run looked (GUI states are in `<exe dir>/mcs`; the shell run read
  `$HOME/.mednafen/mcs` -- under Git Bash the same folder as `%USERPROFILE%\.mednafen\mcs` -- empty); the operator copied the `.mc0` by hand. `run_v3.sh` now checks this
  first (see "Where the state lives").
* D. The state was made under the GUI's default cache mode, so the cache setting was unpinned (see "The save
  state and the cache mode").
* E. A later real run (Windows, unthrottled, about 90 to 95 fps, `--frames 1500 --warmup-frames 300`) showed the
  warm-up overshoot of the old pacing in bracket mode: run 1 origin 233, window opened at 927 (+394, 2 warm-up
  probes); run 2 origin 302, opened at 635 (+33, 3 probes). Earlier, throttled at 60 fps, five runs: +1, +4, +1, +24, +3
  (3 to 4 probes). The window end was fine (`window_overshoot_frames` 3 and 4 with 2 extra probes), and stream mode
  opened at +1, +2 and 0. The cause and the replacement are in "Warm-up convergence".
* F. The replacement pacing on the same machine (unthrottled, about 90 to 93 fps, fresh emulator per run,
  `--warmup-frames 300 --frames 1500`), three runs: `warmup_overshoot_frames` 6, 40 and 7 (at most 13 percent, under
  the 20 percent flag) with 5 warm-up probes each; `window_overshoot_frames` 29, 11 and 16 with 2 extra probes; all
  valid, none inconsistent.
* G. With the per-instruction device sync skipped for attribution alone (see Profiler.md, "Cost of attribution"),
  configuration (c) on this scene ran at about 31.6 fps instead of about 8.7, so the default 1,500-frame window is
  reachable within the default time-out. Not quiesced, so still provisional.
* Not done: the on-screen fps cross-check for (a) and a quiesced machine. A rerun with the defaults above on a
  quiet machine is what would produce a result.
