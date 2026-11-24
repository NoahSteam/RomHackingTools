# Profiler: fix plan after external review

Status: **plan only, nothing below is implemented.** Revision 2 of this plan.
Code under review: `4b6cd9e` (branch `claude/laughing-hypatia-tagh4r`), base `6491e73`.
Pinned emulator: Mednafen 1.32.1 fork at `783518d`.
Feature description: [Codebase/Systems/Profiler.md](Codebase/Systems/Profiler.md).

Two external reviews so far:

1. Of the code: **merge after fixes** (ten findings).
2. Of revision 1 of this plan: **ready after changes**. The required changes were a
   producer-side frame identity, slave state restoration, one VDP1 metric, and cycle-accounting
   tests in place of flag-only ones. This revision makes them.

Line numbers below refer to `4b6cd9e` and to the patched Mednafen tree; they are navigation
aids and will move.

## Rules for every item

- A "Done when" must fail on the old behaviour. Tests assert exact emitted counters, cycles and
  function keys, never just flags or "distinct values".
- A test that calls a new accounting helper directly does not show the emulator reaches it. The
  transition under test stays real; only the surrounding devices and transport are stubbed
  (see "BIOS-free component tests").
- A missing measurement must never look like a measured zero.
- Raw totals (sleep, stall, on-time) stay available alongside clamped display values, so
  clamping cannot hide a broken producer.

## Decisions taken

| Question | Decision |
|---|---|
| Protocol version | **v27.** New fields outside the length-delimited record (gap information) would misalign a v26 reader while both peers claim v26. |
| VDP1 figure | **Command-work cycles**, counted at the command-budget debit. The elapsed "drawing" figure is removed. |
| Function ownership when uncertain | Fall back to raw-PC attribution, driven by an explicit per-CPU ownership-validity state. Full exception-stack reconstruction stays deferred. |
| Hook-registration race | Fix publication order without moving stateful setters ahead of init. Revision 1's "register everything before init" was wrong: `SeStateStartWorker` returns while `sRunning` is false (`se_export.c:747`), so it would silently disable rewind. |
| Gating the always-on counters | Measure first (V3); gate only if the measurement justifies it. |
| UI caching | Deferred unless timing shows a user-visible cost. |
| Guest-program verification (V1) | Expected values written now; first run moved to right after the F2/F4/F6 work, on a machine with a BIOS. |

## Step 0: freeze the v27 record before writing code

Everything that touches the wire is decided together, so the record changes once:

- `sampleSeq` (64-bit): orders perf publications within one exporter run.
- `timelineId`: changes on every successful replacement of machine state.
- `frameNo` (64-bit): the emulated frame within that timeline.
- Latched attribution mode for the frame.
- Per-CPU "other" cycles, split into *not admitted to the table* and *evicted from the top 64*,
  plus a saturation flag (F7).
- Configured instruction-cache emulation mode (F8).
- New counters, appended only, no index reused or reordered: slave on-cycles integrated at
  transitions (F2), VDP1 command-work cycles and VDP1 budget discarded by DMA (F4), 68000 idle
  cycles (F5), A-bus dummy and unmapped bus buckets (F8).
- Gap information attached to a known sequence boundary, so the client can derive the exact
  missing range (F3).

Checks: the new counter count still fits `se_live_perf_frame`; existing indices are unchanged.

Compatibility tests: new client with old server; old client with new server is rejected cleanly
at the version check; matching versions over consecutive replies. Document that a v25 client
already rejects a v26 server at its version check, so emulator and app must be updated together.

## Phase 1: identity, ownership and frame lifecycle

### F3. Frame identity and lost records

- **Defect (confirmed):** `LiveDriver.cpp` stamps the reply's snapshot frame on every record of a
  batch. The server's 16-deep ring silently advances a lagging cursor. `perfSent == 0` means
  both "nothing sent" and "first reply" (`se_export.c:2488`), so an empty first reply later
  skips records. A 32-bit wrap is treated as a restart. `SeExportInit` leaves the ring populated.
