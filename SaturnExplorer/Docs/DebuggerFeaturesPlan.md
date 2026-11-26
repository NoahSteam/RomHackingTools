# Debugger features plan: hardware event log, struct views, inline SH-2 assembler

Status: plan, nothing implemented. Order of work: (1) hardware event log, (2) struct views, (3) inline
assembler. Each feature is split into tasks that own disjoint files so two can be implemented at once; one
integration task per feature owns `App.*`, the FrontEnd source lists in `CMakeLists.txt`, `FEATURE_MAP.md`
and the web guide. Every task is reviewed before it is merged, and each push is reviewed again on the pull
request. Both parts were written from a read of the code at `48408e8`; nothing in them was built or run, and
each ends with what could not be determined.

Decisions taken on top of the two parts below:

- Protocol: the event section is opt-in per connection and the protocol stays at v27 (part 1, D1). A version
  bump would make every shipped v27 application refuse a new emulator.
- `Debug/WriteDestText.h` (shared by struct views and the assembler) is created by struct-views task 1A.
- The silent loss of tracepoint events (`sEvDropped` is counted and never reported) is out of scope here and
  recorded in `KNOWN_LIMITATIONS.md` by the event-log integration task.

---

# Part 1. Hardware event log

Read-only investigation; nothing was built or run. All paths are under `/home/user/RomHackingTools/SaturnExplorer/`.

## 1. Existing mechanisms this builds on

**Tracepoint event path (do not reuse).**
- Queue: `Integration/Common/se_export.c:380-388` is a single global 128-entry FIFO (`sEvQ`) of 104-byte records, filled by `SeExportQueueTraceEvent` (`:1763`) under `SE_LOCK`, newest dropped on overflow.
- Loss is silent today: `sEvDropped` is incremented at `:1772` and never read anywhere, despite the "reported once" comment at `:388`.
- Drain is destructive and global (`se_export.c:2830-2857`, cap `SE_LIVE_EVENTS_MAX` 64 per reply). With two connections (local socket plus TCP) whichever asks first takes the events.
- There are no sequence numbers. The driver parses the block at `Drivers/Live/src/LiveDriver.cpp:1179-1196`; the App drains it in `App::DrainTraceEvents` (`FrontEnd/src/App.cpp:3502`).

**Perf path (the model to copy).**
- Ring with 64-bit sequence: `se_export.c:1096-1100`.
- Per-connection cursor `SeExportPerfCursor` (`se_export.h:73`) and `SeExportBuildPerfSection` (`se_export.c:2073-2127`): exact lost range, `newestSeq`, a fresh connection starts at the newest record, cap `SE_LIVE_PERF_MAX_PER_REPLY`.
- Per-connection request slots (F1): `sProfSlotUsed/Flags`, `SeProfAcquire/SetRequest/Release` (`se_export.c:1086-1164`); verb handler at `:2577`.
- Frame-boundary application: `SeApplyPendingInstalls`, gated by `sProfAtBoundary` (`se_export.c:1409-1425`), reached from `SeExportApplyInstalls` (`:1523`) via `SeMednafenEndFrameHook` (`se_mednafen_glue.c:1220`) and from the paused frame gate (`:1570`).
- Client side: `profDirty` is sent on change (`LiveDriver.cpp:1657`) and restated on every new connection (`:1877`); the App restates the wanted value every frame (`App.cpp:4429-4431`).

**Version rules.**
- The client refuses any server with `version > SE_LIVE_VERSION` (`LiveDriver.cpp:1115`).
- Trailing blocks are read strictly by version, and an unknown verb falls through to GET (`SeLiveProtocol.h:358-364`).
- Precedent for adding without a bump: `snapshot_timeline` (`SeLiveProtocol.h:538-554`, "No version bump on purpose").
- Capabilities: control block +52, assembled at `se_export.c:2796-2806`.

**Patch machinery.**
- `apply.py` uses `fence`/`upsert`/`apply_anchored` (`:57`, `:1438-1456`); insert-only fences are identified by a key substring.
- `--revert` deletes fences over a fixed file list (`:1978-1982`), which already covers every file needed here.
- `COMMON_FILES` (`:50`) lists the shared headers copied into the tree; `RETIRED_FENCES` (`:1605`) handles removed hooks.
- An existing fence keyed X is left alone if unchanged, so new hooks must be new fences with new keys, never edits to profiler fences.

## 2. Design decisions

### D1. Transport: a new opt-in trailing section, no version bump (stay v27)

- **New ring, not the trace queue.** Hardware events get their own ring in `se_export.c`, modelled on the perf ring: 64-bit sequence, per-connection cursor, loss reported exactly.
- **Why not the trace queue.** Its record is 104 bytes of SH-2 registers with no kind field, its drain is destructive across connections, its loss is silent, and its layout is version-pinned.
- **Why not the perf section.** Its record and header are frozen at v27 (`SeLiveProtocol.h:394`).
- **New verb `HWE\n`.** `arg` is the `SE_HWEV_EN_*` enable mask; no payload; per connection, like PRF.
- **Section presence is per connection and sticky.** From the reply to a connection's first `HWE` onward, every reply on that connection ends with the hardware-event section, after the perf section. A connection that never sends `HWE` gets byte-identical v27 replies.
- **New capability bit `SE_LIVE_CAP_HW_EVENTS = 1024u`.** Set from the first reply when the glue registered its enable hook.
- **Compatibility.**

| Client | Server | Result |
|---|---|---|
| Old v27 | New | Never sends `HWE`, never sees the section, unaffected. |
| New | Old v27 or older | Cap bit clear, so the driver must not send `HWE`. An old server would answer as GET with no section and the client would block waiting for it. |
| New | New | Section flows after the first `HWE`. |

- **Driver gate.** The cap check goes in the driver at the single send point (next to `MinVerFor`, `LiveDriver.cpp:1763`), not only in the UI. `MinVerFor(HWE)` = 27 as a second guard.
- **Rejected alternative: bump to v28.** It is cleaner by convention, but every shipped v27 app would refuse the new emulator (`LiveDriver.cpp:1115`). Choose it only if emulator and app are always shipped together; the record and section definitions below are unchanged either way.
- **Yabause.** It shares `se_export.c`; with no hook registered the cap is clear and the verb only records a mask nobody applies.

### D2. Volume control

- **Enable mask.** The server keeps a per-connection mask in the existing per-connection slot index (`profSlot`), ORs the masks under `SE_LOCK`, and sets a pending flag. The emulate thread applies it only at a frame boundary, in the same `sProfAtBoundary` block as the profiler mode, by calling the glue's registered `set(mask)`.
- **Disconnect.** Releasing the slot recomputes the union, so a departed client cannot leave events on.
- **Zero cost when off.** One global `unsigned int SeSsHwEvMask`, defined in the glue next to `SeSsPerfC`, C linkage, written only on the emulate thread. Every site is `if(MDFN_UNLIKELY(SeSsHwEvMask & SE_HWEV_EN_x)) SeMednafenHwEvent(...)`.
- **No debug-loop dependency.** No site needs the per-instruction hook, so events work in non-debugger builds at full speed.
- **Bounded ring.** `SE_HWEV_RING` = 8192 records × 32 bytes = 256 KiB static. The producer never blocks and overwrites the oldest; loss is per connection (cursor fell out of the ring) and reported as an exact count in the section header.
- **Per-reply cap.** `SE_LIVE_HWEV_MAX_PER_REPLY` = 512 (16 KiB). Deferred records are not lost.
- **Per-frame cap.** Lives in the glue: default 64 records per source per frame (compile-time constant in v1). Excess is counted, and at frame end (in `SeMednafenEndFrameHook`, before `SeExportApplyInstalls`) one `SE_HWEV_K_SUPPRESSED` record per overflowing source is queued, rendered as "... +N more this frame".
- **Driver queue.** Bounded at 16384, evictions counted like `perfDriverEvicted`.
- **Panel reporting.** A row "N hardware events lost (emulator ring / client queue)" is inserted at the position of the gap.

