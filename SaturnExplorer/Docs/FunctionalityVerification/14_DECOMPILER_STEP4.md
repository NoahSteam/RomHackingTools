# SH-2 Decompiler — Step 4 (quality)

Plan: [Docs/Decompiler/PLAN.md](../Decompiler/PLAN.md) §B Step 4, contracts A1 and A3.

**Started from:** `ca327c0` (Step 3 gate met) · **Step 4 commits:** `1f8f642` (A3 patch 0005), `b1f21f7`
(hardware map), `169c8d5` (BIOS names), `49ab408` (register hint), `ec50e87` (Assembly notes),
`540b761` (callee prototypes), and the commit adding this record.
**Gate status: MET**: code review approved in round 2 (ChatGPT, at `8e0331b`); CI all seven A7 jobs green at `63da92e`
(run 38401021266); manual checklist complete on Windows and macOS.

Done on the way, outside the decompiler (`b187062`): `build-release-msbuild.bat` passes the toolset of
the Visual Studio it finds (2019: v142, 2022: v143; `SE_TOOLSET` overrides), and CI builds it both
ways. The Call Stack's paused strip names the cause: PAUSED, STEPPED, DMA WATCH HIT, BREAKPOINT HIT
or WATCHPOINT HIT (`StopBannerLabel`, `CallStackHeuristicTests`).

## What was added