- **Why reading `sFrameNo` at end of frame is not enough (confirmed):** it is assigned in
  `SeExportSnapshot`, which Mednafen reaches from `MidSync` inside `if(!espec->NeedSoundReverse)`
  (`ss.cpp:884`), and which a debugger halt also reaches; the first halt behaves specially when
  `sFrameNo == 0`. A completed frame could carry its predecessor's number, and a "distinct
  tooltips" test would still pass.
- **Design:**
  - After the frame gate has applied any pending load and before guest execution, reserve the
    current frame's identity. Ordinary snapshots, mid-frame halt snapshots and the end-of-frame
    perf record all use that same identity. Commit it once when `Emulate()` completes.
  - Existing snapshot and rewind numbering is preserved for ordinary execution. The first frame
    after a rewind to N is N+1. A native slot load with no saved SE frame number starts a new
    timeline with a documented numbering origin.
  - One owner for timeline changes: successful SE rewind and slot loads (`SeStateAfterRestore`)
    and native Mednafen loads (`ss.cpp` `StateAction` and its `PostStateLoad` calls) funnel into
    one notification, so an SE slot load cannot bump the timeline twice. A failed load leaves it
    unchanged. `sRestoreDone + sRestoreFailed` is not usable for this, since failures change it.
  - "First reply" becomes its own flag; ordering is wrap-aware (the sequence is 64-bit on the
    wire); `SeExportInit` clears ring, sequence and capability state.
  - Queued records are cleared or generation-tagged before a reconnected source is shown.
  - The model stores gap positions; the plot breaks at a gap and an averaging window shows its
    real denominator.
- **Done when**, asserting exact identities in each case:
  - a batch of eight records;
  - several halts inside one frame;
  - a frame with no normal snapshot (`NeedSoundReverse`);
  - rewind to an already-used frame number;
  - native slot load, failed load, reconnect;
  - an initially empty first reply followed by three publications: all three arrive;
  - a non-empty first reply;
  - crossing `UINT32_MAX`;
  - deinit then init in one process: no stale record or capability;
  - loss from the server ring and eviction from the driver's 600-record queue each produce the
    exact missing range on the client.

### F14. Safe hook publication at startup

- **Defect (confirmed, narrower than revision 1 claimed):** listeners can read plain hook
  pointers such as `sSetProfiler` while the glue is still registering them. Not every
  registration has this problem, and `SeExportSetSaveStateHook` must run *after* init.
- **Design:** initialize locks and state, install the hook configuration and start dependent
  workers, then admit listeners. Either a configured-init entry point with a compatibility
  wrapper for existing callers, or synchronized publication of the affected pointers only.
- **Done when:** with a startup barrier, a client asks for capabilities at the first permitted
  instant and sees the profiler capability; rewind save and restore still work; deinit/reinit is
  clean; the absent-hook configuration behaves; a race-detector run passes where supported.
- Goes before F1, because F1 advertises a capability on the first reply.

### F1. Per-connection profiler request

- **Defect (confirmed):** one global request, cleared only when the last client leaves
  (`se_export.c:1994`). Two holes in revision 1's fix: `SeRegisterClient`/`SeUnregisterClient`
  are POSIX-only (Windows named pipes bypass them), and `SeOnClientDisconnect` independently
  publishes zero, so an old last-client cleanup can overwrite a new client's request.
- **Design:** own the request in the cross-platform `SeServeClient` lifetime. Under one lock:
  change membership or flags, recompute the OR, publish the desired value. Remove the separate
  profiler zero from last-client cleanup. Only the emulate thread applies the desired mode. The
  driver restates its setting, including "off", on every new connection.
- **Done when**, asserting behaviour after a known application boundary: both requesters on; one
  requester off; requester disconnects with a passive peer attached; old-client cleanup overlaps
  a new client's enable; reconnect restates off.

### F2 + F6. One frame-lifecycle contract

These share frame finalization and the restore path, so they get one contract.