### D3. Record format (fixed 32 bytes, little-endian)

New header `Integration/Common/SeHwEvents.h`, pure C macros plus one struct, included by exporter, glue, emulator TUs, driver and front end:

```c
typedef struct SeHwEvent {            /* wire order, no padding */
    unsigned char kind;               /* SE_HWEV_K_* */
    unsigned char source;             /* bit index of the SE_HWEV_EN_* that produced it (0..7) */
    unsigned char cpu;                /* 0 master, 1 slave, 0xFF none */
    unsigned char flags;              /* SE_HWEV_F_HAS_PC 1, SE_HWEV_F_PC_APPROX 2 */
    unsigned int  frame;              /* stamped by the exporter from sFrameNo (as se_export.c:1777) */
    unsigned int  ts;                 /* master SH-2 clock, frame-relative (SsDbgCpuTimestamp(0)) */
    unsigned int  pc;                 /* valid with HAS_PC */
    unsigned int  arg[4];
} SeHwEvent;                          /* SE_LIVE_HWEV_REC_LEN 32 */
```

Enable bits (`HWE` arg; `source` = bit index):

| Bit | Name | Covers |
|---|---|---|
| 0x01 | `CPU_EXC` | SH-2 interrupts and exceptions taken |
| 0x02 | `SCU_INT` | SCU interrupt delivered, mask writes |
| 0x04 | `SCU_DMA` | SCU DMA start, end, illegal |
| 0x08 | `SH2_DMAC` | on-chip DMAC start, end |
| 0x10 | `VDP1` | draw start/end, frame-buffer swap/erase |
| 0x20 | `VDP2` | VBlank IN/OUT, display on/off |
| 0x40 | `CD` | CD block command and response |
| 0x80 | `SMPC` | SMPC command |

Kinds and arguments:

| Kind | cpu / pc | arg0 | arg1 | arg2 | arg3 |
|---|---|---|---|---|---|
| 1 `SH2_EXCEPTION` | cpu, saved PC (exact) | vector | class `SE_HWEV_EXC_{INT,NMI,CPUADDR,DMAADDR,ILLINSTR,ILLSLOT,TRAP}` | level (INT only) | handler PC |
| 2 `SCU_INT_ACCEPT` | none | source bit 0..31 | level | vector | IPending after |
| 3 `SCU_INT_MASK` | none | new IMask | old IMask | | |
| 4 `SCU_DMA_START` | none | level \| indirect<<8 \| startFactor<<16 \| readBus<<24 \| writeBus<<28 | src | dst | bytes |
| 5 `SCU_DMA_END` | none | level | | | |
| 6 `SCU_DMA_ILLEGAL` | none | level | src | | |
| 7 `DMAC_START` | cpu, PC approx | ch \| CHCR<<8 | SAR | DAR | TCR |
| 8 `DMAC_END` | cpu | ch | | | |
| 9 `CD_COMMAND` | none | CR1 (command = hi byte) | CR2 | CR3 | CR4 |
| 10 `CD_RESPONSE` | none | Results[0] (status = hi byte) | [1] | [2] | [3] |
| 11 `SMPC_COMMAND` | none | command | IREG0..3 | IREG4..6 | |
| 12 `VDP1_DRAW_START` | none | draw FB | 1 if previous draw interrupted | | |
| 13 `VDP1_DRAW_END` | none | last command addr | reason (0 END bit, 1 aborted by swap) | | |
| 14 `VDP1_FB_SWAP` | none | displayed FB | FBCR | TVMR | |
| 15 `VDP1_FB_ERASE` | none | FB | EWDR | EWLR | EWRR |
| 16 `VDP2_VBLANK` | none | 1 IN / 0 OUT | | | |
| 17 `VDP2_DISPLAY` | none | 1 on / 0 off | TVMD value | | |
| 255 `SUPPRESSED` | none | count suppressed | cap | | |

- Text and names (vector names such as 0x40 "VBlank-IN", CD command names, SMPC command names, bus route names) exist only in the front end.
- Unknown kinds render as `kind 0x.. a0 a1 a2 a3`, so a newer emulator degrades gracefully.

Section layout (after the perf section, only on opted-in connections):

```
u32 count        (<= SE_LIVE_HWEV_MAX_PER_REPLY)
u32 recordLen    (32; a client reads recordLen bytes per record and ignores what it does not know;
                  < 32 or > 256 = broken stream)
u64 firstSeq     sequence of the first record (records are consecutive); with count 0, the next
                 sequence owed
u64 newestSeq
u64 lost         records the ring dropped for THIS connection immediately before firstSeq
u32 appliedMask  the mask the emulate thread is running with (the UI shows "pending" until it matches)
u32 supportedMask  SE_HWEV_EN_* bits this build has hooks for
then count records                                          header = SE_LIVE_HWEV_HDR_LEN 40
```

### D4. Interfaces between tasks

**Exporter (`se_export.h`), implemented by task A, called by glue (task B):**

```c
void SeExportSetHwEventHook(void (*set)(unsigned int mask), unsigned int supportedMask);
     /* registered before SeExportStart, like SeExportSetProfilerHook; sets SE_LIVE_CAP_HW_EVENTS */
void SeExportQueueHwEvent(const SeHwEvent* ev);   /* emulate thread; 'frame' is overwritten here; no-op
                                                     when !SeExportHasClient() */
unsigned int SeExportHwEventMask(void);           /* applied mask (tests) */
typedef struct SeExportHwCursor { unsigned long long sent; int started; } SeExportHwCursor;
size_t SeExportBuildHwSection(SeExportHwCursor* c, unsigned char* out);   /* testable like BuildPerfSection */
```

**Glue (`se_mednafen_glue.c`), implemented by B, called by apply.py hooks:**

```c
unsigned int SeSsHwEvMask;                                  /* the single flag every site tests */
void SeMednafenHwEvent(unsigned kind, int cpu, unsigned pc, unsigned a0, unsigned a1, unsigned a2, unsigned a3);
     /* derives source and flags from kind, reads ts, applies the per-frame cap, calls the sink */
void SeMednafenDmacRecalc(int cpu, unsigned runMask, unsigned ch0chcr, unsigned sar0, unsigned dar0, unsigned tcr0,
                          unsigned ch1chcr, unsigned sar1, unsigned dar1, unsigned tcr1);
     /* keeps a per-CPU shadow of the run conditions; rising bit = DMAC_START, falling = DMAC_END */
```

- The glue calls the exporter through a macro: `#ifndef SE_MDFN_HWEV_SINK` / `#define SE_MDFN_HWEV_SINK SeExportQueueHwEvent`. B's unit test defines its own sink before `#include "se_mednafen_glue.c"` (the `ProfilerLifecycleTests.c:12` pattern), so B does not need A's implementation.
- `ts` comes from `SsDbgCpuTimestamp(0)` when `SE_MEDNAFEN_WIRED`, else a test-settable static.