| Item | Files | Pinned by |
|---|---|---|
| A3: `bra`/`bsr` delay slots get their real PC in the spec | `patches/0005-sleigh-delay-slot-pc.patch` (`superh.sinc`), regenerated `sh-2.sla` and `Sh2SpecData.h`; `Sh2Decompiler.cpp` (`ScanRawPcode` no longer flags those slots, except one that another branch also jumps to) | `TestDelaySlotPcRelative` (fixture 3, extended and Capstone-checked by `fixture3.py`); `SlaFreshnessTests`; `TestDelaySlotFlagged` (panel, now on the `bf/s` case) |
| Hardware registers as named globals | `Decompiler/SaturnHardwareMap.{h,cpp}` (256 registers: VDP1, VDP2, SCU, SMPC, SCSP common block; made volatile and extended later, see `15_DECOMPILER_STEP5.md` → "Hardware-register semantics") | `TestHardwareRegisterNames` |
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
| 1 | Decompile several functions that were fine in Step 3 | Same functions decompile; no new errors or warnings | **Pass** at `10cd2f5` (relayed from the Windows session): 12 savestate functions give the same text as the Step 3 build except the `bra` case (row 7); no new errors or warnings |  **Pass** at `68539ce` (arm64), savestate GS-9169_007.yss: `func_0x06030548` (`param_1 * 7000`), `06008204`, `06031598`, `0601be30` decompile, no errors or warnings (live paused: `0021a462`, `00226f74`, `0021a398` at `1f8f642`). |
| 2 | Find a function that touches VDP2/VDP1/SCU/SMPC (e.g. follow PC in a game's V-blank handler, or search the Assembly for `0x25F8`) | Registers print as `VDP2_TVMD`, `SCU_IST`, `SMPC_SF`, … instead of `*(ushort *)0x25f8…` | **Pass**: `puVar4 = &VDP2_TVMD;`, `VDP1_FBCR = 0;`, `VDP1_TVMR = 8;`, `SMPC_SF`, `SMPC_IREG0..2`, `SMPC_COMREG = 0x10`, `(&SCU_D0R)[param_2 * 8] = *param_1;` |  **Pass**: entry `06005cc8` ends with `puVar7 = &VDP2_TVMD;`; entry `0601b6f4` reads `(&SCU_D0R)[param_2 * 8] = *param_1;`, `(&SCU_D0W)`, `(&SCU_D0C)`, `(&SCU_D0AD)`, `(&SCU_D0MD)`; entry `060056e4` has `VDP1_ENDR = 0;`. |
| 3 | The panel's BIOS button → choose a BIOS image; decompile a function that calls into the BIOS | Calls read `bios_0x000006xx(...)`; clicking the call navigates; Clear BIOS makes them opaque again | **Pass**: direct BIOS call written into a savestate copy: `xVar1 = (*(code *)0x600)();` without the image, `xVar1 = bios_0x00000600();` with it; click goes to the `jsr` at `06006704`; Stop Using BIOS Image makes it opaque again |  **Pass**, with the image set through `[decompiler] bios` (the Choose BIOS dialog cannot be driven by the harness): a function written into the savestate at `06006700` (`mov.l @(pc),r0; jsr @r0` with the literal `0x600`) reads `xVar1 = bios_0x00000600();`; clicking the call selects the line and moves SH-2 Assembly to `06006702` (the `jsr`); BIOS menu → Stop Using BIOS Image makes it `xVar1 = (*(code *)0x600)();`. |
| 4 | In SH-2 Assembly, add a note on an instruction inside the shown function | The note appears as `// note` on the matching pseudocode line at once, with no "decompiling…" | **Pass**: `xVar1 = (*(code *)0x600)();  // bios call note` at once, no "decompiling…" |  **Pass**: a note on the `jsr @r0` at `0600822A` of `func_0x06008204` appeared at once as `iVar2 = func_0x06030ba0(xVar1);  // jsr note`, no "decompiling…". Re-run at `61b6ef2`: a note on the folded literal load `06008218` is now counted under the code: "1 note on instructions with no line of their own", and its tooltip reads `0x06008218: literal load note`. |
| 5 | Live: set a breakpoint on a `jsr @rN` whose target is computed (pseudocode shows `(*(code *)…)(…)`); let it hit | Amber hint `jsr 0x…: rN = 0x… at the halt` in the status line and on the call's line; gone after Step Into and while running | **Pass**: SW2 dispatcher `jsr @r2` at `060637EC`; status `jsr 0x060637EC: r2 = 0x0600580C at the halt`, call line `(*pcRam060a9460)();  // r2 = 0x0600580C at the halt`; next hit `r2 = 0x0600C010`; gone after Step Into and while running |  **Pass** at `b21b311` (arm64), Sakura Taisen 2 (SW.cue, state slot 0 of this Mac), found by the planning session's interrupt-dispatcher signature `021E 2228 8D05 4F12 4F22 420B 4F02 4F06` at `060637E2` (entry `060637E0`: `mov.l @(pc),r1; mov.l @(r0,r1),r2; tst r2,r2; bt/s; ...; jsr @r2` at `060637EC`; table at `060A9460`: `06005724`, `0600580C`). Breakpoint on `060637EC`, Continue: BREAKPOINT HIT at frame 3291, PC `060637EC`; pseudocode (entry `0606372C`, Follow PC) status line `jsr 0x060637EC: r2 = 0x0600580C at the halt` and on the call's line `(*pcRam060a9460)();  // r2 = 0x0600580C at the halt`. Step Into: STEPPED, PC `060637EE`, hint gone. Continue to the next hit (frame 3292): `r2 = 0x06005724` (the table's other entry). Run (frames 3292-3534): no hint, output stale only. |
| 6 | Decompile a caller, then one of its callees, then the caller again | The caller is decompiled again; its call to that callee shows the callee's argument count and return | **Pass**: after the callees, `iVar1 = func_0x0601be30(param_1); pxVar2 = func_0x06030ba0(iVar1); if (pxVar2 == (xunknown1 *)0x0) { func_0x060086dc(param_1,-9);` (before: `xVar1 = …; iVar2 = …; if (iVar2 == 0) { func_0x060086dc(param_1,0xfffffff7);`) |  **Pass**: `06008204` first reads `func_0x06031598(iVar2,param_2 + iVar3,param_3);` and `iVar3 = func_0x060198de(param_2);`; after decompiling `06031598` (4 params) and `06008204` again it reads `func_0x06031598(iVar2,iVar4 + iVar3,param_3,param_2);` (4 arguments) and `iVar3 = func_0x060198de();`, and the header's third parameter is now `int4`. |
| 7 | Any function with a PC-relative load in a `bra`/`bsr` delay slot (rare; skip if none found) | No "not trustworthy" banner; the literal is the one at the branch-target-relative address | **Pass** (synthetic only): `return 0x22222222;` with no banner; `bf/s` keeps its banner | **Pass** at `169c8d5` (arm64): fixture 3 written into HWRAM of GS-9169_007.yss. `06006000` (`bra`): no banner, `return 0x22222222;`. `06006100` (`bf/s`): banner at `0x06006104`, body reads `iRam06006110`, unfolded. `06006300` (`bsr` + `mova`): no banner, `xVar1 = func_0x06006308(); return xVar1;` |

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
## Windows run (relayed)

