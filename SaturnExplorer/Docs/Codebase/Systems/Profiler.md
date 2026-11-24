# Profiler

Inspected revision: the change that introduced it (protocol v26), on top of
`6491e73`, plus the v27 wire/counter freeze (Step 0 of [ProfilerFixPlan](../../ProfilerFixPlan.md)).
**v27 status:** the record layout and counter indices below are frozen; most of the new fields are
declared and carried but not yet produced (each is marked "not available" on the wire by a clear
bit in the record's `valid` word) until the plan items F2 to F8 fill them. Behavior below is verified from source and from the tests listed; the
emulator-side numbers have **not** been checked against a running game (see Coverage limits).

## Purpose and responsibilities

The Profiler panel answers three questions about a running Saturn game, in the manner of a
console performance analyzer:

- **Where does each frame's time go?** Both SH-2s (busy / stalled on the bus / asleep /
  switched off), VDP1 drawing, the SCU DSP and the 68000.
- **What crosses the buses?** SH-2 external-bus accesses per device, grouped into the CPU
  bus, A-bus and B-bus, and SCU DMA traffic per level and per bus-to-bus route.
- **How full are the memory pools?** The fixed RAMs (work RAM high/low, VDP1/VDP2 VRAM,
  colour RAM, sound RAM): how much holds data and what is being rewritten.

Optionally it also attributes SH-2 time to **functions** (self time).

It owns the counters, their wire block and the panel. It does not own the shadow call stack
(it reads it; see [Debugger](Debugger.md)), function naming (`FunctionNames`) or the live
transport (see [State sources](StateSources.md)).

## Source locations, types and entry points

| Responsibility | Source and important symbols |
|---|---|
| Counter indices and the function key (single source of truth for emulator, wire and client) | [SePerfCounters.h](../../../Integration/Common/SePerfCounters.h): `SE_PERF_*`, `SE_PERF_DEV_*`, `SE_PERF_FN_*` |
| Emulator instrumentation | [apply.py](../../../Integration/Mednafen/apply.py): `PERF_DECL`, `PERF_BUS_HELPER`, the `*_PERF_*` / `SH2_*` anchors, `process_profiler`; `SsDbgCpuTimestamp`, `SsDbgSetProfileActive` in `SS_ACCESSORS` |
| Frame hand-over and function sampling | [se_mednafen_glue.c](../../../Integration/Mednafen/se_mednafen_glue.c): `SeSsPerfC`, `SeMdfnPerfEndFrame`, `SeMdfnProfSample`, `SeMdfnSetProfiler` |
| Server | [se_export.c](../../../Integration/Common/se_export.c): `SeExportPublishPerf`, `SeExportPrepare` / `SeExportStart` (startup), `SeExportSetProfilerHook`, `SeExportSetPerfCapable`, `SeExportCurrentFunction`, `SePublishProfiler`, the v27 perf record |
| Wire contract | [SeLiveProtocol.h](../../../Integration/Common/SeLiveProtocol.h): `SE_LIVE_VERB_PROFILE`, `SE_LIVE_PROF_FUNCTIONS`, `SE_LIVE_CAP_PROFILER`, `SE_LIVE_CAP_PROF_FUNCTIONS`, "Perf section" |
| Client | [LiveDriver.cpp](../../../Drivers/Live/src/LiveDriver.cpp): `LivePerf`, the v26 and v27 parse in `ReadSnapshot`; [LiveDriver.h](../../../Drivers/Live/src/LiveDriver.h): `se_live_perf_frame`, `se_live_drain_perf`, `se_live_set_profiler` |
| Model (UI-free) | [ProfilerModel.h](../../../FrontEnd/src/Debug/ProfilerModel.h): `PerfFrame`, `ComputePerfShares`, `ComputeBusTotals`, `ProfilerModel`, `MemoryPoolTracker` |
| Panel | [ProfilerPanel.cpp](../../../FrontEnd/src/ProfilerPanel.cpp): `ProfilerPanel::Draw`, `Host` |
| App glue | [App.cpp](../../../FrontEnd/src/App.cpp): `App::UpdateProfiler`, `App::DrawProfiler`; registered in `App::PanelList` (Windows → Debugger → Profiler, hidden by default) |