**Driver (`LiveDriver.h`), implemented by A, called by C:**

```c
typedef struct se_live_hw_event { uint64_t seq; uint64_t lost_before; uint32_t frame, ts, pc;
                                  uint8_t kind, source, cpu, flags; uint32_t arg[4]; } se_live_hw_event;
void     se_live_set_hw_events(const se_data_source*, uint32_t mask);   /* restate every frame; sent on change */
uint32_t se_live_drain_hw_events(const se_data_source*, se_live_hw_event* out, uint32_t max);
typedef struct se_live_hw_status { uint64_t newest_seq, server_lost, driver_evicted;
                                   uint32_t applied_mask, supported_mask; } se_live_hw_status;
int      se_live_hw_status_get(const se_data_source*, se_live_hw_status* out);
```

- `lost_before` is the server loss plus driver eviction immediately preceding this record, so the panel places the "lost" row correctly.
- The driver sends `HWE` only if `srvCaps & SE_LIVE_CAP_HW_EVENTS`.
- On a new connection it sets `hwDirty` only if the wanted mask is non-zero, so sessions that never use the feature never opt in.
- A connection-local `hwSection` flag is set when `HWE` is shipped and passed to `ReadSnapshot`.
- The queue and counters reset on `firstOnConnection`, as for perf (`LiveDriver.cpp:1886-1895`).

### D5. Hook sites (all insert-only fences, new keys)

All anchors were read in `_emu/mednafen/src/ss`.

| File | Site | Events | Notes |
|---|---|---|---|
| `sh7095_ops.inc` | After each `Exception(EXCEPTION_x, ...)`, same anchors as `sh2_exception_edit` (`apply.py:1203`) | `SH2_EXCEPTION` | New keys `SE_HWEV_EXC_x`. Saved PC is exact: `PC -= 4` precedes the macro (`sh7095_ops.inc:1930-1965`). Level from `opexec_ipr`, handler from `opexec_new_PC`. |
| `sh7095_ops.inc` | TRAP, line 1842 | `SH2_EXCEPTION` | Needs its own anchor; that line has no `;`, so the existing regex does not match it. |
| `scu.inc` | `StartDMATransfer`, after `d->ReadFunc = rftab[rb];` (existing `SCU_DMA_PERF_ANCHOR`) | `SCU_DMA_START` | Uses `d - DMALevel`, `ra`, `wa`, `bc`, `rb`, `wb`, `d->Indirect`, `d->SF`. |
| `scu.inc` | `SCU_DoDMAEnd` (`:1679`), after `DMALevel[level].Active = false;` | `SCU_DMA_END` | |
| `scu.inc` | `rb == -1` branch (`:1345`), optional | `SCU_DMA_ILLEGAL` | |
| `scu.inc` | `CheckDoMasterInt`, after `IPending &= ~(1U << bpos);` (`:238`) | `SCU_INT_ACCEPT` | Delivered, not raised: `SetInt` fires per HBlank line and would flood. |
| `scu.inc` | IMS write, `case 0xA0` (`:506-507`) | `SCU_INT_MASK` | Old `IMask` is still intact after the `SS_DBGTI` line. |
| `scu.inc` | `SCU_SetHBVB`, after `const bool VB_End` (`:361`) | `VDP2_VBLANK` | On `VB_FromVDP2 ^ new_VB_FromVDP2`. Called per H phase, so the mask test comes first. |
| `sh7095.inc` | Top of `DMA_RecalcRunning` (`:781`), after `DMA_PenaltyKludgeAmount = 0;` | `DMAC_START` / `DMAC_END` | Called after every CHCR/DMAOR write (`:1362`, `:1375`) and on finish (`:650`). A shadow in the glue turns levels into edges. |
| `cdb.cpp` | After `CTR.Command = CTR.CD[0] >> 8;` (`:2587`) | `CD_COMMAND` | Inside a `switch`/`__COUNTER__` coroutine: calls only, no declarations. |
| `cdb.cpp` | End of `BasicResults` (`:1872`) | `CD_RESPONSE` | Needs a prepended declaration fence in `cdb.cpp`. |
| `smpc.cpp` | After `OREG[0x1F] = ExecutingCommand;` (`:1376`) | `SMPC_COMMAND` | INTBACK continuations are not logged. |
| `vdp1.cpp` | `StartDrawing` (existing `VDP1_PERF_START_ANCHOR`) | `VDP1_DRAW_START` | |
| `vdp1.cpp` | After `SS_DBGTI(... "Drawing finished at 0x%05x" ...)` (`:804`) | `VDP1_DRAW_END` reason 0 | |
| `vdp1.cpp` | Existing `VDP1_PERF_ABORT_ANCHOR` | `VDP1_DRAW_END` reason 1 | |
| `vdp1.cpp` | After `SS_DBGTI(... "Displayed framebuffer changed to %d." ...)` (`:1017`) | `VDP1_FB_SWAP` | |
| `vdp1.cpp` | After `SS_DBGTI(... "VB erase start of framebuffer %d." ...)` (`:941`) | `VDP1_FB_ERASE` | |
| `vdp2.cpp` | TVMD write `case 0x00`, before `DisplayOn = (V >> 15) & 0x1;` (`:626`) | `VDP2_DISPLAY` | Only when the bit changes; `DisplayOn` still holds the old value at that point. |
| `ss.cpp` | Accessor block | none | Add `SsDbgCpuPc(cpu)` using the RPC-4 / PC_ID rule of `SsDbgSh2Regs` (`apply.py:150-170`), for DMAC events. |
| `apply.py` lists | `COMMON_FILES`, `FWD_DECLS` | none | Add `SeHwEvents.h`; declare `SeSsHwEvMask`, `SeMednafenHwEvent`, `SeMednafenDmacRecalc` for each TU. |

- Two fences on one anchor are fine: the newer lands directly after the anchor and each is found by its own key.
- `SeMednafenStateLoaded` must reset per-frame cap counters and mark the DMAC shadow unknown. The first recalc after "unknown" seeds silently.

### D6. Front end

**Chips.**
- CPU/DMA/VDP/CD (plus a new `LogCategory::SMPC`) become "capture and show" toggles, default off. Precedent: the Input chip (`LogPanel.cpp:100`).
- "All" changes visibility only and never arms capture. Chip tooltips state the cost.
- Right-click on a chip offers sub-sources: CPU = exceptions / SCU interrupts (default off); DMA = SCU / DMAC; VDP = VDP1 / VDP2.
- `uint32_t LogPanel::HwRequestMask() const` is the union of armed sub-sources.

**App wiring.**
- A new `App::DrainHwEvents()` is called next to `DrainTraceEvents` (`App.cpp:1619`).
- It restates `se_live_set_hw_events(mask)` every frame (pattern at `App.cpp:4429`), with mask 0 when not live or the cap is absent.
- It drains, formats through a new pure module `FrontEnd/src/HwEventFormat.{h,cpp}` (`se_live_hw_event` → `LogEvent`), and pushes.
- `hasSource`/`cpu`/`pc` are set when `SE_HWEV_F_HAS_PC`. The existing Jump to Assembly/Memory in the detail pane then works unchanged. Detail rows carry the decoded arguments and "PC approximate" when flagged.

**State handling.**