At `10cd2f5`, from the Windows session. Its own commit of this column (`7dbfc1b7`) is waiting on a
GitHub sign-in, so the results are recorded here from its report.

- **Tests:** ctest, 75 of 75 pass.
- **Build script:** `build-release-msbuild.bat` prints `=== Toolset: v143` and builds. With
  `SE_TOOLSET=v142` it prints `=== Toolset: v142`, then fails clearly with MSB8020 and exit code 1,
  because the machine has no VS 2019 tools.
- **Live-mode re-tests:** all pass.
  - Banner: PAUSED, BREAKPOINT HIT, STEPPED, then BREAKPOINT HIT again.
  - One Continue after disabling the breakpoint shows Running, and frames advance (2743 → 2940).
  - Run to Here on `060637F0` reads STEPPED, leaves no dot, and a later Continue runs on.
- **Found, older than Step 4:** starting the Windows app with any command-line argument crashed it
  (`WinMain.cpp`: `__argv` is null in a `wWinMain` program). Fixed in `5178393` and re-tested on
  Windows:
  - `SaturnExplorer.exe foo` starts and keeps running.
  - `--live` connects to Mednafen.
  - `--live nosuchendpoint_xyz` waits on that endpoint, so the argument is actually read.

## Review round 1 (ChatGPT, at `5178393`): not approved

The reviewer reported three blocking findings and two minor ones. All five are fixed, and each fix
has a regression test that fails when the fix is removed (mutation run per row).

| # | Finding | Fix | Regression |
|---|---|---|---|
| 1 (blocking) | Learned callee prototypes had no snapshot key, so an edited callee's old signature was still lent to its caller (A1). | `DecompilerPanel`: each learned signature keeps the `SnapshotKey` it was recovered from. A signature is lent (and hashed into the question) only when that key matches the snapshot held now. A result that cannot vouch for a signature (not Ok, truncated, unsupported) removes the stored one. | `TestCalleePrototypesLent`: learn, edit the callee to use r5, and the on-screen caller is asked again without the old signature (`(1,2,3)`, not `func_0x06007340(1);`). After re-learning it shows `(1,2);`. The same holds after a BIOS image change and a frame change. Mutation (lend regardless of key): 3 checks fail. |
| 2 (blocking) | A callee reached through a mirror (registered as a separate alias function) did not get the lent prototype. | `Sh2Decompiler`: the prototype is applied to every registered alias of the callee, still never to the function being decompiled. | `TestCalleePrototypes`: the same call through a literal at `06007340`, `26007340` and `06107340` all give `func_0x06007340(1);`. Mutation: the mirrors fail. |
| 3 (blocking) | The "slot is also a jump target" safeguard ignored recovered jump tables. | `ScanRawPcode` adds every recovered jump-table destination to the target set. | Fixture 3 gains a recovered switch: case 0 enters a `bra` normally and case 1 enters its slot (`06006A00`, Capstone-checked in `fixture3.py`). It is now unsupported at `06006A1A`, neither literal is folded, and no prototype is exported. |
| 4 (minor) | At a mirrored PC (`2600400E`) the computed-call hint was missing. | The hint matches tokens by the canonical instruction address; the target is still computed from the raw PC. | `TestRegisterHintAtHalt`: cache-through and repeated-HWRAM PCs give the same hint. Mutation: fails. |
| 5 (minor) | Notes and hints did not extend the horizontal scroll range. | The code view's content width includes each line's trailing hint and notes, recomputed every frame. | `TestUserCommentsOnLines`: a 400-character note raises the horizontal scroll maximum from 0 to 2426 px. Mutation: stays 0. |

Tests: full local suite 90/90, and `Sh2DecompilerTests` and `DecompilerPanelTests` clean under ASan.
CI: all seven A7 jobs green at `63da92e` (run 38401021266).

## Review round 2 (ChatGPT, at `8e0331b`): approved

The reviewer ran both affected test executables and its own five reproductions; all now pass, and
PLAN/DESIGN match the code. No further manual run was needed. The gate waited only on CI: run
38401021266 at `63da92e` completed with all seven jobs green, including Windows Release.
