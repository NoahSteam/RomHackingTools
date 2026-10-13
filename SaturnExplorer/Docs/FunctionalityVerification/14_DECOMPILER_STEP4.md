# SH-2 Decompiler — Step 4 (quality)

Plan: [Docs/Decompiler/PLAN.md](../Decompiler/PLAN.md) §B Step 4, contracts A1 and A3.

**Started from:** `ca327c0` (Step 3 gate met) · **Step 4 commits:** `1f8f642` (A3 patch 0005), `b1f21f7`
(hardware map), `169c8d5` (BIOS names), `49ab408` (register hint), `ec50e87` (Assembly notes),
`540b761` (callee prototypes), and the commit adding this record.
**Gate status: OPEN.** CI green for each pushed commit except any still running when this was written. The
manual checklist below is pending on Windows and macOS, and the review has not been done.

Done on the way, outside the decompiler (`b187062`): `build-release-msbuild.bat` passes the toolset of
the Visual Studio it finds (2019: v142, 2022: v143; `SE_TOOLSET` overrides), and CI builds it both
ways. The Call Stack's paused strip names the cause: PAUSED, STEPPED, DMA WATCH HIT, BREAKPOINT HIT
or WATCHPOINT HIT (`StopBannerLabel`, `CallStackHeuristicTests`).

## What was added

| Item | Files | Pinned by |
|---|---|---|
| A3: `bra`/`bsr` delay slots get their real PC in the spec | `patches/0005-sleigh-delay-slot-pc.patch` (`superh.sinc`), regenerated `sh-2.sla` and `Sh2SpecData.h`; `Sh2Decompiler.cpp` (`ScanRawPcode` no longer flags those slots, except one that another branch also jumps to) | `TestDelaySlotPcRelative` (fixture 3, extended and Capstone-checked by `fixture3.py`); `SlaFreshnessTests`; `TestDelaySlotFlagged` (panel, now on the `bf/s` case) |
| Hardware registers as named globals | `Decompiler/SaturnHardwareMap.{h,cpp}` (256 registers: VDP1, VDP2, SCU, SMPC, SCSP common block) | `TestHardwareRegisterNames` |
| BIOS functions named `bios_0x…` | `Sh2Decompiler.cpp` | `TestBiosFunctionNames` |
| Register hint for a computed call at the halt | `Decompiler/RegisterHint.{h,cpp}`, `DecompilerPanel.cpp` | `TestRegisterHintAtHalt` |
| SH-2 Assembly notes on decompiled lines | `DecompilerPanel.cpp`, `App.cpp` (`DecompilerInputs::comments`) | `TestUserCommentsOnLines` |
| Callee prototypes lent from earlier decompiles | `Sh2Decompiler.{h,cpp}` (`Prototype`, `TypeShape`, `DecompileRequest::prototypes`, `DecompileResult::prototype`), `DecompilerPanel.{h,cpp}` (question carries the lent set's hash) | `TestCalleePrototypes`, `TestCalleePrototypesLent` |

No change to `include/saturnexplorer/*.h`, `SE_ABI_VERSION`, Core or the drivers. One new vendor
patch (0005), the one PLAN.md A3 calls for.

**Mutations** (each rebuilt and run; each made a test fail): A3 host flag left on for `bra`/`bsr`; A3
flag dropped for the jumped-to slot; the hint shown while running; the hint shown for a resolved
call; the prototype hash left out of the question. **ASan:** `Sh2DecompilerTests`,
`DecompilerPanelTests`, `DecompilerRunnerTests` clean at `540b761`.

## Manual checklist

Build master, open the SH-2 Pseudocode panel (View → Debugger). A savestate of a real game is enough
for 1–4 and 6; 5 needs live Mednafen.

| # | Check | Expected | Windows | macOS |
|---|---|---|---|---|
| 1 | Decompile several functions that were fine in Step 3 | Same functions decompile; no new errors or warnings | | |
| 2 | Find a function that touches VDP2/VDP1/SCU/SMPC (e.g. follow PC in a game's V-blank handler, or search the Assembly for `0x25F8`) | Registers print as `VDP2_TVMD`, `SCU_IST`, `SMPC_SF`, … instead of `*(ushort *)0x25f8…` | | |
| 3 | The panel's BIOS button → choose a BIOS image; decompile a function that calls into the BIOS | Calls read `bios_0x000006xx(...)`; clicking the call navigates; Clear BIOS makes them opaque again | | |
| 4 | In SH-2 Assembly, add a note on an instruction inside the shown function | The note appears as `// note` on the matching pseudocode line at once, with no "decompiling…" | | |
| 5 | Live: set a breakpoint on a `jsr @rN` whose target is computed (pseudocode shows `(*(code *)…)(…)`); let it hit | Amber hint `jsr 0x…: rN = 0x… at the halt` in the status line and on the call's line; gone after Step Into and while running | | |
| 6 | Decompile a caller, then one of its callees, then the caller again | The caller is decompiled again; its call to that callee shows the callee's argument count and return | | |
| 7 | Any function with a PC-relative load in a `bra`/`bsr` delay slot (rare; skip if none found) | No "not trustworthy" banner; the literal is the one at the branch-target-relative address | | **Pass** at `169c8d5` (arm64): fixture 3 written into HWRAM of GS-9169_007.yss. `06006000` (`bra`): no banner, `return 0x22222222;`. `06006100` (`bf/s`): banner at `0x06006104`, body reads `iRam06006110`, unfolded. `06006300` (`bsr` + `mova`): no banner, `xVar1 = func_0x06006308(); return xVar1;` |

## Findings outside the decompiler (macOS run, relayed)

- **Pause banner**: PAUSED, BREAKPOINT HIT and STEPPED read correctly at `1f8f642`. After Step Into,
  Continue to the same breakpoint read WATCHPOINT HIT with no watchpoint set (2 of 2). Fixed in
  `efbcb62`: a breakpoint owns a halt at any mirror of its address, and a stop that no breakpoint
  or armed watchpoint explains reads HALTED. Needs re-test.
- **Open, live run control:** remove the breakpoint while halted on it, then Continue. The emulator
  stays at the same frame and PC. Run → Resume then runs it, but the app still says paused: the
  Call Stack shows PAUSED, the status bar says Paused, and the pseudocode panel says "live, paused"
  and "decompiling… [stale]". Reproduced at `169c8d5`. Not caused by Step 4, and the root cause is
  under investigation with the emulator.