| Situation | Behaviour |
|---|---|
| Non-live source, or server without `SE_LIVE_CAP_HW_EVENTS` | Hardware chips drawn disabled with a tooltip. The stored preference is untouched and takes effect on the next capable connection. |
| Persistence | `Settings::GetBool/SetBool` (`Settings.h:79-80`), section `Log`, one key per sub-source, saved on change. |
| History / scrub | Events are live-only and not stored in recorded frames. Rows keep the frame they happened on. On a restore (rewind, slot load) an Info row "state restored; frame numbers continue from N" is pushed. |
| Halts / steps | The section rides every reply, so events from instruction steps appear as they happen. A chip toggled during a mid-frame halt shows "(pending)" until `applied_mask` matches; it applies at the next frame boundary or paused frame gate. |

**Panel scaling.** `mCap` is 5000 and the table walks every event each frame with no clipper (`LogPanel.cpp:161`). Hundreds of events per frame would evict tracepoint and Info rows within seconds and cost draw time. Task C must:
- raise the cap (about 50000);
- keep a filtered index list rebuilt on push or filter change and draw with `ImGuiListClipper`;
- take one `NowString()` per drain batch, not per event.

## 3. Tests

**Glue unit: `Integration/tests/HwEventGlueTests.c`** (includes the glue, custom sink)
- Mask 0 produces nothing.
- Kind → source/flags mapping.
- Per-frame cap and the SUPPRESSED record at frame end.
- DMAC shadow: rising, falling, unknown after state load.
- Counters reset on `SeMednafenStateLoaded`.

**Exporter + driver lockstep: `FrontEnd/tests/LiveHwEventTests.cpp`** (real `se_export.c` with `SE_EXPORT_TEST_HOOKS`, real LiveDriver, style of `LivePerfLossTests` / `LiveProfilerRequestTests`)
- A non-opted connection's replies are byte-identical in length to v27.
- Opt-in on the `HWE` reply.
- Mask applied only at `SeExportApplyInstalls`, not `SeExportApplyInstallsMidFrame`.
- Union of two clients; release on disconnect.
- Exact ordering and sequences across the per-reply cap.
- Ring overrun reports an exact `lost`; driver eviction is counted.
- Fresh connection gets no backlog.
- Against `FakeLiveServer`: a server without the cap never receives `HWE`; the mask is restated after reconnect; a `recordLen` of 40 is tolerated; a bad count or length drops the connection.

**Format: `FrontEnd/tests/HwEventFormatTests.cpp`**
- Golden strings for every kind, names tables, unknown kind fallback.

**Panel: extend `FrontEnd/tests/LogPanelTests.cpp`** (ImGuiHarness)
- Chip click arms the mask; "All" does not.
- Chips are disabled without the capability.
- "lost" and "+N more" rows render.
- A PC row exposes Jump to Assembly.
- The clipper still opens the detail pane.

**Real patched source**
- `Integration/tests/check_apply_hooks.py`: each new key present once and behind a `SeSsHwEvMask` test.
- `Integration/Mednafen/check_component.py`: new harness `component/hwev_harness.cpp` driving the real `vdp1.cpp` (draw start/end/swap) and the real SH7095 (exception with vector/level/saved PC, DMAC start/end), plus `--drop` for every `SE_HWEV_*` key and mutations `hwev-mask-ignored` and `hwev-wrong-kind`. Each must be detected.
- `Integration/Mednafen/check_migration.py`: upgrade from the current HEAD `apply.py`; no anchor miss, each hook once, third apply is a no-op, `--revert` byte-exact, with and without `--with-pause`.
- `Integration/tests/check_scratch_includes.py`: knows `SeHwEvents.h`.

**V1 guest**
- Addendum `Integration/Validation/V1/EXPECTED_EVENTS.md` with per-phase expected events: SCU DMA level 0 with VBlank-OUT start factor in phases 6-7, one VDP1 draw start/end per frame in phase 4, SMPC SSHON/SSHOFF in phases 9-10, one VBlank IN/OUT pair per frame.
- A CHECKLIST entry, and what dropping each hook must change.

## 4. Task split

**T0. Interface commit (first, small, one agent)**
- New `Integration/Common/SeHwEvents.h`.
- `SeLiveProtocol.h`: verb, cap bit, section doc, limits.
- Prototypes in `se_export.h` and `LiveDriver.h`.
- Compiling no-op bodies in `se_export.c` and `LiveDriver.cpp`.
- Acceptance: the full existing ctest suite passes unchanged.

**A. Protocol, exporter, driver** (after T0, parallel with B and C)
- Files: `Integration/Common/se_export.c`, `se_export.h` (comments only), `Drivers/Live/src/LiveDriver.cpp`, `FrontEnd/tests/FakeLiveServer.h`, new `FrontEnd/tests/LiveHwEventTests.cpp`, its CMake block (near `CMakeLists.txt:1869`).
- Acceptance: the lockstep tests above; a ThreadSanitizer run as described in Profiler.md "Race-detector run".

**B. Emulator hooks and glue** (after T0, parallel)
- Files: `Integration/Mednafen/apply.py`, `se_mednafen_glue.c`, `check_component.py`, `component/hwev_harness.cpp`, `check_migration.py`, `Integration/tests/check_apply_hooks.py`, `check_scratch_includes.py`, new `Integration/tests/HwEventGlueTests.c`, its CMake block (near `:1562`), `Integration/Mednafen/README.md`.
- Acceptance: the glue tests, component checks with all drops and mutations detected, migration and revert checks, a patched tree that builds.

**C. Log panel and App** (after T0, parallel)
- Files: `FrontEnd/src/LogPanel.{h,cpp}`, new `HwEventFormat.{h,cpp}`, `App.{h,cpp}`, `FrontEnd/tests/LogPanelTests.cpp`, new `HwEventFormatTests.cpp`, CMake (near `:848`).
- C develops against T0's stub driver and synthetic `se_live_hw_event` values.
- Acceptance: the format and panel tests.

**D. Integration and docs** (after A, B and C)
- Real-emulator run on the V1 guest; `EXPECTED_EVENTS.md`.
- New `Docs/Codebase/Systems/HardwareEvents.md`, a `FEATURE_MAP.md` entry, a `Docs/EXECUTION_ACTIONS.md` update ("DMA/VDP/CD categories light up").
- Optionally a `--hw-events` flag on the V3 bench client for a CSV dump.

`CMakeLists.txt` is the only file all three parallel tasks touch; each adds one block in a different region.

## 5. Risks

- **Conditional section presence** is new to this protocol. A driver bug in the cap gate would hang against old servers. This is pinned by the FakeLiveServer tests; v28 is the fallback.
- **Exception hook in the slave's resumable copy** (`sh7095s_rsu.inc`). I did not verify that `opexec_ipr` and `opexec_new_PC` are nameable at the hook point there; the component compile settles it.
- **Coroutine bodies** in `cdb.cpp` and `smpc.cpp`: hooks must be plain calls with no local declarations.
- **Timestamps** use the master SH-2 clock. Device events (CD, VDP1) can be off by the scheduler's slack; document this and do not assert exact `ts` in tests.
- **Device events carry no PC or CPU.** No cheap "currently executing CPU" exists in the fast loop; `SeSsExecCpu` is debugger-only.
- **Interrupt floods.** A game using HBlank interrupts yields over 224 exceptions per frame; the per-frame cap and SUPPRESSED row handle it, but the cap of 64 is a guess.
- **`SE_LOCK` per event** on the emulate thread is acceptable at hundreds per frame. If V3 shows cost, batch per frame in the glue.

