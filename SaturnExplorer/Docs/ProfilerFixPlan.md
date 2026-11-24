# Profiler: fix plan after external review

Status: **plan only, nothing below is implemented.**
Reviewed commit: `4b6cd9e` (branch `claude/laughing-hypatia-tagh4r`) against base `6491e73`.
Review verdict: **merge after fixes.**
Feature description: [Codebase/Systems/Profiler.md](Codebase/Systems/Profiler.md).

Each item names the defect, where it lives, the intended fix, and how it will be shown fixed.
"Confirmed" means the defect was re-read in the source after the review; "accepted" means the
review's reading matches how the code was written but the lines were not re-read one by one.

## Phase 1: wrong numbers or wrong behaviour

These are fixed before any game capture is interpreted.

### F1. Function profiling has no connection owner (confirmed)

- **Where:** `Integration/Common/se_export.c`: `SePublishProfiler`, `SeOnClientDisconnect`
  (only called when the last client leaves); `Drivers/Live/src/LiveDriver.cpp` reconnect path.
- **Defect:** the request is one global. With two clients attached, the one that asked for
  attribution can leave and the emulator stays in its slow per-instruction loop; two clients can
  also switch it off under each other. On reconnect the driver does not restate "off".
- **Fix:** a requested-flags word per connection; the applied value is the OR of live
  connections; a connection's contribution is removed where it is unregistered, under the same
  lock that adds it. The driver always restates its setting on a new connection.
- **Done when:** a test attaches two clients, enables from one, closes it, and sees the hook
  called with 0 while the other is still attached; and a reconnect with the setting off sends it.

### F2. Slave power transitions corrupt the frame split and function time (accepted)

- **Where:** `END_FRAME_HOOK` in `Integration/Mednafen/apply.py`; `SeMdfnProfSample` in
  `se_mednafen_glue.c`.
- **Defect:** slave on-time is all or nothing per frame, sampled at frame end, although SMPC can
  switch the slave inside a frame. The sampler never sees the disabled timestamp, so an
  off-then-on gap inside one frame is charged to the function that ran before the shutdown.
- **Fix:** accumulate on-time at `SH7095::SetActive` (close the interval on power-off, open it on
  power-on, close it at frame end); on power-off invalidate the sampler's baseline for the slave
  and reset its tracked call flow.
- **Done when:** the glue test drives off/on inside one frame and the record shows the exact
  on-time and no cycles charged across the gap.

### F3. History shows the wrong frame number and hides lost records (confirmed in part)