- **Defects:**
  - F2 (confirmed): slave on-time is all-or-nothing per frame; an off/on gap inside a frame is
    charged to the function that ran before shutdown. `SH7095::SetActive` is the power hook, but
    state load deserializes `timestamp` without calling it, and a watchdog can call `Reset`
    directly while power stays on.
  - F6 (confirmed): a mid-frame mode change leaves a partial table stamped with the flag in
    force at publication; the final sampled interval of every frame is discarded at the rebase.
- **Design:**
  - Integrate real enable/disable edges on the scheduler's clock. Seed frame-start state from
    the actual CPU, close at `end_ts` before the rebase, reopen at zero next frame if still on.
  - On a successful load: discard partial accounting, seed from the post-load CPU state.
  - On CPU reset: account the final valid interval, then invalidate profiler ownership, pending
    delayed flow and sampling baselines, even if power remains on.
  - Latch the attribution mode for the next runnable frame, after frame-gate work and before
    `rltab[...][DBG_NeedCPUHooks()]` selects the run loop. A request arriving during a mid-frame
    halt is accepted and reported as pending; the old frame finishes in its old mode. The guest
    clock is stationary during a halt, so a long pause contributes zero cycles.
  - Callbacks needed by stepping, breakpoints or tracepoints are preserved.
  - Before `AdjustTS`: finalize each CPU's last valid sample, subtract its idle cycles, stamp
    the record with the latched mode.
- **Done when:**
  - in a 1,000-cycle frame, a slave on during [0,200) and [800,1000) reports exactly 400
    on-cycles, and the 600-cycle gap contributes zero function cycles;
  - the same across a frame rebase, a redundant `SetActive`, an on-to-off state restore that
    never calls `SetActive`, and a watchdog reset with power left on;
  - toggling in both directions, during a halt and while paused between frames, gives the right
    flag, exact named / raw / other cycle totals and callback state for the old and new frame;
  - a last instruction with a 37-cycle interval of which 7 are idle puts exactly 30 cycles on
    its prior key.

## Phase 2: emulator hooks

### F4. VDP1 command-work cycles

- **Defect (confirmed):** the elapsed interval is charged whenever a draw is active, including
  intervals a DMA holds VDP1 and time from before an automatic start. Moving the same increment
  into the `else if (DrawingActive)` branch would still give a short list the whole interval.
- **Design:** instrument the debit in `VDP1_EAT_CLOCKS(n)` (`vdp1.cpp:707`): evaluate `n` exactly
  once (the use at line 772 calls a command handler) and increment before the resumable `case`
  at line 708, so resuming a phase cannot charge twice. Remove the elapsed increment. Show raw
  work cycles; if normalized to the frame, allow values above 100% and flag instant-draw mode
  (the current clamp in `ComputePerfShares` would hide it).
- **Second counter:** active draw budget discarded by DMA. At lines 860 to 863, record the
  positive `CycleCounter` just before it is zeroed, only while `DrawingActive`. This is a budget
  quantity, not elapsed hold time; measuring the latter is optional.
- Keep restart and swap aborts named as such; count software ENDR (lines 1171 to 1179)
  separately or state its exclusion.
- **Done when**, asserting fetches, work and discarded budget separately: an END-only table gives
  one fetch and a 16-cycle debit; a forced phase yield and resume gives no second debit; an
  active DMA block; a block while inactive; an automatic start after a long idle interval;
  instant draw.

### F5. 68000 idle and executed time

- **Defect (confirmed):** stopped state is sampled once before a slice of many steps; four
  `MDFN_longjmp` paths return into `SOUND_Update` past both increments.
- **Design:** the idle hook is in `M68K::InternalStep` beside `timestamp += 4`
  (`m68k.cpp:2361`), after the reset/fault and interrupt branches have returned. The M68K core is
  shared with other systems and the SSF player, so it gets a per-instance idle accumulator (or
  optional observer) with no Saturn Explorer linkage. `SOUND_Update` saves the entry timestamp
  and idle baseline before `MDFN_setjmp`, leaves them untouched across recovery, and takes both
  deltas once at normal exit. Executed = total - idle.