## 6. Out of scope

- Fixing the silent trace-event drop (`sEvDropped`) or moving tracepoints to the new ring.
- Yabause hooks.
- Events in recorded history, scrub or savestate sources.
- Per-instruction or bus-access events, SCSP/68000 events, the SCU "interrupt raised" edge.
- Halting on an event (break-on-interrupt).
- A user-configurable per-frame cap.
- PC attribution for device events.

## 7. Not determined

- `Integration/Yabause/apply.py` `COPY_FILES` (`:41`) omits `SePerfCounters.h` although `se_export.c` includes it. I could not tell whether that path currently builds; a new header has the same exposure.
- I did not read the V1 guest program (`v1_program.py`), so the per-phase event expectations are inferred from the `EXPECTED.md` headings.
- I did not check whether `BasicResults` covers every CD response path.
- `FEATURE_MAP.md` and `StateSources.md` / `Debugger.md` were not read in detail; the Settings key naming convention was not checked beyond `GetBool/SetBool`.

### Critical Files for Implementation
- /home/user/RomHackingTools/SaturnExplorer/Integration/Common/se_export.c
- /home/user/RomHackingTools/SaturnExplorer/Integration/Common/SeLiveProtocol.h
- /home/user/RomHackingTools/SaturnExplorer/Integration/Mednafen/apply.py
- /home/user/RomHackingTools/SaturnExplorer/Integration/Mednafen/se_mednafen_glue.c
- /home/user/RomHackingTools/SaturnExplorer/Drivers/Live/src/LiveDriver.cpp
- /home/user/RomHackingTools/SaturnExplorer/FrontEnd/src/LogPanel.cpp

---

# Part 2. Struct views and inline SH-2 assembler

Read-only planning against HEAD `48408e8`. Nothing was built or run; the opcode round-trip claim in Feature 2 comes from reading the decoder, and the exhaustive test is what proves it. Paths are relative to `/home/user/RomHackingTools/SaturnExplorer/`.

## Shared facts both plans build on

- **One write path.** `IMemoryBackend::CanWrite / WriteMemory / WriteRefusal / WriteDestination` (`FrontEnd/src/Debug/MemoryBackend.h:56-76`). `ContextBackend::WriteMemory` re-checks policy at commit time and calls `se_write_vram` (`MemoryBackend.cpp:199-244`).
- **Live/staged/snapshot rules are set once per frame by App** (`App.cpp:1909-1932`: `SetReadOnly`, `SetVdpWindowsWritable`, `SetRegistersReadOnly`, `SetEditSink`). A panel that only calls the backend inherits them, including staged scrub edits (`App::RecordPendingEdit`, `App.cpp:3041`).
- **Invalidation needs no new hook.** `Context::WriteVram` calls `RebuildDerived`, which does `++mDeriveSerial` (`Core/src/Context.h:608-641`, `:72-86`).
  - The serial feeds `App::BuildDecompilerView` (`App.cpp:3152`; scrub variant `:3164-3169`; live variant through `CaptureRepeatTracker`, which treats an in-place edit as a change, `CaptureRepeatTracker.h:16-20`).
  - It is part of the decompiler snapshot key (`Decompiler/MemorySnapshot.h:43`), so the panel recaptures and the function finder reruns on the new snapshot. This is all hex edits do today.
- **Patch-to-disc is automatic.** `ApplyChangesToDisc` re-reads memory at each `PatchLocation` and diffs it against `expected` (`App.cpp:6850-6856`, `PatchLibrary.h:25-35`). Any backend write inside a mapped range is picked up.
- **Scrub and history are free for reads.** `ContextBackend` holds `se_context**` and follows the scoped context swap (`MemoryBackend.h:79-80`, `App.cpp:1934`).
- **In-flight edits must drop when `backend.SourceId()` changes**, as the hex editor does (`HexEditorPanel.cpp:266-270`).
- **Build registration.** `CMakeLists.txt` lists FrontEnd sources explicitly in three targets (around lines 2136, 2232, 2350). The comment at `:2126-2128` says the MSBuild project globs `src/**`. C++14 is global (`:39`).
- **Shared-file hotspots:** `App.cpp`, `App.h`, `CMakeLists.txt`, `Docs/Codebase/FEATURE_MAP.md`, `web/guides/features.html`.

---

# Feature 1: Struct views

## Design decisions

1. **Dedicated "Structs" panel, not a tree inside Watch.**
   - Watch is a flat six-column table with fixed per-entry scratch (`WatchPanel.h:47-65`) and one read per entry (`WatchPanel.cpp:46-101`).
   - Its `WatchType` enum of eight is baked into the combo and the JSON (`WatchList.h:16-26`).
   - It has no value editing at all: there is no `WriteMemory` call in `WatchPanel.cpp`.
   - Tree, array table and editing would amount to a rewrite, so Watch gets only a "View as Struct…" row-menu hook.
2. **UI-free model beside the panel**, following CONVENTIONS "Structure and naming": `Debug/StructDefs.{h,cpp}`, with no ImGui and decoding from a byte buffer. The same decode can later run over a `MemSnapshot` or the decompiler's `MemorySnapshot`.
3. **Alignment: SH-2 natural alignment by default, with explicit overrides.**
   - Default: u16 aligns to 2; u32, fixed and pointers align to 4; a struct aligns to its largest member; tail padding to the struct's alignment.
   - Reason: pasted SGL/SBL headers must lay out as the Saturn compilers did.
   - Overrides: `@0xNN` per field, and `packed` after the struct name for hand-reversed layouts.
   - Export always writes `@offset` on every field and the total size, so export then import is exact regardless of alignment rules.
   - Import rejects overlapping or backward offsets and a declared size smaller than the layout.
4. **Persistence: one text file in the work folder, in the import/export format itself.**
   - File: `saturn_structs.txt` via `WorkFilePath` (`WorkFiles.h`), written with `WriteFileAtomically`, as the watch list is (`WatchPanel.cpp:385-393`).
   - Loaded and saved next to the other stores (`App.cpp:329-354`, `:426`).
   - It does not go in `.seproj`, which is `PatchLibrary::Serialize`'s own tab-separated format (`App.cpp:6915-6920`), nor in `settings.ini`, which is key/value.
   - `SaveStore`'s line merge (`AssemblyPanel.cpp:668-683`) is for address-keyed lines and does not fit block text. Keep its safety rule anyway: a file that failed to parse is never overwritten; hold the text, report the error, and refuse saves.
5. **Reads:** one batched read per visible instance at about 10 Hz, as Watch does.
   - Chunk at 64 KiB or less (`ReadOne` cap, `MemoryBackend.cpp:50`).
   - If the whole-struct read fails, fall back to per-field reads, because `ReadOne` requires the range to lie in one region. A struct straddling a region edge then shows per-field "Unavailable".
   - Scalars go through `FormatWatchValue` (`WatchList.h:86`) by slicing bytes, so pointer "suspicious" and "unavailable" semantics (`WatchList.h:71-77`) are reused.
6. **Edits:** `CanWrite` gates entering edit mode. Commit succeeds only if `WriteMemory(addr, be, size) == size`; otherwise show `WriteRefusal`, and say "partially written n/m" on a short count. The status text uses the hex editor's three destination strings (`HexEditorPanel.cpp:805-807`), put in a new small header `Debug/WriteDestText.h` so `HexEditorPanel.cpp` is not touched.

