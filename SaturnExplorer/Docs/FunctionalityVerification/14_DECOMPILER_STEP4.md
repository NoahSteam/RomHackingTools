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
| 1 | Decompile several functions that were fine in Step 3 | Same functions decompile; no new errors or warnings | |  **Pass** at `68539ce` (arm64), savestate GS-9169_007.yss: `func_0x06030548` (`param_1 * 7000`), `06008204`, `06031598`, `0601be30` decompile, no errors or warnings (live paused: `0021a462`, `00226f74`, `0021a398` at `1f8f642`). |
| 2 | Find a function that touches VDP2/VDP1/SCU/SMPC (e.g. follow PC in a game's V-blank handler, or search the Assembly for `0x25F8`) | Registers print as `VDP2_TVMD`, `SCU_IST`, `SMPC_SF`, … instead of `*(ushort *)0x25f8…` | |  **Pass**: entry `06005cc8` ends with `puVar7 = &VDP2_TVMD;`; entry `0601b6f4` reads `(&SCU_D0R)[param_2 * 8] = *param_1;`, `(&SCU_D0W)`, `(&SCU_D0C)`, `(&SCU_D0AD)`, `(&SCU_D0MD)`; entry `060056e4` has `VDP1_ENDR = 0;`. |
| 3 | The panel's BIOS button → choose a BIOS image; decompile a function that calls into the BIOS | Calls read `bios_0x000006xx(...)`; clicking the call navigates; Clear BIOS makes them opaque again | |  **Pass**, with the image set through `[decompiler] bios` (the Choose BIOS dialog cannot be driven by the harness): a function written into the savestate at `06006700` (`mov.l @(pc),r0; jsr @r0` with the literal `0x600`) reads `xVar1 = bios_0x00000600();`; clicking the call selects the line and moves SH-2 Assembly to `06006702` (the `jsr`); BIOS menu → Stop Using BIOS Image makes it `xVar1 = (*(code *)0x600)();`. |
| 4 | In SH-2 Assembly, add a note on an instruction inside the shown function | The note appears as `// note` on the matching pseudocode line at once, with no "decompiling…" | |  **Pass**: a note on the `jsr @r0` at `0600822A` of `func_0x06008204` appeared at once as `iVar2 = func_0x06030ba0(xVar1);  // jsr note`, no "decompiling…". Re-run at `61b6ef2`: a note on the folded literal load `06008218` is now counted under the code: "1 note on instructions with no line of their own", and its tooltip reads `0x06008218: literal load note`. |
| 5 | Live: set a breakpoint on a `jsr @rN` whose target is computed (pseudocode shows `(*(code *)…)(…)`); let it hit | Amber hint `jsr 0x…: rN = 0x… at the halt` in the status line and on the call's line; gone after Step Into and while running | |  **Not run to completion** on macOS at `b21b311`: with Sakura Taisen 2 (SW.cue, state slot 0 of this Mac; a different build of the game from the repo savestate, the same code shifted by 0x10) the computed call `0600542A` (`mov.l @(pc),r1; mov.l @r1,r1; jsr @r1`; entry `06005408`) decompiles to `(*pcRam06000320)(0);` and a breakpoint on it, and on `060056F8`, `060058AE`, `06008CAC`, `06009610`, never hit while the game sat on its menu (frames 7700-10300), so the amber hint was not seen. Not verified. |
| 6 | Decompile a caller, then one of its callees, then the caller again | The caller is decompiled again; its call to that callee shows the callee's argument count and return | |  **Pass**: `06008204` first reads `func_0x06031598(iVar2,param_2 + iVar3,param_3);` and `iVar3 = func_0x060198de(param_2);`; after decompiling `06031598` (4 params) and `06008204` again it reads `func_0x06031598(iVar2,iVar4 + iVar3,param_3,param_2);` (4 arguments) and `iVar3 = func_0x060198de();`, and the header's third parameter is now `int4`. |
| 7 | Any function with a PC-relative load in a `bra`/`bsr` delay slot (rare; skip if none found) | No "not trustworthy" banner; the literal is the one at the branch-target-relative address | | **Pass** at `169c8d5` (arm64): fixture 3 written into HWRAM of GS-9169_007.yss. `06006000` (`bra`): no banner, `return 0x22222222;`. `06006100` (`bf/s`): banner at `0x06006104`, body reads `iRam06006110`, unfolded. `06006300` (`bsr` + `mova`): no banner, `xVar1 = func_0x06006308(); return xVar1;` |

## macOS re-test of the run-control fix (`61b6ef2`, includes `4fc3e72`)

Live Dragon Force, patched Mednafen, arm64. **Repro A** (breakpoint hit, remove it while halted, then one Continue): the
status bar read "Frame 987 | Running", the pseudocode panel "live, running", and the Call Stack "Available when execution is
paused"; frames advanced (987, 1205). **Repro B** (breakpoint in a loop at `00213516`): Continue -> BREAKPOINT HIT, Step Into ->
STEPPED (PC `00213518`), Continue -> **BREAKPOINT HIT** at `00213516` (not HALTED, not WATCHPOINT HIT). **Step Over** twice at
a breakpoint (`002135B2` -> `002135B4` -> `002135B6`): STEPPED each time. **Step Out** was greyed in that function (no return
address), so it was not exercised. **Run to Here** (row `002135B8`) halted at `002135B8`, but the banner read BREAKPOINT HIT
and a breakpoint dot stayed on that row (an ordinary breakpoint was not set by the test; this may be how the transient
is shown, and STEPPED would be the more fitting label).

### Run to Here (`b21b311`, macOS arm64)

At a halted `00213516` (Dragon Force, paused), right-click row `0021351A` -> Run to Here: the banner read **STEPPED**, PC `0021351A`, **no breakpoint dot** on the
row; Continue then ran on (frame 1088 -> 1478, status "Running", pseudocode "live, running") and did not stop at `0021351A` again.

## Findings outside the decompiler (macOS run, relayed)

- **Pause banner**: PAUSED, BREAKPOINT HIT and STEPPED read correctly at `1f8f642`. After Step Into,
  Continue to the same breakpoint read WATCHPOINT HIT with no watchpoint set (2 of 2). Fixed in
  `efbcb62`: a breakpoint owns a halt at any mirror of its address, and a stop that no breakpoint
  or armed watchpoint explains reads HALTED. Needs re-test.
- **Live run control, fixed (needs re-test):** after a user resume (Continue, Resume, Play, Step Into),
  the emulator re-reports the halt it just left for a frame or two. The app took that echo as a new
  halt: it paused again, and when the emulator then ran, nothing un-paused it, so the UI said
  "paused" over a running emulator. The mac tester diagnosed this and the planning session checked
  it against the code. The same echo explains the HALTED banner after Step Into then Continue: the
  echo of the stepped halt was presented, and the real breakpoint re-hit behind it was not.
  `StepHaltMachine::Resumed()` now marks the left halt as an echo, by sequence number, at every
  user resume. Pinned by three `StepHaltMachineTests`: a resume echo is not a halt, the re-hit
  after a step is presented, and Step Over / Out still complete through the echo.