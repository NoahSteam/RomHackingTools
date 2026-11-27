# V1 guest program: expected hardware events

Status: **derived from the guest program and the pinned Mednafen source, not yet compared with a run.**
Nothing here was observed on an emulator; the hooks' only runtime evidence is the unit and component tests
listed in `Docs/Codebase/Systems/HardwareEvents.md`. A row that disagrees with a run is a finding: either the
hook, or this derivation, is wrong.

Companion to `EXPECTED.md` (same phases, same conventions: steady frames 5..118 of each phase, the guest runs
forever and wraps). Derivations come from `v1_program.py` and from `scu.inc`, `vdp1.cpp`, `smpc.cpp`,
`cdb.cpp` and `sh7095_ops.inc` of the Mednafen tree. `tests/check_v1.py` pins the guest's side of each
derivation on its fake machine (marked **[pinned]**); what the emulator then emits is not checked there.

Marks: **exact** = exactly this in every steady frame; **range** = a band with its reason; **not asserted** =
with the reason. "Sum exact" means exact over any window of 2 or more steady frames, but a single frame may
differ by one because the emulator's frame boundary is not aligned to the guest's VBlank-IN.

## How to read it

Arm all five Log chips (CPU, DMA, VDP, CD, SMPC), i.e. every `SE_HWEV_EN_*` bit, before the phase you want,
and read the rows of a 16-frame window centred in the phase. Two reading rules:

* The `frame` shown for an event is the committed frame number, one behind the frame the emulator is
  executing (the Profiler's frame number). Compare counts per window, not frame numbers.
* The per-source cap (64 per frame) is counted **per CPU** (master, slave, no CPU each have their own budget).
  The master and the devices never reach it in this guest, so a `SUPPRESSED` row for the master or for a
  device in any phase is a finding. The one expected exception is phase 9, where the slave overflows its own
  budget (below).


> **Order and stamp of the `SUPPRESSED` row (observed on a real run, 2026-11-27).** The row is queued at the end
> of the frame it summarises, after that frame was committed, so it carries the NEXT frame's number and comes
> before that frame's own slave rows: a steady phase-9 stamp reads master `SH2_EXCEPTION`, `SCU_INT_MASK`,
> `SUPPRESSED` (cpu 1, n = 13,444), then 64 slave `SH2_EXCEPTION` rows. The phase's entry frame holds 64 rows, a
> smaller `SUPPRESSED` and 64 more. Count rows per phase rather than relying on "64 then SUPPRESSED" inside one stamp.

## Events in every phase (steady frames)

| Source / kind | Expected per steady frame | Mark | Why |
|---|---|---|---|
| CPU_EXC `SH2_EXCEPTION`, cpu 0 | **1**: vector 0x40, class INT (0), level 15, saved PC = `main_loop` + 2 (the instruction after `sleep`), handler PC = `vbin` | exact for the content **[pinned]** (vector table, saved PC); count sum exact | `vtab[0x40]` = `vbin`, every other vector = `bad_irq`; the CPU is asleep when VBlank-IN arrives because the per-frame work is shorter than a frame; a pending exception makes `SLEEP` fall through, so the pushed PC is after it (`sh7095_ops.inc`, `SLEEP`); VBlank-IN is SCU level 0xF (`internal_tab[0]`) |
| CPU_EXC, any other vector or class (NMI, address error, illegal instruction, slot illegal, TRAPA, on-chip peripherals) on the master | **0** | exact | any of them would land in `bad_irq`, a spin loop, and the phases would stop advancing |
| SCU_INT `SCU_INT_ACCEPT` | **1**: arg0 source bit 0 (VBIN), arg1 level 15, arg2 vector 0x40 | exact | only IMS bit 0 is unmasked (`0xFFFFFFFE` gives `IMask` 0xBFFE) |
| `SCU_INT_ACCEPT` arg3 (`IPending` after) | bit 0 clear; bits 1 (VBOUT) and 2 (HBIN) set; the rest not asserted | range | masked sources stay pending and are never cleared (the guest never writes IST), so the value only grows within a run: VDP1 draw end (bit 13) after phase 4, DMA end level 0 (bit 11) after phase 6, SMPC (bit 7) after a command are expected once those phases have run, but the BIOS's leftovers are unknown |
| SCU_INT `SCU_INT_MASK` | **1**: arg0 new IMask **0xBFFE**, arg1 old IMask **0xBFFF** | exact **[pinned]** | the handler's one `mov.l 0xFFFFFFFE` to IMS; `SCU_MSH2VectorFetch` has just set `IMask` = 0xBFFF; `IMask = (IMask &~ mask) \| (v & mask & 0xBFFF)` with mask 0xFFFFFFFF |
| VDP2 `VDP2_VBLANK` | **1** IN (arg0 1) and **1** OUT (arg0 0) | sum exact | one VBlank per frame; the hook sits at the top of `SCU_SetHBVB` and fires on a level change only |
| VDP1 `VDP1_FB_SWAP` | **1**: arg0 displayed FB alternating 0/1 frame to frame, arg1 FBCR **0x0000**, arg2 TVMR **0x0000** | exact, the parity of arg0 not asserted **[pinned]** (FBCR and TVMR are written 0 once at start) | `FBCR` = 0 means an automatic swap at every frame, in every phase and whether or not VDP1 draws (`vdp1.cpp` `SetHBVB`, `!(FBCR & FBCR_FCM)`); the stored values are the written ones (`value & 0x1F`, `value & 0xF`) |
| VDP1 `VDP1_FB_ERASE` | **0** | exact **[pinned]** | the hook is on "VB erase start", taken only when `TVMR` VBE is set or a rotation erase is pending; the guest writes `TVMR` = 0 |
| VDP2 `VDP2_DISPLAY` | **0** | exact **[pinned]** | `TVMD` is written only at start-up (0x0100, then 0x8100 in `hw_init2`) and the hook fires only on a change of the display bit |
| SH2_DMAC `DMAC_START` / `DMAC_END` | **0** on the master | exact **[pinned]** (the guest writes no on-chip register) | the guest never touches the SH-2 DMAC |
| SCU_DMA `SCU_DMA_ILLEGAL` | **0** | exact | both DMA addresses are legal |
| CD `CD_COMMAND`, `CD_RESPONSE` | **0** | exact **[pinned]** (no CD block register is ever written) | a response is produced only inside a command; the guest sends none |
| `SUPPRESSED`, lost-events row | **0** in every phase except phase 9, where exactly one `SUPPRESSED` row with cpu 1 (source CPU_EXC) per steady frame is expected | exact | about 10 records per source per frame at most for the master and the devices; the slave's phase 9 flood is the exception (above) |

Order inside one frame, where the code fixes it: `VDP2_VBLANK` IN, then `SCU_INT_ACCEPT`, then
`SH2_EXCEPTION`, then `SCU_INT_MASK` (the handler runs after the exception); `VDP2_VBLANK` OUT before any
`SCU_DMA_START` of that edge; `VDP1_FB_SWAP` before `VDP1_DRAW_START`. The order between sources otherwise
(including `ts` values) is **not asserted**.

## Per phase: what is added

| id | Phase | Events beyond the common set | Mark and reason |
|---|---|---|---|
| 0 | IDLE | `SMPC_COMMAND` 0x07 (SNDOFF) then 0x03 (SSHOFF), in the phase's first frame only; none in steady frames | exact order **[pinned]**. arg1 / arg2 (IREG) **not asserted**: the guest never writes IREG, so they hold what the BIOS left. The frame of the event is not asserted: the command starts when the SMPC task runs it (92 SMPC clocks after COMREG), normally still the entry frame because the guest polls SF |
| 1 | BUSY | none | exact: the loop makes no device access |
| 2 | FUNC | none | exact |
| 3 | BUS | none | exact: no write to an SMPC command port, CD block or on-chip register **[pinned]** |
| 4 | VDP1_ONE | `VDP1_DRAW_START` **1** (arg0 draw FB = 1 - the same frame's swap arg0, arg1 **0**); `VDP1_DRAW_END` **1** (arg0 **0x20**, arg1 0 = END bit) | exact; the end address is 32 * commands **[pinned]**; arg1 of DRAW_START is 0 because the swap clears `DrawingActive` before `StartDrawing`; a draw can land in the adjacent frame (sum exact) |
| 5 | VDP1_MANY | as phase 4; DRAW_END arg0 **0x800** | exact (64 commands) **[pinned]** |
| 6 | DMA_BIG | `SCU_DMA_START` **1**: arg0 **0x12010000** (level 0, direct, start factor 1 = VBlank-OUT, read bus C, write bus B), arg1 **0x06020000**, arg2 **0x05C20000**, arg3 **16384**; `SCU_DMA_END` **1** (arg0 0) | exact **[pinned]** (arg0 packing recomputed from the programmed registers); the end follows its start in the same frame unless the frame boundary falls between (sum exact) |
| 7 | DMA_SMALL | as phase 6 with arg3 **4096** | exact **[pinned]** |
| 8 | DMA_DRAW | the phase 6 DMA events (16384) and a 400-command draw: `VDP1_DRAW_START` 1; `VDP1_DRAW_END` 1 with arg0 **0x3200**, arg1 0 if the draw completes, otherwise one with arg1 **1** (aborted by the next swap) and arg0 <= 0x3200 | range: the DMA halts VDP1 while it writes VDP1 VRAM, so the draw may be cut (`EXPECTED.md`: overruns 0 or 1); one end per start either way |
| 9 | SSH_ON | `SMPC_COMMAND` 0x02 (SSHON) in the first frame. **Master**: the common set unchanged, i.e. its one vector-0x40 exception **every frame** (the slave's flood has its own cap budget and cannot drop it). **Slave**: `SH2_EXCEPTION` cpu 1, vector **0x43**, class INT, level **6**, the first 64 of the frame, then one `SUPPRESSED` row (source CPU_EXC, cpu 1, arg1 64) saying how many more were dropped (about 13,500 interrupts per frame in total). Any DMAC, CD or SMPC event from the slave: **not asserted** | Observed on the real patched Mednafen: the slave's only interrupt input is the IRL auto-vector, and `scu.inc` (around line 394) drives `CPU[1].SetIRL(((VB \| HB) << 1) \| (VB << 2))` so IRL 6 is held through the whole VBlank; `sh7095.inc` `GetPendingInt` takes vector `(IRL >> 1) + 64` = 0x43, and the slave takes it again as soon as its handler returns. Before the cap was per CPU this flood filled the shared CPU_EXC budget and dropped the master's exception in 212 of 214 frames. The Log names vector 0x43 on the slave "VBlank-IN (IRL 6 auto-vector)" (not the master's SCU "Timer 0") |
| 10 | SSH_OFF | `SMPC_COMMAND` 0x03 in the first frame; from the next frame no event with cpu 1 | exact for steady frames: the slave is held off |
| 11 | M68K_STOP | `SMPC_COMMAND` 0x07 then 0x06 (SNDOFF, SNDON) in the first frame | exact order **[pinned]**; the guest writes the 68000 vectors and program into sound RAM between them **[pinned]** |
| 12 | M68K_BUSY | as phase 11 | as phase 11 |
| 13 | M68K_OFF | `SMPC_COMMAND` 0x07 (SNDOFF) in the first frame | exact order **[pinned]** |

Phases 11 to 13 produce **no event from the 68000 or the SCSP**: no hardware event source covers them, and the
guest enables no SCSP interrupt (no SCSP register is written **[pinned]**), so no `SCU_INT_ACCEPT` for bit 6
appears. SCU DMA events stop after phase 8 because the next phase entry writes `D0EN` = 0 (`quiesce`); VDP1
draws stop because `quiesce` writes `PTMR` = 0.

Transition frames (the first 4 and last 2 of every phase) are not asserted: the old phase's DMA or draw may
still produce an event, and the entry work runs.

## Not derived

* **The BIOS boot path**: every SMPC, CD block, VDP2 display and SH-2 event before the guest's first frame,
  including the guest's own start-up `VDP2_DISPLAY` events (0x0100 changes the display bit only if the BIOS
  left it on; 0x8100 turns it on). A capture armed after launch misses them anyway.
* **The slave in phase 9** beyond the vector 0x43 level 6 flood described above, and what it does to the SCU or SMPC.
* **SMPC IREG arguments** (the BIOS's leftovers) and the exact frame of an SMPC event.
* **The exact `IPending`** in `SCU_INT_ACCEPT`, the order of events between sources, and `ts`.
* The CD hooks beyond "no event": nothing in the guest issues a CD command, so neither hook is exercised. V1
  cannot show that the CD, NMI, slot-illegal or on-chip DMAC hooks work.

## What dropping each hook must change

Remove the named fence from the patched tree and read the phase named (`check_component.py --drop <key>` does
the same to a copy for the component harness).

| Hook key | V1 row that must change | Change |
|---|---|---|
| `SE_HWEV_HOOK_EXC_INT` | every phase | no `SH2_EXCEPTION` at all |
| `SE_HWEV_HOOK_EXC_NMI`, `_CPUADDR`, `_DMAADDR`, `_ILLINSTR`, `_ILLSLOT`, `_TRAP` | none | V1 cannot detect these (the guest raises none) |
| `SE_HWEV_HOOK_DMAC` | none on the master | V1 cannot detect it (the guest does not use the on-chip DMAC) |
| `SE_HWEV_HOOK_SCU_INT_ACCEPT` | every phase | no `SCU_INT_ACCEPT` |
| `SE_HWEV_HOOK_SCU_INT_MASK` | every phase | no `SCU_INT_MASK` |
| `SE_HWEV_HOOK_VBLANK` | every phase | no `VDP2_VBLANK` |
| `SE_HWEV_HOOK_SCU_DMA_START` | phases 6, 7, 8 | no `SCU_DMA_START` |
| `SE_HWEV_HOOK_SCU_DMA_END` | phases 6, 7, 8 | no `SCU_DMA_END` |
| `SE_HWEV_HOOK_SCU_DMA_SRC_REFUSED`, `_DST_REFUSED` | none | V1 cannot detect them |
| `SE_HWEV_HOOK_VDP1_DRAW_START` | phases 4, 5, 8 | no `VDP1_DRAW_START` |
| `SE_HWEV_HOOK_VDP1_DRAW_END` | phases 4, 5 (and 8 when it completes) | no END-bit `VDP1_DRAW_END` |
| `SE_HWEV_HOOK_VDP1_DRAW_ABORT` | phase 8, only if a run shows an abort | no aborted `VDP1_DRAW_END`; undetectable if no abort occurs |
| `SE_HWEV_HOOK_VDP1_FB_SWAP` | every phase | no `VDP1_FB_SWAP` |
| `SE_HWEV_HOOK_VDP1_FB_ERASE` | none | V1 cannot detect it (`TVMR` VBE is 0) |
| `SE_HWEV_HOOK_VDP2_DISPLAY` | none in steady frames | only the start-up events, if captured |
| `SE_HWEV_HOOK_SMPC_COMMAND` | phases 0, 9 to 13 (first frames) | no `SMPC_COMMAND` |
| `SE_HWEV_HOOK_CD_COMMAND`, `_CD_RESPONSE`, `_CD_STATUS_RESPONSE` | none | V1 cannot detect them |
| the glue's mask test (`SeSsHwEvMask`) | any | with the chips disarmed, no event in any phase; arming one chip yields only that chip's sources |
| the per-frame cap | none | never reached by V1; covered by the glue unit test |