## Data model

```cpp
enum class FieldKind { U8,S8,U16,S16,U32,S32, Fixed16_16, Pointer, Struct, RGB555 };
enum class FieldFormat { Default, Hex, Dec, Fixed };
struct FieldDef { std::string name; FieldKind kind; std::string structName; // Struct, or Pointer target ("" = void*)
                  uint32_t count = 1; bool hasExplicitOffset = false; uint32_t offset = 0; FieldFormat fmt; };
struct StructDef { std::string name; bool packed = false; std::vector<FieldDef> fields; uint32_t size, align; };
struct StructView { uint64_t id; std::string label, expression, structName; uint32_t count = 1; };
class StructLibrary { Parse(text, errors) / Emit() / Find(name) / Layout(name) / Generation(); views; };
bool DecodeField(const FieldDef&, const uint8_t* bytes, StructValue& out);
bool EncodeField(const FieldDef&, FieldFormat, const std::string& text, uint8_t out[4], std::string& err);
```

- `Layout` rejects by-value recursion. Pointer cycles are fine.
- `Default` display follows Watch: unsigned as hex, signed as decimal, fixed as decimal with the raw hex in a tooltip.
- `EncodeField` for fixed accepts `1.5`, `-0.25` or a raw `0x00018000`.

## Text format (v1)

```
struct Actor packed? {            // size 0x40
    u8     state;
    u8     _pad[3];               // explicit padding is an ordinary field
    fixed  pos[3];                // 16.16 signed
    [[hex]] u16 flags @0x20;      // optional display attribute, optional explicit offset
    Vec3   vel;                   // nested by value (must be defined; order-independent)
    Actor* next;                  // pointer-to-struct, 4 bytes
    void*  userdata;
};
view player = Actor[1] @ 0x060A1000;   // persisted instances; not C, ignored by nothing else
```

- Types: `u8 s8 u16 s16 u32 s32 fixed`, plus single-token aliases `uint8_t…int32_t`, `Uint8…Sint32`, `FIXED`, and `ANGLE` as s16.
- Big-endian only. `//` and `/* */` comments are accepted.
- Bitfields (`:`), unions, enums, multi-token C types, typedefs and the preprocessor are rejected with a line-numbered error. `:` stays reserved for v2 bitfields, which is why display format uses `[[...]]`.

## UI

- **Window "Structs"**, docked with Watch (`App.cpp:2194`).
  - Toolbar: `+ View`, `Definitions…`, `Import`, `Export`, and a count or "(disconnected)". It mirrors Watch (`WatchPanel.cpp:127-141`). Import and Export use `IPlatform::OpenFileDialog` and `SaveFile` with `SaveOutcome` handling (`WatchPanel.cpp:330-368`).
- **Views table** with columns Field, Address, Type, Value and `...`.
  - Root rows hold editable label, expression and struct combo. The expression resolves through `IExpressionResolver` (`WatchList.h:40-47`).
  - Children are tree nodes for fields, array elements and nested structs.
  - A pointer-to-struct expands lazily to the target. Null shows "null", an unreadable target uses the Watch tooltips, depth is capped at 8, and a repeated address shows "(shown above)".
  - Only expanded, visible nodes are read. Change tint follows Watch.
- **Array of structs:** a view with `count > 1` gets a Tree/Table toggle.
  - Table rows are elements, drawn through `ImGuiListClipper`.
  - Columns are the flattened scalar leaves, capped at 64 with a note when truncated. Index and address columns are frozen.
- **Row menu:** Edit Value, Copy Address/Value, Break on Read/Write/Either via `bps.AddMemory(addr, size, kind)` as Watch does (`WatchPanel.cpp:303-309`), View in Memory via an out-param as Watch does (`WatchPanel.cpp:311-312`, `App.cpp:3097`), Open Pointer Target as New View, and Add to Watch for scalar leaves.
- **Definitions:** a struct list on the left, `InputTextMultiline` on the right, Apply and Revert, and a line-numbered error list. Text is the only editor; there is no form editor.
- **CLAUDE.md rules:** any whole-row selection goes through `RowSelectable`. A tree node spanning columns needs the overlap flag so value edit boxes still get clicks. Combos use `ComboWidth`.

## Integration points evaluated

| Point | Finding | Plan |
|---|---|---|
| History/scrub | Free through the backend. Edits on a scrubbed frame become Staged. | Drop the edit on a `SourceId` change. |
| Decompiler captured values | The inspector reads only the immutable matched snapshot, never `ContextBackend` (`CapturedValueInspection.h`). | Offer only "View as Struct" on the existing fixed-address route (`DecompilerPanel::Request::addWatch`, `DecompilerPanel.h:140-143`), as a stretch in task 1C. Struct rendering of captured snapshot values and feeding types to Ghidra are out of scope. |
| Memory Compare | Its `Request` already carries an address and size (`MemoryComparePanel.h:42-50`). | Stretch: a "View as Struct" action. Field-level A/B diff is out of scope, though buffer-based decode makes it possible later. |
| Big-endian helpers | `Core/src/ByteOrder.h` is vector-based, in namespace `se`, under `Core/src`. | Use local span readers in `StructDefs.cpp`, as `WatchList.cpp` does. I did not check whether FrontEnd targets can include `Core/src`. |

## Tasks

**1A: Model and text format (pure).**
- Owns: `FrontEnd/src/Debug/StructDefs.{h,cpp}`, `FrontEnd/src/Debug/WriteDestText.h`, `FrontEnd/tests/StructDefsTests.cpp`, plus its own test-target block in `CMakeLists.txt`.
- The first commit is the header alone, so 1B can start against it.
- Acceptance:
  - Layout matches hand-computed SH-2 offsets for mixed u8/u16/u32/nested/array cases, including `packed` and `@offset`.
  - Emit then Parse is the identity, and the emitted text is stable.
  - By-value recursion, unknown types, overlap, bitfields and unions are rejected with line numbers.
  - Encode and decode are covered for every kind and format, including fixed `-0.5` as `FFFF8000` and range errors.
  - Malformed text never yields a partial library.

**1B: Panel.**
- Owns: `FrontEnd/src/StructPanel.{h,cpp}`, `FrontEnd/tests/StructPanelTests.cpp`, plus its test-target block.
- The signature mirrors Watch: `Draw(IMemoryBackend&, IExpressionResolver&, IPlatform&, BreakpointManager&, float dt, uint32_t* outHexJump)`, with `Load`, `Save` and `RequestViewAt(addr, label)`.
- Acceptance, via `ImGuiHarness` with a mock backend as in `HexEditorPanelTests`:
  - A click reaches the value edit box inside a tree row and a table row.
  - Enter writes the exact big-endian bytes.
  - A refused write keeps the old value and shows the refusal text.
  - A short write is reported.
  - A `SourceId` change cancels the edit.
  - Pointer follow reads the target, and a cycle terminates.
  - A 10,000-element array reads only the visible rows.
  - Row height is unchanged while editing.

**1C: Integration (the only App owner; after 1A and 1B).**
- Owns: `App.h`, `App.cpp`, the three FrontEnd source lists in `CMakeLists.txt`, the Watch row-menu hook in `WatchPanel.{h,cpp}` (one out-param), docs and the web guide.
- App changes, about 25 lines:
  - `App.h`: include, `StructPanel mStructPanel;`, `Panels::structs`, `DrawStructs`.
  - `App.cpp`: one `PanelList` row (`:440`), a draw call (`:2015`), `DrawStructs` beside `DrawWatch` (`:3092`), load (`:329`), save (`:426`), dock (`:2194`).
