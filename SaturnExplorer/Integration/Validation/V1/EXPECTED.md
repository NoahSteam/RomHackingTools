# V1 guest program: expected profiler observations

Status: **written before the guest program existed and before any run.** Nothing in this file has been
confirmed on an emulator. Every number is derived from the counter definitions
(`Integration/Common/SePerfCounters.h`, `Docs/Codebase/Systems/Profiler.md`) and from the pinned
Mednafen source in `_emu/mednafen/src/ss` (the files named in each derivation). A figure marked
**[derived]** follows from those sources; **[range]** is a tolerance band chosen below; **[qual]** is a
qualitative claim (zero / nonzero / ordering) because the exact figure depends on bus timing the
derivation does not pin down. When a run disagrees, the disagreement is the finding: either the
profiler, or this derivation, is wrong, and the CHECKLIST says which phase to look at.

Plan reference: `Docs/ProfilerFixPlan.md`, Phase 5, V1. The one-line summary of what the guest does:
a cyclic sequence of fixed-length phases, each isolating one counter family, driven by the SCU
VBlank-IN interrupt, with the master SH-2 asleep between frames.

## Machine-readable parameters

`build_v1.py` and `tests/check_v1.py` read this block; they must agree with the program.

```params
PHASE_FRAMES=120
SETTLE_FRAMES=4
BUSY_L=40000
FUNC_A_N=3000
FUNC_B_N=1000
VDP1_ONE_CMDS=1
VDP1_MANY_CMDS=64
DMA_BIG_BYTES=16384
DMA_SMALL_BYTES=4096
DMA_DRAW_CMDS=400
```

## Conventions

* **Frame** means one call of the emulator's frame function (the unit of a v27 perf record).
  `F` is the record's frame-cycle counter (index 0); the guest never changes the video mode after its
  first frames, so `F` is constant frame to frame. Its absolute value is **not asserted** (it depends on
  the VDP2 mode and on NTSC/PAL, and was not measured here). Informational sanity band for NTSC
  320x224: 440,000..460,000 SH-2 cycles; PAL: 520,000..560,000. Everything below that depends on `F` is
  written as a function of `F`, and the CHECKLIST records the observed `F` once.
* **Phase**: `PHASE_FRAMES` = 120 VBlank-IN wakes. The first `SETTLE_FRAMES` = 4 and the last 2 frames of
  a phase are *transition frames*: the phase-entry work (SMPC commands, VDP1 table build) runs in the
  first one, and the emulator's frame boundary is not aligned to the guest's VBlank-IN, so the frame in
  which a phase changes is a mixture. **Read only steady frames** (frames 5..118 of a phase). The
  Profiler's averaging window must lie inside that range; a 16-frame window centred in the phase is
  the intended reading.
* The guest runs forever: after the last phase it re-enters phase 0, so every phase can be read again.
* Phase identification during a run: the backdrop colour (table below) and a live word in work RAM high
  at `0x0600FF00` = phase id, `0x0600FF04` = VBlank-IN count (visible in the Memory view).
* Cache modes. The disc is not in Mednafen's game database, so the default is `data` (instruction
  fetches served from the fast map, data cache emulated, `cacheMode` = OFF on the wire). The second
  mode is selected with `-ss.dbg_cem full` (`cacheMode` = ON). Expectations are given for `data`;
  where `full` differs it is stated, and where it is not stated for `full` it is **unverified and
  expected to be wider** (a cold instruction cache costs line fills in the first frame only).
* "Exact" means exactly equal in every steady frame. "= x +- t" is an absolute tolerance per frame.

## Cycle model used for the pure CPU arithmetic