## Execution flow

```mermaid
flowchart TD
    I["Injected increments in Mednafen: SeSsPerfC[...]"] --> E["SeMdfnPerfEndFrame (end of Emulate)"]
    S["SeMdfnProfSample (per instruction, opt-in)"] --> E
    E --> P["SeExportPublishPerf: serialized record, seq + 1"]
    P --> R["Server ring of the last 16 frames"]
    R --> D["Each reply: the records this connection has not had yet"]
    D --> A["App::UpdateProfiler: se_live_drain_perf"]
    A --> M["ProfilerModel history"]
    M --> U["ProfilerPanel"]
    C["Captured RAM (any source)"] --> T["MemoryPoolTracker"] --> U
```

### Counters (always on)

The patch adds one 64-bit array, `SeSsPerfC`, and increments it where the work happens:

| Counter | Where it is counted |
|---|---|
| Frame length, slave-on time (v26 semantics; F2 integrates real on/off edges) | End of `Emulate()` (`end_ts`; the slave's timestamp reads `SS_EVENT_DISABLED_TS` while SMPC holds it off) |
| SH-2 sleep | The `SLEEP` op's idle step and `PSEUDO_STANDBY`, per CPU (`which`) |
| SH-2 bus stall | `PSEUDO_DMABURST` (DMA burst or external halt), per CPU |
| SH-2 bus accesses per device | `BSC_BusRead` / `BSC_BusWrite`, classified by a 64 KiB lookup table |
| VDP1 commands / starts / unfinished draws | the command fetch, `StartDrawing`, the frame-buffer-swap abort. The elapsed draw-cycle increment (index 6) is gone in v27; command-work cycles are added by F4 |
| SCU DSP cycles | The instruction loop of `SCU_UpdateDSP` |
| 68000 cycles (executed / total) | Around `SoundCPU.Run` in `SOUND_Update`; a slice that starts stopped or halted is not "executed" |
| SCU DMA transfers and bytes per level and route | `StartDMATransfer`, with the byte count the transfer was started with |

The increments are unconditional: each sits on a path that already does far more work.
Every hook is an insertion (never a replacement), so `apply.py --revert` restores the files.

`SeMdfnPerfEndFrame` runs from `SeMednafenEndFrameHook`, hands the array to
`SeExportPublishPerf` when a client is attached, and zeroes it. The server numbers each
frame (`seq`) and keeps the last 16; every reply carries the records its connection has not
been sent yet, oldest first (at most `SE_LIVE_PERF_MAX_PER_REPLY`). The records are a queue
beside the snapshot, not part of it: a client that is served a newer snapshot and skips one
still receives every frame's counters, and a paused or halted emulator sends none. The
driver additionally drops a `seq` it has already queued.

### Wire record and counter layout (v27, frozen)

The authoritative text is the comment at "Perf section" in
[SeLiveProtocol.h](../../../Integration/Common/SeLiveProtocol.h) and the v26 to v27 table in
[SePerfCounters.h](../../../Integration/Common/SePerfCounters.h); this is the summary.

Section (all little-endian): `u32 count, u64 newestSeq, u64 lostFirst, u64 lostLast`, then per
record `u32 length` and

| Field | Notes |
|---|---|
| `u64 sampleSeq` | orders publications in one exporter run, never 0 |
| `u32 timelineId`, `u64 frameNo` | frame identity (F3); valid with `SE_PERF_VALID_IDENTITY` |
| `u32 mode` | attribution mode latched for the frame (F6); `SE_PERF_VALID_MODE` |
| `u32 cacheMode` | `SE_PERF_CACHE_*` (F8); `SE_PERF_VALID_CACHE` |
| `u32 valid` | `SE_PERF_VALID_*`: which v27-only fields the producer really filled |
| `u32 satFlags`, `u64 otherNotAdmitted[2]`, `u64 otherEvicted[2]` | per-CPU Other split (F7); `SE_PERF_VALID_OTHER` |
| `u32 counterCount`, `u64 counters[]` | 62 from a v27 server |
| `u32 fnCount`, `{u32 key, u32 cycles}[]`, `u32 fnOtherCycles` | as in v26 |

The lost range is the `sampleSeq` span the server dropped for this connection just before the
first record of the reply (`lostLast + 1` equals that record's `sampleSeq`); 0, 0 means none.

Counters: indices 0 to 53 keep their v26 meaning. Index 5 (slave on-cycles) keeps its index and
meaning (only its producer is corrected, F2). **Index 6 (v26: elapsed VDP1 drawing) is retired:
a v27 producer writes 0 and it is never reused.** Appended: 54 slave on-cycles integrated (F2),
55 VDP1 command-work cycles, 56 VDP1 budget discarded by DMA (F4), 57 68000 idle cycles (F5),
58 to 61 A-bus dummy and unmapped bus buckets, `[xdev * 2 + write]` (F8).

Version handling: the driver reads both forms by the negotiated server version. A v26 record is
presented with `se_live_perf_frame.valid == SE_LIVE_PERF_V26_VDP1_ELAPSED` only: counter 6 is
the old figure, shown under its old label ("VDP1 drawing"), and identity, mode, cache mode, the
Other split and every v27-only counter are "not available". A v27 record is shown as "VDP1
command work", and as "not available" until `SE_PERF_VALID_VDP1_WORK` is set. A v26 client
rejects a v27 server at its version check (a v25 client already rejects v26): update emulator and
app together.

Capability bits (documented in `SeLiveProtocol.h`), both correct in the very first reply (F14):
`SE_LIVE_CAP_PROFILER` means counters are produced, declared at startup with
`SeExportSetPerfCapable` (not inferred from the first publication); `SE_LIVE_CAP_PROF_FUNCTIONS`
means the per-instruction hook exists, so a build without debugger support advertises only the
first.

Startup order (F14): `SeMednafenFrameHook` calls `SeExportPrepare` (locks and state, server marked
running so the savestate worker can start), registers every hook and the counter capability, and
only then `SeExportStart` admits listeners. Moving registration before init would not work:
`SeStateStartWorker` returns while `sRunning` is false, silently disabling rewind. `SeExportInit` is
Prepare + Start for callers with nothing to register; a deinit/init cycle clears the declared
capabilities and the profiler hook.

### Function attribution (opt-in)

`SE_LIVE_PROF_FUNCTIONS` (the `PRF` verb) makes `SeSyncCpuHook` keep the continuous
per-instruction callback installed, exactly as a tracepoint does. That selects Mednafen's
debug run loop, which is several times slower, so:

- the client sends it only while the panel is open with the checkbox ticked
  (`App::UpdateProfiler` restates the setting every frame; the driver sends changes);
- the server drops it when the client disconnects (`SeOnClientDisconnect`);
- it needs `--enable-debugger`; otherwise no hook is registered and the capability bit is
  clear, and the panel disables the checkbox.

Per instruction, `SeMdfnProfSample` charges the cycles since that CPU's previous
instruction, minus the sleep/stall cycles counted in between, to the function the previous
instruction was in. The function is the innermost shadow-call-stack frame's entry
(`SeExportCurrentFunction`); with no recorded frame (code already running when profiling
began, e.g. the main loop) the key is a 16-byte bucket of the raw PC, flagged
`SE_PERF_FN_RAW_PC`. A per-frame hash table collects the keys; the 64 hottest go on the
wire and the rest are summed into `fn_other_cycles`.

### Memory pools

Not an emulator feature: `MemoryPoolTracker` reads the captured regions through
`ContextBackend`, so it works for savestates and scrubbed frames too. Each pool is split
into at most 1024 blocks; a block that is one byte value repeated is *empty*, anything else
*holds data*, and a block whose hash differs from the previous sample is *rewritten* (the
mark fades over later samples). Sampling runs only while the Memory tab is showing, every
tenth UI frame. This is occupancy of the fixed RAMs, not a view of any game's heap
allocator, which the hardware knows nothing about.

## State, ownership and invalidation

- `SeSsPerfC` and the sampling table are written by the emulate thread only.
- `sPerfBlk` (the server's record ring) is guarded by `SE_LOCK`; `perfSent`, the newest
  frame a connection has been sent, is local to that connection's serving loop, so the
  local-socket and TCP clients each get every frame.
- The driver's perf queue is guarded by `ctlMtx`, bounded at 600 records, and cleared on a
  new connection; `perfSeqSeen` belongs to the poll thread.
- `ProfilerModel` is UI-thread state. It is cleared on `CloseData`, on
  `AdoptNewEmulatorInstance`, by the panel's Clear button, and by a `seq` that goes
  backwards (a new emulator run).

## Extension points and constraints

| Task | Start here; preserve |
|---|---|
| Add a counter | Append to `SePerfCounters.h` (never reorder), add the increment as an anchored insertion in `apply.py`, read it in `ComputePerfShares` / the panel. Older clients ignore surplus counters; older servers leave new ones zero |
| Add an emulator | Fill an array in `SePerfCounters.h` order and call `SeExportPublishPerf` once per frame; register `SeExportSetProfilerHook` only if a per-instruction hook exists |
| Change a record | Each states its own length: append fields, keep the existing order |
| Inclusive (caller) time, per-scanline timelines | Not implemented; see Coverage limits |

## Testing

```sh
ctest --test-dir build --output-on-failure -R 'ProfilerModel|ProfilerPanel|LiveProfiler|MednafenRoundTrip'
```

- [ProfilerModelTests.cpp](../../../FrontEnd/tests/ProfilerModelTests.cpp): share arithmetic
  and clamping, bus grouping, history window and de-duplication, function totals, pool
  classification.
- [ProfilerPanelTests.cpp](../../../FrontEnd/tests/ProfilerPanelTests.cpp): the panel drawn
  headlessly; drawn figures, tab-driven data requests, disabled attribution, row
  double-click through a row with a hoverable bar.
- [LiveProfilerTests.cpp](../../../FrontEnd/tests/LiveProfilerTests.cpp): real exporter and
  real driver; every frame once and intact, `PRF` reaching the emulate-thread hook, and
  attribution dropped on disconnect.
- [MemoryRoundTripTests.cpp](../../../FrontEnd/tests/MemoryRoundTripTests.cpp) (profiler
  section): the real glue; counters handed over and zeroed per frame, and
  `SeMdfnProfSample` charging cycles to a recorded function, to a raw-PC bucket, per CPU,
  less sleep, and not across a timestamp jump.
- The panel was also run in the native frontend against a stand-in emulator (the real
  export server fed synthetic counters) and inspected on all four tabs.

## Coverage limits

- The patched Mednafen 1.32.1 was **compiled and linked** with these hooks
  (`--enable-debugger`); it was not run with a game, so the magnitudes of the counters
  have not been compared with known workloads. Verify on a real title before relying on
  absolute numbers.
- "Busy" includes time an SH-2 spends waiting on slow bus cycles; only SLEEP/standby and
  DMA-burst/external-halt stalls are separated out.
- Bus accesses are the ones the emulator models as reaching the external bus: cache hits
  and fast-map instruction fetches are not counted.
- DMA bytes are the counts transfers were *started* with; an aborted transfer is counted
  in full.
- Function time is self time only, and the raw-PC buckets are not folded into functions
  (`ProfilerPanel::Host::functionContaining` is an unfilled hook for the function finder).
- VDP2 has no time figure: its raster is fixed-length.

## Related documentation

[Knowledge-base index](../README.md), [State sources](StateSources.md),
[Debugger](Debugger.md), [Mednafen integration](../../../Integration/Mednafen/README.md).