- Acceptance: full build on all targets and `ctest` green; the panel toggle persists; "View in Memory" opens the hex editor; a watchpoint appears in Breakpoints; `saturn_structs.txt` survives a restart; a corrupt file is not overwritten.

## Docs

- `Docs/Codebase/FEATURE_MAP.md`: a new row under "Debug and understand game code".
- `Systems/Debugger.md`: sources table, a flow paragraph and an extension point.
- `Systems/MemoryAndHistory.md`: a new write consumer.
- `KNOWN_LIMITATIONS.md`: no bitfields.
- `web/guides/features.html`: a new card beside Watch (around `:449`) and the list at `:292`.

## Risks

- Tree rows with edit widgets hit the overlap trap in CLAUDE.md; cover with harness tests.
- Wide tables are slow; cap columns and clip rows.
- A staged edit batch is dropped when the user edits another frame (`StagedEdits` rule). That is the same as hex, but surprising with many field edits.
- On the web build the store is ephemeral, the same stopgap as watches (`WatchPanel.cpp:370-374`).

## Out of scope

Bitfields, enums, unions, a form-based definition editor, a C preprocessor, storing structs in `.seproj`, Ghidra type export, struct-aware compare diff, expression syntax beyond `IExpressionResolver`, and watchpoints on whole arrays or nested structs.

---

# Feature 2: Inline SH-2 assembler

## Design decisions

1. **Location:** `FrontEnd/src/Debug/Sh2Asm.{h,cpp}`, namespace `sfe`, pure C++14, beside `Sh2Disasm`. It has no ImGui or emulator dependency, so it builds for web.
2. **A shared table is not possible without rewriting the decoder.**
   - `Sh2Decode` is a nested `switch` with printf formats (`Sh2Disasm.cpp:71-294`); there is no table.
   - It is load-bearing for step-over, watchpoint cause analysis, the call stack and the SLEIGH oracle test, so v1 leaves it alone.
   - Drift is locked three ways instead:
     - The assembler has its own table of mnemonic, operand kinds, base opcode and field placement.
     - Every successful assemble is decoded back with `Sh2Decode` and compared field by field. A mismatch returns an internal error and never bytes.
     - The exhaustive test below covers all 65536 opcodes.
   - A single shared table is a follow-up refactor.
3. **Accepted syntax is exactly what the decoder prints, plus a few aliases.**
   - Case-insensitive; whitespace-tolerant; the "Copy Instruction" layout pastes back (`AssemblyPanel.cpp:533`).
   - `bt/s` and `bf/s` are what the decoder prints (`Sh2Disasm.cpp:248-249`); `bt.s` and `bf.s` are accepted as aliases.
   - `.word 0xNNNN` is accepted, which is how every illegal encoding round-trips.
   - `#imm` takes `0..0xFF` as raw bits, because the decoder prints the raw byte even for sign-extending `add`, `mov` and `cmp/eq` (`Sh2Disasm.cpp:234,242,291`). It also takes `-128..-1`. For sign-extending ops, `0x80..0xFF` adds a note such as "= -1".
   - Displacements are the scaled byte values the decoder prints. They must be a multiple of the scale and in range, and the error names the legal range.
   - `@(0x0,rN)` and `@rN` are different encodings and stay distinct.
4. **PC-relative operands.**
   - Both spellings assemble: absolute `@(0x0600ABCD),rN` and raw `@(0x1C,pc),rN`. The decoder prints the first normally and the second after a delayed branch (`Sh2Disasm.cpp:33-46`).
   - Absolute form: word `disp = EA − (addr+4)`, even, 0..510. Long and mova `disp = EA − ((addr+4)&~3)`, multiple of 4, 0..1020. Errors say backward, misaligned, or too far by N bytes.
   - Branches: `target − (addr+4)` must be even. `bra` and `bsr` reach −4096..+4094; `bt`, `bf`, `bt/s` and `bf/s` reach −256..+254. Arithmetic is modulo 2^32, as in the decoder.
   - The out-of-range error states the distance and limit. If the target is in range after folding mirrors, it adds a hint. It never auto-folds, because the printed address carries the instruction's mirror.
   - Targets and absolute PC-relative operands accept `symbol` or `symbol±n` through a resolver callback. App backs it with `FunctionNames::All()` (`CallStack.h:94`) and the default `sub_06XXXXXX` spelling. A duplicate name is an error listing the addresses.
5. **Delay-slot awareness is warnings, not errors.** The same row may also be reached directly, which is the ambiguity `Sh2Disasm.h:32-36` documents.
   - Writing a branch, `trapa`, or any PC-changing instruction right after a delayed branch warns as an illegal slot instruction.
   - Writing a PC-relative load or mova there with an absolute operand resolves as if reached directly, and warns with the in-slot address when the branch target is static (the logic of `Sh2DecodeAfterBranch`, `Sh2Disasm.cpp:303-318`).
   - Writing a delayed branch warns if the next word decodes to an illegal slot instruction.
   - Replacing a delayed branch with a non-delayed instruction notes that the following instruction is no longer a slot.
6. **Literal pool: refuse allocation, allow editing the existing slot.**
   - `mov.l #imm32,rN`, or a `mov #imm` beyond 8 bits, is refused with: "needs a literal-pool slot; SH-2 loads 32-bit constants with mov.l @(disp,pc),rN — use Edit Literal on that row or type the @(addr) form".
   - Rows with an unambiguous `PcRel` Word or Long get "Edit Literal Value…", which writes the 2 or 4 bytes at the effective address through the same path.
   - Before commit, scan the instructions that can reach that slot (510 or 1020 bytes back) and show "also loaded by N other instructions (first at …)", since pool words are shared.
7. **The write happens in the panel, not in App.** `AssemblyPanel::Draw` already receives the backend (`AssemblyPanel.cpp:159`).
   - `CanWrite(addr)` gates the action; when disabled, the tooltip shows `WriteRefusal`.
   - Commit is one `WriteMemory(addr, {hi,lo}, 2)`, which is one poke or one staged record. A return other than 2 shows the refusal or partial text.
   - After a successful write, call `mPages.clear()`. With Auto Refresh off the page cache is only dropped on a source change (`AssemblyPanel.cpp:295`), and a backend write does not move `SourceId`.
   - Show the destination text from `Debug/WriteDestText.h`. This header is shared with Feature 1; whichever lands first creates it.

## API

```cpp
struct Sh2AsmContext { uint32_t address = 0;
    bool afterDelayedBranch = false; bool slotTargetKnown = false; uint32_t slotBranchTarget = 0;
    bool haveNext = false; uint16_t nextOpcode = 0;
    std::function<bool(const std::string&, uint32_t&, std::string& err)> resolveSymbol; };
struct Sh2AsmResult { bool ok = false; uint16_t opcode = 0; std::string error; size_t errorColumn = 0;
    std::vector<std::string> warnings; DisassembledInstruction decoded; };
Sh2AsmResult Sh2Assemble(const std::string& line, const Sh2AsmContext& ctx);
```

## UI (Assembly panel)