- **Done when:** exact total, idle and executed for STOP partway through a slice, IRQ wake from
  STOP, external halt, and a real sound-bus `longjmp` after timestamp advancement; a second CPU
  instance is unaffected; a non-Saturn consumer of the core still compiles.

### F9. Function ownership validity

- **Defect (confirmed):** asynchronous interrupt entry is not an instruction presentation, a
  `JMP @Rn` can be a tail transfer, and ending continuous observation while a callee is active
  leaves it recorded after its unseen return. A fallback that triggers only when the stack is
  empty misses all three, because the stale top still has a valid address.
- **Design:** a per-CPU ownership-validity state, invalidated on actual exception entry, loss of
  continuous observation, reset or load, and an unproven non-call transfer once its delay slot
  completes. Invalid ownership means raw-PC attribution. Define when a freshly observed call
  re-establishes ownership and what happens when it returns to an unknown parent.
- **Done when:** IRQ, tail `JMP`, ordinary call/return with both delay slots, a nested call from
  unknown code, and disable/re-enable after an unseen return all show zero handler or tail-body
  cycles on the stale owner.

### F8. Bus classification and unit

- **Defect (confirmed):** `0x05000000` (A-bus dummy space) is labelled CD block; holes above the
  VDP2 and SCU windows are labelled as those devices. The unit is bus-controller requests, which
  varies with cache emulation.
- **Design:** dummy and unmapped buckets; neither falls into CPU-bus totals through the model's
  default mapping; the unit is named in the panel; the cache mode travels with the sample.
- **Done when:** both ends of each changed decode range are tested against the pinned SCU
  decoder (dummy A-bus, unmapped holes, real CD, VDP2, SCU); one split low-WRAM request, one
  SH-2 DMA request, a cache fill, a cache hit and a fast-map fetch give the expected counts.

### F11. DMA and DSP definitions

- "Descriptors started" and "bytes requested" for DMA. DSP time counts regular scheduled
  dispatches at two SH-2 clocks each; a manual control-register step bypasses the loop and is
  either counted or stated as excluded.
- **Done when:** an indirect DMA chain and a manual DSP step match the chosen definitions.

## Phase 3: front end

### F7. Function shares and the remainder

- **Defect (confirmed):** shares divide by the listed rows (`ProfilerPanel.cpp:557`). Separately,
  `SeMdfnProfAdd` admits no new key after 3,072 in a frame, so a function that dominates late in
  one frame can be missing even from the one-frame view; top-64 truncation is a different
  limitation that appears when frames are combined.
- **Design:** keep the bounded table. Track Other per CPU across both admission failure and
  top-64 eviction. Show an Other row, use the full denominator, state that listed rows are
  retained keys, and indicate saturation. Unobserved time stays distinguishable from measured
  cycles placed in Other.
- **Done when:** 30,000 of 100,000 with 40,000 in Other shows 30%; separate master and slave
  filters; a 65th key; a late dominant 3,073rd key; `listed + Other == attributed` per CPU.

### F10. Memory tab

- **Design:** states renamed to uniform / non-uniform / changed since sample. Re-prime on a new
  source, emulator instance or timeline. Describe the VDP1 draw-end latch and its fallback to
  live VRAM. Specify behaviour when scrubbing backward.
- **Done when:** two unrelated same-size captures with different bytes report zero changed
  blocks on the first sample after the switch; a real change within one source is reported.

### Labels

- Residual SH-2 "busy" time is labelled as including bus waits.
- Any tooltip that says "Frame" uses the real frame identity from F3.

## Phase 4: tests and baseline

- **T1.** `LiveProfilerTests`: establish the starting cursor, drain to an acknowledged final
  sequence instead of sleeping, assert exact continuity for a workload kept within ring
  capacity; deliberate loss is a separate test (F3).
- **T2.** Diagnose the intermittent frame-counter assertion in the profiler section of
  `MemoryRoundTripTests` and add a deterministic regression that fails on the old behaviour.