- **Where:** `LiveDriver.cpp` (`PollLoop` stamps the reply's frame on every record of a batch);
  `se_export.c` perf ring and `perfSent` cursor; `ProfilerPanel.cpp` `HistoryStrip` tooltip;
  `ProfilerModel::Push`.
- **Defect:** several records share one frame number; a lagging connection silently loses
  records when the 16-deep ring wraps; `perfSent == 0` means both "nothing sent" and "first
  reply", so an empty first reply later skips records (confirmed at `se_export.c:2488`); a
  32-bit sequence wrap is treated as an emulator restart; `SeExportInit` does not clear the ring.
- **Fix:** each record carries the emulator's own frame number and a timeline id that changes on
  a state load; the server reports how many records a connection missed; "first reply" becomes
  its own flag; ordering is wrap-aware; `SeExportInit` clears ring, sequence and capability
  state. The model stores gaps and the plot and averages show them.
- **Done when:** a test forces a lag past ring capacity and the client reports the exact number
  missed; tooltips show distinct, correct frame numbers.

### F4. VDP1 "drawing" counts time when drawing is blocked (accepted)

- **Where:** `VDP1_PERF_UPDATE_HOOK` in `apply.py`, ahead of the `SCU_CheckVDP1HaltKludge()` test
  in `VDP1::Update`.
- **Defect:** the whole elapsed interval is charged whenever a draw is active, including
  intervals a work-RAM-to-VDP1 DMA holds VDP1, and time from before an automatic draw started.
- **Fix:** charge only intervals in which drawing proceeds; start the interval at the draw
  start; report the DMA-held interval as its own counter. Decide and document whether the figure
  is "draw active" or "draw work".
- **Done when:** a controlled check shows zero drawing credited across a DMA hold, and the new
  held counter accounts for it.

### F5. 68000 split misclassifies slices and loses cycles (confirmed in part)

- **Where:** `SOUND_PERF_*` hooks in `apply.py`; `SOUND_Update` in Mednafen's `sound.cpp`.
- **Defect:** stopped state is sampled once before a slice that can run many steps; four
  `MDFN_longjmp` paths return into `SOUND_Update` past both increments (confirmed), losing those
  cycles from both totals.
- **Fix:** take total advancement across the whole update including the recovery path; count
  idle at the core's own stopped/halted step; executed = total - idle.
- **Done when:** total 68000 cycles per frame equal the scheduler's advancement in a test that
  forces the recovery path.

### F6. Mode changes mid-frame make a record's flag disagree with its data (accepted)

- **Where:** `SeMdfnSetProfiler`, `SeMdfnPerfEndFrame` in the glue; `SeExportPublishPerf`.
- **Defect:** switching attribution inside a frame leaves a partial table stamped with the flag
  in force at publication, so a record is either dropped by the model or weighted as a full
  frame. The last sampled interval of each frame is discarded at the rebase.
- **Fix:** latch the requested mode at the frame boundary; charge the final interval before the
  rebase.
- **Done when:** a test toggles mid-frame and every record is either fully attributed or empty.

### F7. Function percentages ignore the known remainder (confirmed)

- **Where:** `ProfilerPanel::DrawFunctionsTab` (`ProfilerPanel.cpp:557`).
- **Defect:** shares divide by the sum of listed rows, not by the total that includes "other".
- **Fix:** an explicit Other row; the full total as the denominator; per-CPU remainders for the
  CPU filter, or a label that limits the denominator; mark window rankings as approximate.
- **Done when:** the panel test gives 30% for 30,000 of 100,000 with 40,000 in Other.

## Phase 2: labels and honesty

### F8. Bus device labels and unit (accepted)

- **Where:** `PERF_BUS_HELPER` in `apply.py`; `PerfDeviceName` in `ProfilerModel.cpp`.
- **Defect:** `0x05000000` (A-bus dummy space) is labelled CD block; holes above the VDP2 and SCU
  windows are labelled as those devices. The count is of bus-controller requests, which differs
  from device accesses and changes with instruction-cache emulation.
- **Fix:** separate buckets for dummy and unmapped space (appended indices, none reordered);
  name the unit in the panel; export and show the cache mode.

### F9. Function ownership through interrupts and tail jumps (accepted)

- **Where:** `SeMdfnProfSample`, `SeMdfnTrackFlow` in the glue.
- **Defect:** asynchronous interrupt entry and tail `JMP` leave the previous owner in place.
- **Fix:** when ownership is uncertain, fall back to raw-PC attribution rather than charge a
  stale function. Feeding real exception events into the stack is a later improvement.

### F10. Memory tab claims more than a capture can show (accepted)

- **Where:** `MemoryPoolTracker` in `ProfilerModel.cpp`; `App::UpdateProfiler`,
  `App::AdoptNewEmulatorInstance`.
- **Defect:** "empty", "holds data" and "rewritten" overstate what uniform / non-uniform /
  changed-since-sample can establish; hashes survive a change of source or emulator, so the
  first sample of a new game shows differences from the old one as writes.
- **Fix:** rename the three states; re-prime on a new source, emulator instance or timeline;
  note that VDP1 VRAM is shown from the draw-end latch.

### F11. DMA and DSP names (accepted)

- **Fix:** label as "descriptors started", "bytes requested" and "DSP instruction time".

## Phase 3: cost

### F12. Gate the always-on counters

- Bus and idle increments go behind an emulate-thread flag latched at the frame boundary, set
  while a client is attached. Capability is advertised independently of a first sample.
- **Unverified today:** their cost has not been measured; see V3.

### F13. Cache UI computation

- Memory hashing only when the capture revision changes; frame shares, plots and the function
  table cached against newest sequence, window and filter.

### F14. Hook-registration race at startup

- `SeExportInit` starts listeners before hooks are registered. This affects every hook, not only
  the profiler's; fix by registering before listeners start, for all of them.

## Phase 4: tests

- **T1.** `LiveProfilerTests`: drain to an acknowledged final sequence instead of sleeping;
  assert exact continuity, not just increasing order.
- **T2.** Find the cause of the intermittent frame-counter assertion in the profiler section of
  `MemoryRoundTripTests`.
- **T3.** New tests named under F1, F2, F3, F6 and F7.
- **T4.** Run `SaturnExplorerDecompilerFaultTests` on `6491e73` and on the branch with the same
  compiler and flags, and record whether the failure predates the branch.

## Phase 5: verification on the emulator

Needs a Saturn BIOS and discs, which are not available in the build environment used so far.

- **V1.** A deterministic guest program run inside the patched Mednafen: slave off for a whole
  frame then on mid-frame; a known call / delay slot / body / return sequence; a fixed uncached
  work-RAM transfer; a one-command VDP1 list; a work-RAM-to-VDP1 SCU DMA. Compare the wire record
  with independent timestamps, instruction ownership and request counts, in both cache modes.
- **V2.** Commercial scenes as integration checks only, since none gives a known percentage:
  Virtua Fighter attract fight, Thunderstrike II gameplay entry (instant-draw hack), a Dragon
  Force load or battle transition.
- **V3.** Benchmark against base from one state and input recording: no client, client attached
  with attribution off, attribution on. Compare guest counters with attribution off and on.

## Deferred

Raw-PC folding into functions, inclusive time, separating bus waits, recording profiler frames
with rewind (depends on F3), CSV export, a per-scanline bus-ownership timeline.

## Order and dependencies

F3 changes the wire record, so it goes first with F1 and F6 (same files, one protocol revision).
F2, F4 and F5 are independent emulator-hook changes. F7 to F11 are front-end and labelling and
can follow in any order. F12 depends on V3 only for its justification, not its implementation.