- **Entry points:** row-menu "Assemble…" and "Edit Literal Value…", double-click on the Instr or Bytes cell, and F2 on the selected row.
  - Existing gestures are unchanged: address double-click opens Memory (`:410-413`), operand click follows a branch (`:439`), comment double-click edits the note (`:490`).
  - The dead "Set PC Here" item stays as it is.
- **Edit box:** one `InputText` in the Operands cell, seeded with `Mnemonic + " " + Operands`. For slot rows that is already the `@(disp,pc)` form.
  - It uses zero vertical `FramePadding`, like the comment editor (`:465-472`). The clipper assumes one-line rows and `AssemblyPanelTests` pins that.
- **Live preview on each keystroke:**
  - The Bytes cell shows the new encoding next to the old.
  - The Comment cell shows the error in red with a column, or the warnings in amber, with the full text in a tooltip.
- **Keys:** Enter commits only when the result is ok and changed. Escape or losing focus cancels. Stretch: Shift+Enter commits and opens the next row.
- **While editing:** Follow-PC scrolling is suspended. The edit is cancelled when `SourceId` changes or the CPU combo changes.
- **App wiring:** `SetSymbolResolver(std::function…)`, the same setter style as `SetOfferDecompile` and `SetSaveErrorHandler` (`AssemblyPanel.h:62,103`). `Request` gains `assembled`, `asmAddr`, `asmOld` and `asmNew` so App can log the edit.

## Opcode round-trip analysis

- Every valid decode path consumes all 16 bits, either by exact match or by assigning every bit to a field. At a fixed address, text maps back to a single opcode. Everything else prints `.word 0xNNNN`.
- **Expected non-reassemblable opcodes: none**, given the syntax in decision 3. The test must assert the list is empty rather than allow-list anything.
- The one non-instruction text is `????` from `Sh2DecodeAt` on a short read (`Sh2Disasm.cpp:350-359`). That is an unreadable row, and editing is disabled there.
- Branch and absolute PC-relative text is address-dependent, so it only round-trips at the same address. The test passes the address.

## Tasks

**2A: Assembler (pure).**
- Owns: `FrontEnd/src/Debug/Sh2Asm.{h,cpp}`, `FrontEnd/tests/Sh2AsmTests.cpp`, plus its test-target block linking `Sh2Disasm.cpp`. Header first.
- Acceptance:
  - For every opcode 0..0xFFFF, at addresses `0x06004000`, `0x06004002`, `0x00200000`, `0x26004000` and `0x060FFFFE`: `Sh2Assemble(Sh2Decode text) == op`.
  - The same holds for `Sh2DecodeAfterBranch` text after a `bra`, with `afterDelayedBranch` set.
  - For every ok result, decoding back equals the input semantics.
  - Error cases: unknown mnemonic, wrong operand count, a non-`r0` register in an `r0`-only form (`mov.b r1,@(4,r5)`), an immediate out of range, a misaligned or out-of-range displacement, an odd, backward or too-far PC-relative target, each branch range at ±1 around its limits, and unknown or ambiguous symbols.
  - `mov.l #0x12345678,r1` produces the literal-pool message.
  - Each delay-slot warning is produced.

**2B: Panel editing.**
- Owns: `FrontEnd/src/AssemblyPanel.{h,cpp}` and additions to `FrontEnd/tests/AssemblyPanelTests.cpp`. It adds `Debug/Sh2Asm.cpp` to that test target (`CMakeLists.txt:1136`).
- Acceptance, via `ImGuiHarness`:
  - Double-click opens the editor with exact seed text.
  - Typing `nop` and Enter writes `00 09` in one `WriteMemory` call.
  - Invalid text writes nothing and shows the error.
  - A refused backend shows the refusal and leaves the bytes.
  - Escape cancels.
  - Row height is unchanged during the edit.
  - With Auto Refresh off the row re-decodes after commit.
  - A `SourceId` change cancels the edit.
  - Edit Literal writes 4 bytes at the effective address and reports sharers.

**2C: Integration (App owner; after 2B).**
- Owns: `App.cpp` (about five lines: the resolver setter at Initialize and an `mLog` line in `DrawAssembly`, `:3102`), the three FrontEnd source lists and every other target that compiles `AssemblyPanel.cpp`, docs and the web guide. No `App.h` change.
- Acceptance: all targets build including web; `ctest` is green; a real-context test extending `MemoryBackendTests` or `DecompilerPanelTests` shows that an assembled write moves `se_derive_serial`.
- Manual check: a staged edit on a scrubbed frame replays with Play From Here, and an edit inside a mapped location appears in Apply Changes to Disc.

**Sequencing across both features:** 1A, 1B, 2A and 2B can run in parallel, since their files are disjoint apart from each appending its own test block to `CMakeLists.txt`. Then one agent does 2C followed by 1C. That agent is the only editor of `App.*`, the FrontEnd source lists, `FEATURE_MAP.md` and `features.html`.

## Docs

- `FEATURE_MAP.md`: extend the "SH-2 assembly…" row and the "Hex view / edit" row.
- `Systems/Debugger.md`: assembly section, constraints (literal pool, slot warnings) and tests.
- `Systems/MemoryAndHistory.md`: the write-consumer list.
- `Systems/Decompiler.md`: one line saying assembled edits invalidate through the derive serial.
- `KNOWN_LIMITATIONS.md`.
- `web/guides/features.html`: the SH-2 Assembly card (`:438-440`).
- A comment in `Integration/Validation/V1/sh2asm.py` pointing at the C++ assembler; no behaviour change.

## Risks

- **Running emulator:** a queued poke lands at the next frame gate. Whether Mednafen's emulated SH-2 instruction cache can keep executing the old opcode after a poke was not investigated. It applies equally to today's hex edits of code and needs a manual live check.
- **Running Pseudocode view:** it keeps its capture until its refresh policy fires, as with hex edits.
- **Call Stack after an edit while paused:** I did not verify that it rebuilds. Its origin is keyed on the derive serial (`App.cpp:3214`, `:4774`), but I did not trace the dirty flag.
- **Symbol names** are not unique and may contain characters the tokenizer must allow.
- **Editing under Auto Refresh** on self-modifying code: the preview's "old" bytes can change mid-edit. Re-read at commit and show old and new in the log.

## Out of scope

- Literal-pool allocation, multi-line paste and assembling to a file.
- Labels other than function names, and expressions beyond `sym±n`.
- Refactoring `Sh2Decode` onto a table.
- Undo history.
- Automatic patch-location creation for edited code. The hex editor's "Find in game files (for patching)" remains the route.
- 68000 assembly.
- SH-2 register writes. There is no host API for them, per `Debugger.md`.

## Could not determine

- Which sizes `BreakpointManager::AddMemory` and the live watchpoint installer accept beyond 1, 2 and 4.
- Whether a live sink can accept only one byte of a two-byte write. The plan handles it as an error.
- Web-build persistence of the work-folder files.

### Critical Files for Implementation
- /home/user/RomHackingTools/SaturnExplorer/FrontEnd/src/Debug/Sh2Disasm.cpp
- /home/user/RomHackingTools/SaturnExplorer/FrontEnd/src/AssemblyPanel.cpp
- /home/user/RomHackingTools/SaturnExplorer/FrontEnd/src/WatchPanel.cpp
- /home/user/RomHackingTools/SaturnExplorer/FrontEnd/src/Debug/MemoryBackend.cpp
- /home/user/RomHackingTools/SaturnExplorer/FrontEnd/src/App.cpp