- **T3.** Each item's tests land in its own phase, including F4, F5, F9 and F14.
- **T4.** Run early. `SaturnExplorerDecompilerFaultTests` on `6491e73` and on the branch with the
  same compiler and flags; record the same failing matrix cell or explain the difference. The
  claim that it shares no changed sources is not a waiver.
- Build checks keep both SH-2 inclusion paths of `sh7095_ops.inc`; `end_ts` in SH-2 clock units
  stays the frame denominator.

## BIOS-free component tests

All three are feasible with patched Mednafen code and no BIOS. They do not replace V1.

| Item | Smallest faithful test | Still left to V1 |
|---|---|---|
| F2 | Drive the patched `SH7095::SetActive` branches with a controlled scheduler clock; cross a real rebase; keep the disabled-sentinel case; load serialized CPU state through the post-load path; feed real sampler and finalization output to the real exporter and driver. | That SMPC delivers transitions in the expected order during guest execution. |
| F4 | Drive the real patched `VDP1::Update` / `DoDrawing` with a tiny VRAM command list; control the DMA-halt predicate; force a phase yield and resume; automatic start at a non-zero timestamp. A stubbed `DoDrawing` is not acceptable. | Whole-emulator event order. |
| F5 | Instantiate the real `M68K` with test memory and IRQ callbacks and run real STOP / wake / halt steps; a Saturn sound harness that reaches one real faulting bus handler and its `longjmp`. A mock `SoundCPU.Run` is not acceptable. | Patch placement in the full build. |

## Phase 5: verification on the emulator

Needs a Saturn BIOS, discs and a toolchain, which the build environment used so far lacks.

- **V1. Deterministic guest program.** Expected observations are written before the first run.
  First run as soon as F2, F4 and F6 are integrated, before UI work or commercial scenes. Slave
  held off for a whole frame, then on mid-frame; a known call / delay slot / body / return; a
  fixed uncached work-RAM transfer; a one-command VDP1 list; a work-RAM-to-VDP1 SCU DMA. Both
  cache modes. Each check asserts independently expected numbers, and disabling the relevant
  hook must make it fail.
- **V2. Commercial scenes**, integration only. Record game revision, state and input, cache mode
  and the independent trace event compared. Virtua Fighter (USA) attract fight; Thunderstrike II
  (USA) gameplay entry (instant-draw mode); a repeatable Dragon Force load or battle transition.
- **V3. Benchmark.** Same state and input recording; rewind, renderer, audio and throttling held
  constant. No client; client attached with attribution off; attribution on. Base `6491e73`
  gives a speed baseline only. To isolate gating, compare the patched build with instrumentation
  forced on against gated off while disconnected. Compare guest counters with attribution off
  and on within the patched build, since the continuous debug callback changes peripheral update
  cadence. Ownership is checked against an independent instruction / exception trace, not the
  Call Stack panel, which shares the profiler's stack producer.

## Conditional on measurement

- **F12. Gate bus and idle counters** behind an emulate-thread flag latched at the frame
  boundary, only if V3 shows a cost. If done: attaching mid-frame advertises the capability at
  once and publishes the first *complete* measured frame.
- **F13. UI caching**, only if timing shows a visible cost. If done: invalidate on clear, source
  or timeline change, filter or window change, name-resolution change and plot dimensions; for
  memory, on display identity and edits as well as revision.

## Deferred

Raw-PC folding into functions, inclusive time, separating bus waits, storing profiler frames
with rewind, CSV export, a per-scanline bus-ownership timeline, full exception-stack
reconstruction, elapsed DMA-hold duration for VDP1.

## Order

0. Freeze the v27 record. Start T4 and write V1's expected values in parallel.
1. F14, then F3, F1, and the F2 + F6 lifecycle contract, as separately testable changes.
2. F4, F5, F9, F8, F11. First V1 run once F2, F4 and F6 are in.
3. F7, F10, labels.
4. T1, T2 and remaining tests; V2 and V3.
5. F12 and F13 only if V3 calls for them.