From `sh7095.inc` (`FetchIF`, `Branch`, `DoIDIF`) with instruction fetch from the fast map
(cache mode `data`): every instruction costs 1 cycle (the fetch's `timestamp++`); a *taken* `bt`/`bf`
costs 3 (own fetch + `FetchIF_ForceIBufferFill` + the refetch in `DoIDIF`); a not-taken one costs 1;
register-only ALU instructions (`dt`, `add`, `cmp`) cost 1 and wait for no memory. A load that is
followed by a use of its destination waits for the bus, so loop bodies in the exact derivations
contain **no** memory operation.

The guest's pure loop is `L: dt r1 ; bf L` with `r1 = N`. Per iteration `dt` = 1 and a taken `bf` = 3;
the last iteration's `bf` is not taken (1):

    cycles(N) = 4*(N-1) + 2 = 4N - 2

`tests/check_v1.py` recomputes this from the *emitted bytes* (decoding with the repo's SH-2 decoder
and summing the model above), so a change to the loop that breaks this arithmetic fails the test.

| Loop | N | `4N - 2` |
|---|---|---|
| busy phase | `BUSY_L` = 40,000 | **159,998** |
| function A | `FUNC_A_N` = 3,000 | **11,998** |
| function B | `FUNC_B_N` = 1,000 | **3,998** |

Fixed per-call overhead outside the loop (literal load, return, delay slot, call) is **not exact**
because it contains a literal-pool load from cached work RAM; it is bounded below by the instruction
count and above by 40 cycles (a first-touch cache line fill plus load-use wait). All "awake overhead"
figures below use **OV = 600 cycles** as a deliberately generous bound for the per-frame handler and
dispatcher (about 25 instructions of exception entry, handler and dispatcher with a handful of cached
work-RAM accesses; the lower bound is 25 cycles). OV only widens ranges; it never narrows them.

## Phase table

Backdrop colours are RGB555 so the phase can be told apart on screen without reading memory.

| id | name | what the guest does every frame | backdrop |
|---|---|---|---|
| 0 | IDLE | nothing; slave off, 68000 held in reset, VDP1 not drawing | 0x0000 black |
| 1 | BUSY | runs the pure loop `BUSY_L` times, then sleeps | 0x001F red |
| 2 | FUNC | calls function A (3000) then function B (1000), then sleeps | 0x03E0 green |
| 3 | BUS | the bus table below | 0x7C00 blue |
| 4 | VDP1_ONE | VDP1 draws a table of 1 command + END each frame | 0x03FF yellow |
| 5 | VDP1_MANY | VDP1 draws 64 commands + END each frame | 0x7FE0 cyan |
| 6 | DMA_BIG | SCU DMA level 0, 16384 bytes work RAM high to VDP1 VRAM each frame, VDP1 not drawing | 0x7C1F magenta |
| 7 | DMA_SMALL | as 6 with 4096 bytes | 0x4210 grey |
| 8 | DMA_DRAW | the 16384-byte DMA while VDP1 draws a 400-command table | 0x2D6B dark |
| 9 | SSH_ON | `SSHON` issued in the first frame; slave runs from then on | 0x5294 |
| 10 | SSH_OFF | `SSHOFF` issued in the first frame | 0x35AD |
| 11 | M68K_STOP | 68000 released, program is `STOP #$2700` | 0x1CE7 |
| 12 | M68K_BUSY | 68000 released, program is `BRA.S *` | 0x739C |
| 13 | M68K_OFF | `SNDOFF`: 68000 held in reset again | 0x0842 |

Unless a phase says otherwise: VDP1 commands/starts/overruns/work/discarded = 0, DMA = 0,
68000 executing = 0, slave on-cycles = 0, master and slave stall = 0, DSP cycles = 0.

## Per-phase expectations

### Phase 0 IDLE (master sleep, baseline)

| Counter | Expected | Why |
|---|---|---|
| frame cycles (0) | `F`, constant | |
| master sleep (1) | `F - OV .. F - 25` -> share **>= 99.8%** [range] | `SLEEP` op idle step adds 3 cycles per step (`sh7095_ops.inc` ~1734) for the whole interval between `sleep` and the next interrupt; awake time is only exception entry + handler + dispatcher |
| master stall (3) | 0 [derived] | stall is counted only by `PSEUDO_DMABURST` (SH-2 DMAC burst or `ExtHalt`, which only an SCU DMA touching the CPU bus sets) |
| slave sleep (2), slave stall (4) | 0 | slave is off |
| slave on-cycles (5), valid bit `SSH2_INT` set | **0** exactly [derived] | no `SetActive(true)` edge in the frame |
| VDP1 commands/starts/overruns (7,8,9) | 0, work (55) 0, discarded (56) 0 | `PTMR` = 0: `SetHBVB` never calls `StartDrawing` |
| 68000 total (12) | `T = F * ratio`; ratio `0.4205 +- 0.5%` for NTSC 320-wide, `0.3943 +- 0.5%` for NTSC 352-wide (`smpc.cpp` `SOUND_SetClockRatio` = 11,289,600 * divisor / MasterClock); PAL differs (about `0.4235` / `0.397`) | the 68000/SH-2 clock ratio; total is the 68000 time the frame advanced, quantised to 4 cycles (a halted step is `timestamp += 4`) |
| 68000 idle (57) | **= total** exactly | held in reset by SMPC `SNDOFF`: `XPENDING_MASK_EXTHALTED`, every step is the idle fall-through |
| 68000 executed (11) | **0** exactly | total - idle |
| bus requests, devices other than work RAM high | all 0 | handler touches only work RAM high; instruction fetch is not a request in `data` mode |
| bus requests work RAM high (device 12) | reads 0..8, writes **4..16** per frame [range] | exception entry pushes SR and PC, the handler pushes two registers and writes the frame counter, the dispatcher writes a few state words; all are write-through |

This phase's bus figures are the **baseline** that the BUS phase is differenced against.

### Phase 1 BUSY

Master busy = `F - sleep` = `159,998 + overhead`, overhead in `[25, OV]` -> **busy in [160,023, 160,598]**,
i.e. master sleep = `F - busy` [derived, range]. Everything else as phase 0 (the loop has no memory
operation, so the bus counters equal the baseline). In cache mode `full` the same figure is expected
within +2% (a 2-instruction loop is resident after the first iteration; the first-frame line fill is
the only addition); unverified.

### Phase 2 FUNC (function attribution; needs `--enable-debugger` and the Functions tab ticked)

Per steady frame the Functions tab, master filter, should list two function rows whose entries are the
addresses printed by `run_v1.sh` (labels `func_a`, `func_b`). Self time, callees excluded:

| Row | Expected self cycles | Why |
|---|---|---|
| func_a | `11,998 + c`, **c in [3, 40]** -> [12,001, 12,038] [range] | loop `4N-2` plus literal load, `rts`, delay slot |
| func_b | `3,998 + c`, c in [3, 40] -> [4,001, 4,038] [range] | |
| ratio func_a / func_b | **[2.96, 3.02]** [range] | follows from the two ranges |

The remaining master time is in raw-PC buckets (caller loop, handler): the panel's "Not observed
(estimate)" row and `Other` stay small. After the attribution checkbox is toggled the first frame mixes
two modes (the mode is latched per frame, F6), so read from the second frame after the toggle.
Master busy = `15,996 + c_A + c_B + overhead`; sleep as in phase 1 [range].

Caveat that is part of the expectation: a function entered through an *observed* `jsr` is named; the
caller's own cycles (the loop that issues the two calls, the interrupt handler) sit in raw-PC buckets
because the program was already running when attribution was switched on and an unobserved interrupt
leaves ownership unknown (Profiler.md, F9).

### Phase 3 BUS (bus requests per device)

Each frame the guest performs, through **uncached** addresses (bit 29 set) so the data cache never
absorbs a request, the accesses of the table below. `K` = number of requests added to the phase-0
baseline of that bucket. Unit is the BSC request: one `mov.l`/`mov.b` = one request
(`Profiler.md`, "Bus classification (F8)").

| # | device (index) | address | op | K reads | K writes | counter index (read / write) |
|---|---|---|---|---|---|---|
| 1 | BIOS (0) | 0x20000000 | rd32 | 10 | | 28 |
| 2 | SMPC (1) | 0x20100061 | rd8 | 20 | | 30 |
| 3 | BACKUP (2) | 0x20180001 | rd8 | 30 | | 32 |
| 4 | WRAM_L (3) | 0x20200000 / 0x20280000 | rd32 / wr32 | 40 | 11 | 34 / 35 |
| 5 | CS0_OTHER (4) | 0x20400000 | rd32 | 50 | | 36 |
| 6 | ABUS_CS0 (5) | 0x22000000 | rd32 | 60 | | 38 |
| 7 | ABUS_CS1 (6) | 0x24000000 | rd32 | 70 | | 40 |
| 8 | ABUS_CD (7) | 0x25890008 | rd32 | 80 | | 42 |
| 9 | SCSP (8) | 0x25A7F000 / 0x25A7F100 | rd32 / wr32 | 90 | 15 | 44 / 45 |
| 10 | VDP1 (9) | 0x25C7F000 / 0x25C7F100 | rd32 / wr32 | 100 | 13 | 46 / 47 |
| 11 | VDP2 (10) | 0x25E7F000 / 0x25E7F100 | rd32 / wr32 | 110 | 14 | 48 / 49 |
| 12 | SCU (11) | 0x25FE00A0 | rd32 | 120 | | 50 |
| 13 | WRAM_H (12) | 0x26000000 / 0x26080000 | rd32 / wr32 | 130 | 12 | 52 / 53 |
| 14 | A-bus dummy (X0) | 0x25000000 | rd32 | 140 | | 58 |
| 15 | unmapped (X1) | 0x25900000 | rd32 | 150 | | 60 |

(`Integration/Mednafen/bus_decode.py` assigns each address to the device shown; `tests/check_v1.py`
asserts it.)

Expected per bucket in a steady frame of this phase:

* every device except work RAM high: **baseline (0) + K, exactly**, reads and writes separately;
  writes to devices with no write K stay 0. [derived]
* work RAM high (12): reads `K + (0..8)`, writes `12 + (4..16)` [range]. Not exact because the table
  walk, the loop code and the dispatcher also touch cached work RAM high; in `full` cache mode add up
  to 64 more reads per frame (line fills of resident code), unverified.
* CPU/A/B bus totals follow from the buckets; dummy and unmapped (58..61) are kept out of the three
  totals (`BusTotals::dummy*`, `unmapped*`).
* master busy is informational: bounded above by `15 * 150 * 40 + OV` = 90,600 cycles even if every
  access cost 40 cycles.
* The A-bus cartridge, CS1, CD, dummy and unmapped regions read open-bus values; the program does not
  look at them.

In phase 9 the slave runs BIOS code and the bus buckets are **not** expected to stay at baseline; do
not read bus figures in phase 9.

### Phase 4 VDP1_ONE

Table: 1 SET LOCAL COORD command + END, rewritten at phase entry; `PTMR` = 2, `FBCR` = 0 so a draw
starts at each frame-buffer swap (`vdp1.cpp` `SetHBVB`, `if(PTMR & 0x2) StartDrawing()`).

| Counter | Expected | Why |
|---|---|---|
| draw starts (8) | **1** per frame | one swap per frame |
| commands fetched (7) | **2** | the fetch counter is incremented for every fetched entry including END (`++SeSsPerfC[COMMANDS]` precedes the `0x8000` test) |
| work cycles (55) | **32** | `VDP1_EAT_CLOCKS(16)` per fetched command, and `CMD_SetLocalCoord` returns 0 so the command body costs nothing: `16 * 2` |
| overruns (9) | 0 | a 2-entry table finishes within the first budget |
| discarded (56) | 0 | no DMA |
| elapsed figure (6) | 0 (retired index) | v27 |

The emulator's frame boundary may fall between the swap and the draw, in which case a draw lands in the
adjacent frame; over any window of 2 or more steady frames the *sum* is exact (`2 * frames`). Read a
window and divide.

### Phase 5 VDP1_MANY

64 SET LOCAL COORD + END: starts **1**, commands **65**, work **1,040** (= 16 * 65), overruns 0,
discarded 0 (same derivation). The per-`Update` budget is 263 cycles (`VDP1_UpdateTimingGran`), so the
draw spans several `Update` calls; `CommandPhase` resumption must not double count: **work must be
exactly 1,040, not more** (the F4 "charged once on a resumed phase" property).

### Phases 6, 7 DMA (SCU level 0, start factor VBlank-OUT, work RAM high -> VDP1 VRAM)

`D0MD` start factor 1 (VBlank-OUT) with `D0EN` enable, so the DMA restarts every frame
(`scu.inc` `CheckDMASFByInt`; `Enable` stays set).

| Counter | Phase 6 (16384 B) | Phase 7 (4096 B) | Why |
|---|---|---|---|
| DMA transfers level 0 (13) | **1** | **1** | `StartDMATransfer` after its three refusals: one count per started transfer |
| DMA bytes level 0 (16) | **16384** | **4096** | "requested" bytes = the byte count the transfer was started with |
| route bytes (19 + 7) | **16384** | **4096** | source bus C (2) * 3 + destination bus B (1) = 7 |
| levels 1, 2 and all other routes | 0 | 0 | |
| master stall (3) | **> 0** [qual] | > 0 | `RecalcDMAHalt` sets `ExtHalt` on both SH-2 while a DMA reads the C-bus; each halted step adds 7 |
| stall(6) / stall(7) | ratio **in [3.0, 5.0]** [range] | | duration is proportional to byte count; fixed start-up cost makes it slightly below 4 |
| discarded (56) | **0** | 0 | VDP1 is not drawing, the discard branch needs an active draw |
| slave stall (4) | 0 | 0 | slave off |

The SH-2 stall share = stall / F is **informational** (no independent figure was derived).

### Phase 8 DMA_DRAW

DMA as phase 6 *and* a table of 400 commands + END drawn every frame. Draw starts at VBlank-OUT, the
DMA starts at the same VBlank-OUT.

| Counter | Expected | Why |
|---|---|---|
| draw starts | 1 | |
| commands | **401** if the draw completes; **<= 401** otherwise [range] | the DMA halts VDP1 while it writes VDP1 VRAM, so the draw can be cut by the next swap |
| work cycles (55) | **6,416** (= 16 * 401) if it completes, `<=` that otherwise [range] | |
| discarded (56) | **> 0** [qual] | `Update`: `CycleCounter > 0 && SCU_CheckVDP1HaltKludge()` is true (DMA active, write bus B, read C-bus, destination 0x05C00000..0x05DFFFFF) while `DrawingActive`; the positive budget is zeroed and counted |
| DMA transfers / bytes / route | 1 / 16384 / 16384 | as phase 6 |
| overruns (9) | 0 or 1 [range] | non-zero only if the draw is still active at the next swap |

### Phase 9 SSH_ON, Phase 10 SSH_OFF (F2: integrated slave on-time)

| Frame | slave on-cycles (5), valid `SSH2_INT` | Why |
|---|---|---|
| phases 0..8, steady | **0** exact | |
| phase 9, the frame containing the `SSHON` | **0 < on < F** [qual, partial frame] | the SMPC command finishes some time after the COMREG write; the integrated figure is `[on edge, end of frame)`; the old all-or-nothing figure would show 0 or `F` |
| phase 9, steady | **F exactly** | on for the whole frame, `[0, end_ts)` |
| phase 10, frame with `SSHOFF` | **0 < on < F** [qual] | `[0, off edge)` |
| phase 10, steady and phase 11 onwards | **0** exact | |

The slave executes BIOS code after `SSHON`; its sleep, stall and busy split is **not asserted**. Master
columns stay as phase 0 except in the command frames (the SMPC command poll adds awake time, bounded by
OV + 2,000). If on-cycles shows `F` in phase 10's transition frame, or `0` in phase 9's, the F2
integration is not in effect.

### Phases 11, 12, 13 (68000, F5)

| Phase | idle (57) | executed (11) | total (12) | Why |
|---|---|---|---|---|
| 11 M68K_STOP | **= total** exactly [derived] | **0** exactly in steady frames | `T` as phase 0 | after reset and the vector fetch the program executes `STOP #$2700` once; every later step is the stopped fall-through (4 cycles, idle). The one-off cost is in the first frame only |
| 12 M68K_BUSY | **0** exactly | **= total** | `T` | `BRA.S *` never stops; no SCSP interrupt is enabled, so no interrupt step |
| 13 M68K_OFF | = total | 0 | `T` | `SNDOFF` -> `ExtHalted` |

`T` in all phases: `T = F * ratio` with ratio per the phase 0 row, tolerance **+-0.5% + 12 cycles**
(slice overshoot of at most one instruction per `SOUND_Update` slice). Executed = total - idle holds by
construction (F5), so the **meaningful** checks are the exact idle/executed values above: they
distinguish "STOP is counted idle" from "STOP is counted busy" (the old per-slice sampling) and
"reset-held is idle". The panel's idle + executed share pair must read 100% in every phase.

## Things deliberately not asserted

* Absolute `F`; absolute master awake overhead beyond the `OV` band; slave busy split; DMA durations;
  VDP2 anything (no VDP2 counter exists).
* DSP cycles: the guest never runs the DSP; expected 0.
* Wall-clock: speed is V3's job.

## What disabling a hook must do (plan: "disabling the relevant hook must make it fail")

| Check | Hook | Failure when the hook is disabled |
|---|---|---|
| phase 9/10 partial frames | F2 `SeMednafenSlaveActive` edges | on-cycles shows only `0` or `F` |
| phase 5 work = 1,040 | F4 `VDP1_EAT_CLOCKS` debit placement | a resumed phase doubles it, or work reads 0 |
| phase 11 idle = total | F5 `IdleCycleCount` | idle reads 0 (old per-slice sampling) |
| phase 3 buckets | F8 classification | counts land in neighbouring buckets (dummy/unmapped folded into CD/VDP2/SCU) |
| phases 6/7 route 7 | F11 DMA | route bytes 0 |
| phase 2 rows | F9 ownership | rows missing or raw-PC only |
