# SH-2 Decompiler — Step 3 (the SH-2 Pseudocode panel and App wiring)

Plan: [Docs/Decompiler/PLAN.md](../Decompiler/PLAN.md) §B Step 3, contracts A1–A4 (A7 for the gates).

**Started from:** `152bdd2` (Step 2 review closed) · **Step 3 commits:** `97c496d` (implementation) and the commit adding this record  
**Actions run:** https://github.com/NoahSteam/RomHackingTools/actions/runs/38166760462 (all six A7 jobs green at `97c496d`)  
**Gate status: NOT complete.** CI is green (above), but PLAN's manual checklist is still open on
Windows and macOS (see "Manual checklist"); only a Linux run of the real app was possible here.
The Windows run (at `0e463ee0`, "Real application run (Windows)") failed the A4 reconnect row; the
Windows re-test at `6e8f2d06` ("After the Windows run") passed it, with one residual (bug 4), since fixed.
Every checklist row is now passed on both platforms or unavailable with a stated reason (the in-flight A4 race,
automated); whether that meets the gate is the reviewer's call.

## What was added

| File | Role |
|---|---|
| `FrontEnd/src/DecompilerPanel.{h,cpp}` | The "SH-2 Pseudocode" window: CPU combo (`ComboWidth`), Follow PC, entry box, Refresh / Stop, auto-refresh, BIOS menu; status line (source kind, frame, function, entry source); stale / decompiling badges; A3 banner, "too large", "disabled", entry errors; a clipped list of `RowSelectable` rows with per-`TokenKind` colours; row click → SH-2 Assembly at the line's first instruction; context menu: Show in SH-2 Assembly, Rename Function / Rename Callee, Set Function Start Here, View in Memory, Copy Line / Copy Function. |
| `App.{h,cpp}` | `Panels::decompiler`, `PanelList()` row (Debugger), `BuildDefaultLayout` + `AdoptNewPanels` beside SH-2 Assembly, `UpdateDecompiler` after `ScopedContextSwap` (every frame; draws when shown), `BuildDecompilerView`, one `std::unique_ptr<DecompilerRunner>` (created in `Initialize`, released first in `Shutdown`), `ResetSessionDebugState` → `InvalidateSession()` + `ResetSession()` (no join), `[decompiler] autorefresh / bios` settings, `LoadBiosImage` (hash once), recorded-frame identity, the Rename modal moved to app level. Entry points: Call Stack row menu "Decompile", Registers PC menu "Decompile". |
| `AssemblyPanel.{h,cpp}` | Row-menu item "Decompile Here" (offered only when the app has the decompiler). |
| `Debug/CallStack.{h,cpp}` | `FunctionNames::All()` and `Generation()` (bumped by rename, load, import): names are engine input (A1). |
| `FrontEnd/tests/DecompilerPanelTests.cpp` | The panel tests below. CMake target `SaturnExplorerDecompilerPanelTests` (60 s); `SE_DECOMPILER_UI_SOURCES` joins the desktop frontends only; the MSBuild project picks the panel up through its `src\**` glob. |
| `.github/workflows/saturn-explorer-verify.yml` | The panel tests join the verbose decompiler step, the Windows Release step and the Rosetta job. |

No change to `include/saturnexplorer/*.h`, `SE_ABI_VERSION`, Core, the drivers, the engine
wrapper (`FrontEnd/src/Decompiler/`) or the vendored Ghidra sources; no new patch.

## Requirements → tests

`DecompilerPanelTests` drives the real panel, the real `DecompilerRunner` and engine, and (for the
edit case) the real `HexEditorPanel`, headlessly through `ImGuiHarness` under the app's theme and
font, over a mock `IMemoryBackend` holding both work RAMs. Asynchrony is pinned by a phase gate (the
runner's phase observer holds the engine at `Init` until released) and by bounded frame loops that
stop on a condition — no sleeps.

| Requirement (PLAN Step 3 / A1–A4) | Test | Kind |
|---|---|---|
| Click → `jumpAssembly` with the line's **first instruction** address, on the panel's CPU; a whole-row `RowSelectable` (a click past the text lands) | `TestLineClickNavigates` (also View in Memory from the context menu) | automated, real clicks |
| CPU switch keeps the result; Follow PC uses the selected CPU; old output retained + stale while the other CPU's function is pending | `TestCpuSwitchKeepsOutput` | automated |
| Hex Editor write (real Memory panel typing into the mock) bumps the serial → one capture, one re-request; old output retained + stale meanwhile; then `+ 2` | `TestHexWriteReRequests` | automated, real typing |
| Install rule: older generation, older epoch, a key that is not the held snapshot's, another entry — each rejected, output retained and stale; the right answer is installed | `TestInstallRule` (each check alone, via `Accept`) | automated |
| …and end to end: the panel moves to a snapshot it cannot ask about (PC left captured memory); the outstanding answer for the old snapshot is discarded | `TestInstallRule` (last case) | automated |
| Session change: epoch bump + `ResetSession` with the engine held returns at once (0.6 ms locally); late answer dropped; next session's answer installed | `TestSessionChangeDoesNotWait` | automated |
| No repeated submissions; a failure is shown, not retried; A2 refusal before the engine (cache data array named); a typed mirror is accepted and shown canonical | `TestEntriesAndNoRetryLoops` | automated, real typing |
| A3: unsupported + slot address, output still shown (greyed) | `TestDelaySlotFlagged` (fixture 3, `bra` case) | automated |
| A1 non-memory: rename re-runs (`read_word(` appears); BIOS image → new region-set hash, and a same-size image with other bytes → another; Set Function Start Here (from the context menu) requests the new entry | `TestRenameBiosAndBounds` | automated |
| Running live: no capture as frames move (1 in 90 frames), stale badge; Refresh captures and installs the answer for the frame captured; auto-refresh waits for the PC to leave the function, debounced (3 captures in 90 frames of a PC leaving every other frame); pausing recaptures at once, once | `TestRunningLive` | automated |
| Scrub cache: revisiting a recorded frame is a cache hit (no submission); an edit to it, or the same frame number of another recording, is a new key; bounded (`kCacheSize` = 8) | `TestScrubCache` | automated |
| Disabled runner: nothing submitted, no loop, one capture; over budget → "too large to decompile within 0.05 s", not resubmitted until Refresh | `TestDisabledAndBudget` | automated |
| Snapshot copies only when the view moves (not per frame) | `Captures()` asserted in the edit, running, pause and disabled cases | automated |
| Damaged-engine refusal shown without retry | Step 2's `Sh2DecompilerTests` pins the refusal; the panel shows any non-Ok status's error and asks each question once (`TestEntriesAndNoRetryLoops`) | automated (composition) |

**Mechanisms removed in turn** (scripted, each rebuilt and run; every one made a test fail): the
generation check, the epoch check, the key check, the entry check, the immediate re-request on a
key change, "running live does not capture per frame", the auto-refresh debounce, the
one-question-once rule, the LRU, "CPU switch keeps output", stale retention while pending,
"navigate to the line's first instruction", the A2 refusal, and names being part of the question.

**Sanitizers:** the panel tests are clean under ASan + UBSan on Linux (the only report is the known
signed shift inside vendored Ghidra, `address.hh:605`, recorded in Step 2) and under ThreadSanitizer
(no warnings: the panel touches the worker only through the runner's API).

## Timings

| | Linux (this container, Debug) | Windows | macOS arm64 | macOS x86_64 (Rosetta) |
|---|---|---|---|---|
| `DecompilerPanelTests` total | 0.63 s (2.7 s ASan, 4.1 s TSan) | 1.51 s Debug · 0.39 s Release (CI) · 2.36 s Debug on a laptop at `0e463ee0` | 2.28 s · 1.85 s at `b3760496` | 4.80 s |
| Session change with the engine held (bound: 1 s) | 0.6 ms | 1.82 ms Debug · 0.60 ms Release (CI) · 2.15 ms Debug on a laptop at `0e463ee0` | 15.8 ms · 11.57 ms at `b3760496` | 44.1 ms |
| Auto-refresh captures, 90 frames, PC leaving every other frame | 3 | 3 | 3 | 3 |
| App exit (SIGTERM → `Shutdown` → runner join → exit 0), real app | 145 ms | 68 ms Debug (window close button → process gone, timed from after the click returned; no crash event) | — | — |

## Real application run (Linux, performed)

The SDL2 desktop frontend (the same `App.cpp` / `DecompilerPanel.cpp` the Windows and macOS
builds compile) was built here and driven under Xvfb with `xdotool`, on the spike's
`GS-9169_007.yss`:

- The "SH-2 Pseudocode" tab docks beside SH-2 Assembly in the default layout.
- Follow PC (master, PC `0603055A`): `func_0x06030548` found by the rts scan (60 %), decompiled
  with `param_1 * 7000` folded — the Step 2 fixture's 2a, now through the app.
- Clicking the `bVar1 = …` line turned SH-2 Assembly's Follow PC off and showed `0603054A…`.
- Slave SH-2: "No function found containing 0x003C3000…" (its PC is outside captured memory),
  previous output kept.
- Entry `C6004000` → "0xC6004000 is not code memory: … cache data array" (A2), nothing submitted;
  entry `26031598` → `func_0x06031598` (mirror shown canonical).
- Context menu on a line → Rename Function… → the shared modal → `busy_wait`: the request re-ran
  and the output says `void busy_wait(int4 param_1)`.
- Memory panel edit of the literal pool word at `0x06030566` (bytes `1B 58` → `1B 0F`): the
  output changed to `param_1 * 0x1b0f` without any other action (A1 on a savestate).
- "Decompile Here" in SH-2 Assembly, "Decompile" on Call Stack frame #1 (→ `func_0x06008204`),
  and "Decompile" on the Registers PC value all opened the panel on the right function.
- `[decompiler] autorefresh / bios` written to `settings.ini`; SIGTERM → normal shutdown, exit 0.

## Manual checklist (PLAN Step 3) — status

| Check | Windows | macOS | Notes |
|---|---|---|---|
| `.yss` open and edit | **passed** | **passed** | performed on Linux (above) |
| Live Mednafen paused | **passed** | **passed** (second run, below) | unavailable here (no emulator); automated with a mock (`LivePaused` cases) |
| Live Mednafen running (stale badge, Refresh, auto-refresh) | **passed** (the auto-refresh rate cap is only sampled, as on macOS) | **passed** (second run; the auto-refresh rate cap is only sampled, see below) | automated with a mock (`TestRunningLive`) |
| Live, after a Hex Editor write | **passed** (output changed once; the request count is not visible in the GUI) | **passed** (second run) | automated with a mock (`TestHexWriteReRequests`) |
| Live, while scrubbing | **passed**, Play From Here included (after rebuilding the patched Mednafen to the current tap, v25; the v18 build on the machine refuses it as too old) | **passed** for per-frame output and the cleared timeline; Play From Here **not exercised** (second run) | automated with a mock (`TestScrubCache`); App's recorded-frame identity is exercised only by a live run |
| Unload and reconnect while a long decompile is in flight (A4) | **passed** on the re-test at `6e8f2d06` (Disconnect, Launch while paused, Disconnect → Connect three times: no hang, no crash, a current function after each reconnect), with one residual after a relaunch (Windows bug 4, below). The in-flight race itself is **unavailable** (cannot be hit by hand). (First run at `0e463ee0`: **failed**, Windows bugs 1 and 2) | **unavailable** for the in-flight race (cannot be hit by hand); idle Close Source, Disconnect and reconnect observed: no hang, nothing carried into the new session | automated (`TestSessionChangeDoesNotWait`, Step 2 runner tests) |
| Master and slave | **passed** | **passed** | performed on Linux |
| BIOS call | **passed** for add (through the Choose BIOS Image… dialog), replace (Reload) and remove (Stop Using) while the emulator ran | **passed** for add (from settings), replace (Reload) and remove (Stop Using) while the emulator ran; the Choose BIOS Image… file dialog was not driven (second run) | unavailable here (no BIOS image); the key change is automated |
| Hand-typed mirror; rejected cache-array address (A2) | **passed** | **passed** | performed on Linux |
| Fixture-3 delay-slot banner on a patched savestate (A3) | **passed** | **passed** | automated on fixture 3 |
| Native menu shows Windows → Debugger → SH-2 Pseudocode | **passed** (the Win32/D3D11 frontend draws its menu bar with ImGui; there is no OS menu bar on Windows) | **passed** | menus are built from `PanelList()`; not run here |
| The MSBuild-linked exe decompiles | **passed** with a deviation: `build-release-msbuild.bat` fails on a VS 2022-only machine (Windows bug 3), so the same solution was built with `/p:PlatformToolset=v143` | — | CI builds and links it (`build-release-msbuild.bat` job); no CLI to load a savestate, so it needs a manual run |

The Step 3 gate ("manual checklist complete on Windows and macOS") is for the reviewer to judge: after
the re-test at `6e8f2d06` every Windows row is passed (A4 with the residual of Windows bug 4). A4's
in-flight race is unavailable on both platforms; on macOS, Play From Here was not exercised and the MSBuild
row does not apply.

## Real application run (macOS)

Run at `b3760496` (`master`), on an **arm64** Mac (Apple Silicon, native, no Rosetta), macOS 26 (Darwin 25.5.0),
by driving the real `SaturnExplorer.app` (`cmake -S SaturnExplorer -B build && cmake --build build -j8`) with
computer use, one screenshot per check. Savestate: `SakuraTaisen2/.../Yabause/OhrimeMiniGame/GS-9169_007.yss`.

**Build and tests.** The build succeeded. `ctest --test-dir build --output-on-failure`: **89 of 90 passed**; the one
failure is `SaturnExplorerDecompilerRunnerTests` (see Bug 1). `SaturnExplorerDecompilerPanelTests` passed and printed
`session change with the engine held: 11.57 ms` and `DecompilerPanelTests: 1851.0 ms total` (cells above).

**Checks performed (all on the `.yss`)**

| # | Check | Result | What the screenshot showed |
|---|---|---|---|
| 1 | Native menu, open, follow PC, edit | passed | The macOS menu bar has **Windows** → Debugger → SH-2 Pseudocode (the menu is titled "Windows", not "Window"); the item toggles the panel and its check mark follows the panel's visibility. Master PC `0603055A` gave `func_0x06030548` with `param_1 * 7000`. Memory panel: `0x06030567` `58` → `0F` (word `1B 58` → `1B 0F`) changed the output to `param_1 * 0x1b0f` with no other action. |
| 2 | Entry points | passed | Call Stack frame #1 → "Decompile" → `func_0x06008204` (Follow PC went off). Assembly row → "Decompile Here" → `func_0x06030548`. Registers PC value → "Decompile" → `func_0x06030548` (was `06008204`). Clicking the `if (0 < param_1 * 0x1b0f)` line turned Assembly's Follow PC off and jumped it to `0603054A…`. |
| 3 | Master and slave | passed | Slave: "No function found containing 0x003C3000: 0x003C3000 is not in captured memory", the previous output kept and badged `[stale]`. |
| 4 | Typed addresses (A2) | passed | `26031598` → `func_0x06031598` (canonical). `C6004000` → "0xC6004000 is not code memory: … is in the cache data array, not memory". `106004000` and `-F9FFC000` → "Type the function's entry as a hex address, e.g. 06004000." Output unchanged each time. Whether a request was submitted is not visible in the GUI (automated: `TestEntriesAndNoRetryLoops`, `TestEntryParsing`). |
| 5 | Rename | passed | Context menu → Rename Function… → `busy_wait` re-ran: `void busy_wait(int4 param_1)`, status `busy_wait @ 0x06030548`. |
| 11 | A3 banner | passed | A copy of the savestate with fixture 3's `bra` + PC-relative `mov.l` slot written into HWRAM at `06006000` (and the `bf/s` case at `06006100`; generated from the `fixture3.py` words, not committed). Entry `06006000` showed, in amber: "PC-relative load in a delay slot at 0x06006002: pseudocode for this function is not trustworthy.", "1 warning", and the output greyed (`return xRam06006010;`). |
| 13 | Hidden panel | passed (end state only) | With the panel closed, the byte at `0x06030567` was changed `58` → `0F`; reopening from the Windows menu already showed the fresh `0x1b0f`. The intermediate stale frame (one or two frames) cannot be caught at screenshot latency; it is pinned by `TestCompletedAnswerJudgedAgainstCurrentView`. |
| 14 | Quit | passed | App menu → Quit exited cleanly (no crash report; process gone within 3 s, not timed more precisely). `settings.ini` gained `[decompiler] autorefresh = 0` / `bios = ` and `decompiler = 1` in the panel list; nothing else changed. |
| 12 | A4 | unavailable | Session → Source → Close Source with the panel showing a small function: the UI did not hang; the status line went to "No source", and the old output stayed on screen badged `[stale: frame 0]`. A decompile that is still running when a session ends cannot be arranged through the GUI (the engine answers in a few milliseconds, the UI step takes seconds), so the race is not exercised here; automated: `TestSessionChangeDoesNotWait`. |
| 6–9 | Live Mednafen | unavailable | A patched Mednafen is built (`_emu/mednafen/src/mednafen`, Oct 9), but no Saturn BIOS image and no game disc/cue exist anywhere on this Mac, so nothing can be booted. |
| 10 | BIOS | unavailable | No BIOS image exists. The BIOS button's tooltip reads "No BIOS image: calls into the BIOS stop there." (the unset state). |

**Bugs and observations** (not fixed by the tester, per the brief; 1 and 2 fixed afterwards -- see "After the macOS run")

1. **`SaturnExplorerDecompilerRunnerTests` crashes on macOS arm64** (consistent, 3 of 3 serial runs; not load-related).
   Repro: `ctest --test-dir build -R DecompilerRunnerTests --output-on-failure`, or
   `build/bin/SaturnExplorerDecompilerRunnerTests <scratch dir>`: it prints `-- TestBaselines()` and dies with
   `SIGBUS` (exit 138) in 0.16 s. The crash report (`~/Library/Logs/DiagnosticReports/SaturnExplorerDecompilerRunnerTests-*.ips`)
   says `EXC_BAD_ACCESS … KERN_PROTECTION_FAILURE … "Could not determine thread index for stack guard region"`, in
   `ghidra::Heritage::renameRecurse` (recursion on the 1000-diamond fixture), i.e. a stack overflow on the engine's worker
   thread. If the app's `DecompilerRunner` thread has the same stack size as the test's, a very large function could
   crash the app the same way on macOS (default secondary-thread stacks are 512 KiB); not tried in the app.
2. Cosmetic: after "Decompile" from the Registers panel or the Call Stack, the pseudocode window's **entry box keeps the
   previously typed text** (it showed `06008204` while the function shown was `06030548`).
3. While the app's native file dialog (a spawned `osascript … choose file`) is open, **SIGTERM does not end the app**
   (the UI thread is blocked on the dialog), and the dialog outlives a SIGKILLed app. Not decompiler related; found while
   trying to open a patched savestate.
4. Checklist wording: the menu is "Windows". The raw ImGui widgets need a hover frame before a click lands, so the first
   synthetic click on a widget does nothing unless the pointer is moved onto it first (a test-driver quirk, not an app bug).

The Step 3 gate is **not** complete: Windows is still pending (see the second run below for the macOS live checks).

### Second run (live), `aa17e104`, same Mac

`git pull` brought `aa17e104`; `ctest --test-dir build -R DecompilerRunnerTests --output-on-failure` now **passes** (33.77 s), so Bug 1
above is confirmed fixed on macOS arm64. The live checks ran against the patched Mednafen (`_emu/mednafen/src/mednafen`) started with
Session → Launch on Dragon Force (USA) (`.cue`) and the BIOS images in `~/.mednafen/firmware`. SE's decompiler BIOS (`[decompiler] bios`)
was empty to start with; for check 10 it was set in `settings.ini` to a scratch copy of a BIOS (see 10).

| # | Check | Result | What was observed |
|---|---|---|---|
| 6 | Live, paused | passed | Paused at frame 3857: "live, paused · frame 3857", `func_0x002138e8` at the PC `002138E8` (entry from rts scan), output `return in_r0 + in_r3;`. |
| 7 | Live, running | passed | Resumed (Run → Resume): "live, running · frame 4010" with the amber badge `[stale: frame 3857]`. **Refresh** recaptured (`func_0x060e8a1a`, then stale from frame 6019 as the emulator moved on). With **Auto-refresh** on, captures appeared only after the PC left the shown function: stale-frame labels 8572 → 8887 (functions `0021EEBC` → `00213850`), and no change between 2-second samples while the function stayed. Samples were 2 s apart, so "at most once per 30 frames" is only bounded from below (consecutive captures were at least 48 frames apart), not measured. |
| 8 | Live, after a write | passed | Paused at frame 9696 in `func_0x0021f394`. Memory panel: byte `0x0021F8E5` `FF` → `7F` (`cmp/eq #0xFF,r0` → `#0x7F`). The panel showed `decompiling…  [stale: frame 9696]`, then the new output: `if ((iVar1 == -1) || …` became `if ((iVar1 == 0x7f) || …`; no further `decompiling…` over the next seconds. |
| 9 | Live, scrubbing | passed (partly) | Paused with 300 recorded frames. Scrubbing showed each frame's own header and function: `#5878` → `func_0x002131a0`, `#6980` → `060E8A1A`, `#7957` → `0021EEBC`, `#9035` → `060E8A1A`; the Memory panel showed the recorded frame's bytes (`FF`, not my live `7F`). Revisiting `#5878` showed its output again without a `decompiling…` badge (a cache hit is not directly visible; `TestScrubCache` pins it). Session → Source → Recording → Stop, then Start (clears the frames), resume, pause: 49 new frames (`#9698`…`#10333`) showed only their own functions, none of the old timeline. **Play From Here** could not be tried: for the frames I scrubbed to (`#5878`, `#9358`, `#9466`) its tooltip read "no savestate to restore" (48 of 300 recorded frames can be resumed); at the newest frame it was available but discards nothing, so it would not tell timelines apart. |
| 10 | BIOS | passed (set from settings, replace, remove) | With the BIOS unset, a PC in the BIOS (`0x00001D3C`) gave "No function found containing 0x00001D3C: … not in captured memory". With `[decompiler] bios` pointing at a scratch copy of `mpr-17933.bin`, the same area decompiled (`func_0x00001d38`). With the emulator running, the file was overwritten with `sega_101.bin` and BIOS ▸ **Reload BIOS Image** chosen: the log gained "Decompiler: BIOS image … mapped at 0x00000000" and the panel recaptured once (stale badge moved from `frame 1` to `frame 4643`). BIOS ▸ **Stop Using BIOS Image**: one more recapture (`stale: frame 5953`), nothing further over the next 4 s while the emulator ran past frame 6300; the BIOS button's tooltip then read "No BIOS image". The "Choose BIOS Image…" dialog (an `osascript choose file` window) cannot be driven by the harness, so adding an image at run time was not done; the initial add was from settings. The first click on **Reload** did nothing (the menu closed without acting, a pointer-timing effect of the test driver); the second worked. |
| 12 | A4, reconnect | partly | Session → Source → Disconnect (live) emptied the panels ("No data loaded"); Connect to Emulator (live) brought a fresh capture (`func_0x002273b8`, frame 7989), not the output of the previous session. No hang. The in-flight race itself (a decompile still running when the session ends) was not hit. |

Other notes: pausing from Run → Pause lands on a "BREAKPOINT HIT" banner in Call Stack (PC `002138E8`, later `0021F8E0`,
`00213248`) although this run set no breakpoint (not investigated). My first launch of the emulator also rewrote
`[controller.keys]` in `settings.ini` with default key codes; I restored it, so `settings.ini` is as it was before the run.

**After the macOS run** (fixed on Linux; `DecompilerRunnerTests` re-run on macOS arm64 at `aa17e104`: **passes**, 33.77 s, see the second run below; the entry-box fix was not re-checked in the app)

| Bug | Fix | Regression test (fails with the fix removed) |
|---|---|---|
| 1, worker stack overflow | `DecompilerRunner` starts its engine thread itself (pthread / `_beginthreadex`) with an explicit 16 MiB stack (`kWorkerStackBytes`) instead of `std::thread`, which cannot choose one. Ghidra's analysis recursion grows with the function; secondary-thread defaults are 512 KiB on macOS, 1 MiB on Windows, 8 MiB on Linux. The app's runner is the same class, so the app had the same exposure | `DecompilerRunnerTests` `TestWorkerStack`: reads the stack size from inside the worker (record hook) and requires at least `kWorkerStackBytes`. On Linux the crash reproduces by shrinking the default thread stack (`ulimit -s 128`: segfault in `TestBaselines`); with the fix the whole suite passes there. Mutation: drop `pthread_attr_setstacksize` (the test reports 128 KiB, and the suite then crashes) |
| 2, entry box | The box shows the entry of each newly installed function, deferred while the box is being edited | `DecompilerPanelTests` `TestEntryBoxFollowsShownFunction`: canonical entry after a typed mirror, the shown entry after an external `ShowFunctionContaining`, a draft kept while typing and replaced once the box is left. Mutations: never sync (fails); sync while editing (fails) |

Bug 3 (the file dialog) is outside the decompiler and Step 3; left as recorded.

Verified by https://github.com/NoahSteam/RomHackingTools/actions/runs/38286417743 (all six jobs
green at `aa17e10`, including the Windows `_beginthreadex` path in Debug and Release).

## Real application run (Windows)

Run at `0e463ee0` (`master`) on a Lenovo 81LK laptop (Intel Core i5-9300HF, 16 GB, GeForce GTX 1650), Windows 11 Home
10.0.26200, Visual Studio 2022 (MSVC 19.44.35221, Windows SDK 10.0.26100), CMake 4.4.0, by driving the real app with
computer use, one or more screenshots per check (kept with the run, not committed). Savestate:
`SakuraTaisen2/.../Yabause/OhrimeMiniGame/GS-9169_007.yss`.

**Build and tests.**
- (a) `cmake -S SaturnExplorer -B build -DBUILD_TESTING=ON`, `cmake --build build --config Debug`,
  `ctest --test-dir build -C Debug --output-on-failure`: the build succeeded (only vendored-Ghidra warnings) and **75 of 75
  tests passed** (91.7 s). Re-run verbosely for their timing lines (`ctest -R "Decompiler(Panel|Runner)Tests" -V`):
  `SaturnExplorerDecompilerPanelTests` printed `session change with the engine held: 2.15 ms`,
  `auto-refresh over 90 frames of a PC leaving every other frame: 3 captures` and `DecompilerPanelTests: 2355.4 ms total`
  (2.55 s in the full run). `SaturnExplorerDecompilerRunnerTests` passed in 9.39 s (`DecompilerRunnerTests: 9390.1 ms total`),
  printing `worker stack 16384 KiB (asked for 16384 KiB)` -- the `_beginthreadex` path of `aa17e10` -- with baselines of
  412 / 511 / 512 ms (diamonds / straight / jumptable), `InvalidateSession + Poll returned in 0.016 ms`, and the three
  injected-failure cases `SKIPPED in an MSVC Debug build` as designed.
- (b) `SaturnExplorer\build-release-msbuild.bat` **failed** (Windows bug 3: the projects ask for the VS 2019 toolset).
  For check 15 the same solution and target were built with the installed toolset:
  `MSBuild RomHackingTools.sln /t:FrontEnd /p:Configuration=Release /p:Platform=x64 /p:PlatformToolset=v143 /m`, which
  produced `x64\Release\SaturnExplorer.exe`.

**Emulator.** Live checks used the patched Mednafen in `_emu/mednafen` (Mednafen 1.32.1, Saturn Explorer tap) with the BIOS
images in `_emu/mednafen/firmware` and the disc *Sakura Wars 2 (English v1.1.0) Disc 1* (`.cue`), started from
Session → Launch. The build already on the machine (Oct 5) speaks protocol v18; checks 6–8, 10 and the first half of 9 ran
on it. Play From Here needs v22+, so the tap was rebuilt with `python Integration/install.py --incremental --yes`
(protocol v25), and Play From Here and the reconnect re-tests ran on that build. Per README → "Rewind", the rebuilt
binary's real savestate path was checked with `objdump -d`: `SsDbgSaveState` calls `MDFNSS_SaveSM`, and `SsDbgLoadState`
calls `MemoryStream(unsigned long long, int)` and `MDFNSS_LoadSM`; Play From Here then restored a frame (check 9).

**Checks**

| # | Check | Result | What the screenshot showed |
|---|---|---|---|
| 1 | `.yss` open and edit; menu | passed | The menu bar (ImGui-drawn in the Win32/D3D11 frontend) has **Windows** → Debugger → SH-2 Pseudocode, checked while the panel is open; Windows → Reset Layout docks the tab beside SH-2 Assembly. Session → Source → Load Dump → Yabause Save State… (native file dialog) opened the `.yss`. Follow PC, master PC `0603055A`: `func_0x06030548`, `param_1 * 7000`, "entry from rts scan, 60%". Memory panel: `0x06030567` `58` → `0F` changed the output to `param_1 * 0x1b0f` with no other action. |
| 2 | Entry points | passed | Call Stack frame #1 → "Decompile" → `func_0x06008204`, Follow PC went off, entry box `06008204`. SH-2 Assembly row → "Decompile Here" → `func_0x06030548`, entry box `06030548`. Back to `06008204` from the Call Stack, then Registers → SH-2 → PC value → "Decompile" → `func_0x06030548`, entry box `06008204` → `06030548`, panel brought forward. Clicking the `bVar1 = iVar2 < param_1 * 0x1b0f;` line turned Assembly's Follow PC off and jumped it to `0603054A` (tooltip `0x0603054A`). The entry box followed the shown function after every one of these (the `aa17e10` fix holds on Windows). |
| 3 | Master and slave | passed | Slave SH-2: "No function found containing 0x003C3000: 0x003C3000 is not in captured memory", previous output kept and badged `[stale]`. |
| 4 | Typed addresses (A2) | passed | `26031598` → `func_0x06031598`, entry box rewritten to `06031598`. `C6004000` → "0xC6004000 is not code memory: 0xC6004000 is in the cache data array, not memory". `106004000` and `-F9FFC000` → "Type the function's entry as a hex address, e.g. 06004000." Each refusal kept the output, badged `[stale]`. Whether a request was submitted is not visible in the GUI. |
| 5 | Rename | passed | Context menu on the signature line → Rename Function… → app-level modal "Function at 06030548" → `busy_wait`: `void busy_wait(int4 param_1)`, status `busy_wait @ 0x06030548`. |
| 13 | Hidden panel | passed (end state only) | Windows → Debugger → SH-2 Pseudocode closed the panel; Memory `0x06030567` `0F` → `58`; reopening showed `param_1 * 7000` at once. As on macOS, the intermediate frame cannot be caught at screenshot latency. |
| 11 | A3 banner | passed | A copy of the savestate with fixture 3's `bra` case at `06006000` and `bf/s` case at `06006100` written into HWRAM (the `fixture3.py` words, through the `.yss` OTHR section, 16-bit byte-swapped; not committed). Entry `06006000`: amber "PC-relative load in a delay slot at 0x06006002: pseudocode for this function is not trustworthy.", "1 warning", output greyed (`return xRam06006010;`). Entry `06006100`: the same banner for `0x06006104`. |
| 12 | A4: Close Source, Disconnect, reconnect | **failed** at `0e463ee0`; **passed** on the re-test at `6e8f2d06` (see "After the Windows run") | Close Source (savestate): no hang; "No source", old output kept as `[stale: frame 0]`. **Disconnect (live)**: the Debug app stops responding about 2 s later and never recovers (Windows bug 1; 3 of 3, on the v18 and the v25 emulator). Session → Launch while the emulator is paused at a halt hangs it the same way (3 of 3, also with the SH-2 Pseudocode panel closed); Launch while running does not (1 of 1). The MSBuild Release app survived one Disconnect → Connect, but its panel then kept the previous session's output (Windows bug 2), and the second Disconnect → Connect crashed it (access violation at the same place as the Debug hang). The in-flight race is not reachable by hand (same as macOS). |
| 6 | Live, paused | passed | Run → Pause at frame 1462: "live, paused · frame 1462", `func_0x0605e538` (entry from rts scan, 60%), PC `0605E554` inside it. |
| 7 | Live, running | passed | Resume: "live, running · frame 1645" with `[stale: frame 1462]`; still `1462` at frame 1934 (no capture while it ran). **Refresh** recaptured (`stale: frame 1967`; the game was back in the same function). **Auto-refresh** on: 2-second samples showed a new function each time the PC had moved on -- `06060AB0` → `06060C18` → `0605E538` → `0605D1B8` → `060627F0` -- with the stale label 17–20 frames behind each capture (the Debug build's decompile time). As on macOS, "at most every 30 frames" is only bounded from below by the sampling. Pausing recaptured once (`frame 3230`). |
| 8 | Live, after a Hex Editor write | passed | Paused in `func_0x0605e538`. Memory: `0x0605E555` `01` → `05` (`add #0x1,r2` → `add #0x5,r2`): `iVar1 = iVar1 + 1;` became `iVar1 = iVar1 + 5;` with no other action, and no `decompiling…` badge over the next 4 s. Exactly one re-request cannot be counted from the GUI (`TestHexWriteReRequests` pins it). The byte was restored. |
| 9 | Live, scrubbing | passed | 102 recorded frames: `#76` → `func_0x0603a4b8`, `#1350` → `func_0x06062918`, `#2056` → `func_0x06000840`, `#720` → `func_0x0606d7ec`; revisits showed their output at once with no `decompiling…` badge. A first visit was just as fast in the screenshots, so a cache hit cannot be told apart by eye (`TestScrubCache`). Recording → Stop, then Start, resume, pause: 10 new frames `#2438`…`#2549`, none of the old timeline. **Play From Here** (v25 emulator): tooltips said "3 of 66 recorded frames can be resumed"; at `#1534` it read "Restore the game to frame #1534 and resume from it. The 1 recorded frame(s) after it are discarded." Pressing it resumed the game ("live, running", `[stale: frame 1534]`). After pausing, the timeline ran `#1513`, `#1534`, then the new `#1536`, `#1542`…; the discarded `#1556` was gone, and each frame showed its own output. |
| 10 | BIOS | passed | No image: entry `00001D38` → "no code at 0x00001D38: uncaptured memory at 0x00001D38 (BIOS)"; tooltip "No BIOS image: calls into the BIOS stop there." Running with Follow PC: BIOS ▸ **Choose BIOS Image…** (native dialog) on a scratch copy of `mpr-17933.bin` → one recapture (`stale: frame 2614` → `3447`), nothing more over 4 s. File overwritten with `sega_101.bin`, BIOS ▸ **Reload** → one recapture (`decompiling…`, then `stale: frame 4945`), nothing more over 4 s. BIOS ▸ **Stop Using** → one recapture (`stale: frame 5633`), nothing more over 4 s, tooltip back to "No BIOS image". With an image set again, entry `00001D38` decompiled (`func_0x00001d38`). The first Reload attempt did nothing (the menu had not opened under the synthetic click, as on macOS); the retry worked. |
| 14 | Quit | passed | Window close button with the `.yss` loaded and the panel showing: the process was gone 68 ms after the click returned; no Application Error / WER event. `settings.ini` gained `[decompiler] autorefresh = 0` / `bios = ` and `decompiler = 1` in `[panels]` (besides `[callstack] split`, `[diff]` and `memorycompare = 0`, which are not the decompiler's). `settings.ini` and `imgui.ini` were restored from copies taken before the run. |
| 15 | MSBuild-linked exe | passed | `x64\Release\SaturnExplorer.exe` (built as in (b)) opened the `.yss` (Ctrl+O): `func_0x06030548`, `param_1 * 7000`; Windows → Debugger lists SH-2 Pseudocode. Closed cleanly (process gone in 468 ms). |

**Bugs and observations** (not fixed, per the brief)

1. **Ending a live session hangs the Debug app (and crashes the Release app).**
   Repro (Debug, `build\bin\Debug\SaturnExplorerFrontEnd.exe` at `0e463ee0`): Settings point Launch at the patched Mednafen
   and a game -- here *Sakura Wars 2* (`SAKURA TAISEN2_1.cue`). Session → Launch (F5), wait for "Connected to Emulator",
   then Session → Source → Disconnect (live). Within about 2 s the window stops responding (`Responding = False`, CPU time
   flat) and never recovers (watched 10–30 s). The same happens with F6 (pause; the Call Stack shows the halt) followed
   by F5 (Launch). Seen 3 of 3 for Disconnect (twice on the v18 tap, once on v25) and 3 of 3 for Launch-while-paused
   (v18; the third with the SH-2 Pseudocode panel closed); Launch while the game runs did not hang (1 of 1, v18).
   Repro (Release, `x64\Release\SaturnExplorer.exe`): with the emulator running, Session → Source → Disconnect, then
   Connect to Emulator (live), twice. The first cycle survived; the second crashed: `0xc0000005` at offset `0x1967a0`,
   which the PDB names as `DisplayedSnapshot+0x140` (`Drivers/Live/src/LiveDriver.cpp:425`).
   The hung UI thread (stack read with DbgHelp) is blocked in the Live driver, not the decompiler:
   `App::BuildUI` (App.cpp:1643) → `App::BuildTopBarViewModel` (App.cpp:7185) → `se_live_get_stop` (LiveDriver.cpp:2465) →
   `DisplayedSnapshot` (:425) → `Newest` (:407) → `std::lock_guard` on `LiveState::mtx`. No other thread is in the Live
   driver; the decompiler worker sits idle in its `condition_variable::wait` (DecompilerRunner.cpp:364).
   *Not verified, offered as a lead:* the call at App.cpp:7185 is reached only when
   `mEmuSlotCount > 0 && se_live_server_version(&mDataSource) >= SE_LIVE_MINVER_EMUSAVE && LiveHas(SE_LIVE_CAP_EMU_SAVE)`.
   Both `se_live_*` calls read `mDataSource` after the session was closed. If `mDataSource.user` is left pointing at the
   freed `LiveState`, the MSVC Debug heap's fill (0xDD) would read as a huge version and as a held SRW lock (a hang
   forever), while the Release heap gives an access violation once the block is reused. Mednafen has a numbered save
   slot for this game here (`_emu/mednafen/mcs/SAKURA TAISEN2_1.….mc0`), which makes the slot count non-zero; the
   macOS run, which saw no hang, may simply have had none.
2. **After reconnecting, the panel kept the previous session's output.** In the Release app, emulator running, Follow PC on:
   Disconnect, then Connect to Emulator (live). The panel showed "live, running", the red line "This source has no master
   SH-2 registers to follow." and the old session's `func_0x0605e538` badged `[stale: frame 2169]` (a frame of the earlier
   connection), unchanged for 6 s of running, although SH-2 Assembly showed PC `0605E556`. Refresh fixed it
   (`func_0x06000840`, `stale: frame 3847`). Seen once; the next attempt hit bug 1. It looks like the one capture made on
   connecting came before the first snapshot with SH-2 registers, and a running emulator does not capture again until
   Refresh.
3. **`build-release-msbuild.bat` fails on a VS 2022-only machine.** It ends with `MSB8020: The build tools for Visual Studio
   2019 (Platform Toolset = 'v142') cannot be found` for `Core\SaturnExplorer.vcxproj`, `Drivers\Common`,
   `Drivers\Live` and `FrontEnd\third_party\zlib`, then "Build FAILED". Repro: run the script on a machine with only VS 2022
   Community (vswhere finds `...\2022\Community\MSBuild\Current\Bin\MSBuild.exe`). Overriding the toolset on the command line
   (above) builds and links.
4. Observations, not decompiler bugs:
   - Pausing from Run → Pause lands on a "BREAKPOINT HIT" banner in Call Stack with no breakpoint set (as on macOS).
   - The MSBuild exe draws a different top bar from the CMake build (Session, Layers, ⏸, ⏭, State, Dump Memory…,
     Windows, Demo, More), and its "Windows"/"Demo"/"More" labels overlap the FPS / "Live connected" status text at a
     1100-px-wide window.
   - The first capture after the very first connect (boot) read "No function found containing 0x00000000".
   - Rename and the Memory panel wrote `saturn_functions.txt`, `assembly_comments.txt` and `saturn_explorer_watches.json`
     beside the savestate (in the repository's `SakuraTaisen2/...` folder); they were deleted after the run. The name
     `busy_wait` also stayed in effect after opening the patched copy from another folder.
   - The decompiler created `%APPDATA%\SaturnExplorer\decompiler\292f3f3507b05dc5` (+ `.lock`) on first use; left in place.
   - Test-driver effects: the Claude desktop app's window, hidden from the screenshots but on top, swallowed clicks on the
     right third of the screen, so Saturn Explorer was run in an 1100×830 window at the left; ImGui widgets need a hover
     frame before a synthetic click lands; one stray click hit Session → Launch and relaunched the emulator while paused,
     which is how bug 1 was first found.

**After the Windows run** (fixed in this container; needs the Windows re-test of check 12)

| Bug | Fix | Test |
|---|---|---|
| 1, hang / crash after ending a live session | Not decompiler code, and older than Step 3 (`5c30808`, `9088b1f`): `CloseSource` destroyed the context, which frees the live driver's state, but kept its copy of the data source (`mDataSource`, still pointing at that state) and the emulator slot count. `BuildTopBarViewModel` then called `se_live_server_version` / `se_live_get_stop` on it every frame -- `LiveHas` answers yes once no live source is open, so nothing stopped it -- and the driver trusts any data source it produced. PC bot's lead was right. `mDataSource` and `mEmuSlotCount` are now reset with the context, and `canSaveEmulatorState` also requires a live source | No App-level harness exists for a live session; the Windows re-run of check 12 (Disconnect, Launch while paused, Disconnect → Connect twice) is the test |
| 2, reconnect kept the old session's output | On a running emulator, Follow PC captures again once the view has registers for the followed CPU that the held snapshot lacked (the first look after connecting can come before the first register frame) | `DecompilerPanelTests` `TestRunningLiveRegistersArrive`: no registers → one capture, no request, none per frame; the other CPU's registers → nothing; the followed CPU's → one capture and the function; then no more while running. Mutation: drop the condition (fails) |

Bug 3 (`build-release-msbuild.bat` wants the VS 2019 toolset) is build configuration, not Step 3; CI's `windows-latest` still has v142, which is why CI builds it. Left as recorded.

Verified by https://github.com/NoahSteam/RomHackingTools/actions/runs/38333252775 (all six jobs
green at `6e8f2d0`). Locally: the full suite passes.

**Windows re-test of check 12** at `6e8f2d06` (`master`), same laptop, Mednafen (the rebuilt v25 tap), game, BIOS
and settings as the first run. `cmake --build build --config Debug` and the MSBuild Release exe (`/p:PlatformToolset=v143`,
as before) rebuilt; `ctest --test-dir build -C Debug --output-on-failure`: **75 of 75 passed** (89.7 s). Follow PC on
throughout, Auto-refresh off.

| Step | Result | What was observed |
|---|---|---|
| a. Debug, Disconnect while running | passed | Session → Source → Disconnect (live) at frame ~2214: "No source", the old output kept stale; the app stayed responsive for 30 s (`Responding = True`, CPU time rising), sampled every 3 s. |
| c + d. Debug, Disconnect → Connect ×3 | passed | Three cycles in a row, no hang. After each Connect, without Refresh, the panel showed "live, running" and a current function of the new session, with no register error: `func_0x06074858` (frame 2869), `func_0x0600c48c` (frame 3657), `func_0x06074350` (frame 4283). Bug 2 fixed. |
| b. Debug, F6 then F5 (Launch while paused) | passed (no hang), with a residual | Paused at frame 4608 in `func_0x06000840` ("BREAKPOINT HIT" banner), then Session → Launch: the new Mednafen started and connected; the app stayed responsive (15 s sampled). Bug 1 fixed. Residual: see bug 4. |
| 3. Release (MSBuild), Disconnect → Connect ×3 | passed | The exe auto-connected to the running emulator; three cycles, no crash, no Application Error / WER event, still responding afterwards. Each reconnect showed a current function captured in the new session: `func_0x06074350` (`stale: frame 4885`), `func_0x0605e538` (`5623`), `func_0x06074350` (`5901`). |

Bug 4 (new, residual of step b; not fixed). **After Session → Launch, the panel keeps the previous session's output until
Refresh when the new session's first PC is in the BIOS.** Repro (Debug, `6e8f2d06`, no decompiler BIOS image set, Follow PC on,
Auto-refresh off): connected and running, F6 (pause), then F5 (Session → Launch). The relaunched game boots, and the panel
shows "live, running · frame 2308", the red line "No function found containing 0x00001FF0: 0x00001FF0 is not in captured
memory", and below it the previous session's `func_0x06000840` badged `[stale: frame 4608]` -- a frame number of the old
session, higher than the new session's. It stayed like that for at least 10 s of running (to frame 2824). Refresh then
showed a current function (`func_0x060627f0`, `stale: frame 3110`). The PC really is in the BIOS at boot and there is no
image, so the error is right; what carries over is the old output and its frame label, and a running emulator does not
capture again until Refresh (Auto-refresh off). With Auto-refresh on this would presumably recover once the PC leaves
the BIOS; not tried.

`settings.ini` and `imgui.ini` were restored from the copies taken before the first run (byte-identical), and nothing was
written beside the savestate this time.

**Bug 4, fixed after the re-test:** on a running emulator, a followed PC the held snapshot found no function for is now
retried once the PC has moved, at most every 30 frames (the auto-refresh debounce) whether or not auto-refresh is on --
an error is not an answer worth keeping stable. The old session's output stays on screen, badged stale, until the new
answer arrives, as everywhere else. Test: `DecompilerPanelTests` `TestRunningLiveUnansweredFollowRetries` (PC in the
unmapped BIOS → one capture, no request; the PC unchanged → nothing; moved within the BIOS → one capture per 30 frames;
moved into code → the function, then no more captures). Mutation: drop the condition (fails). Not re-checked in the app.
Verified by https://github.com/NoahSteam/RomHackingTools/actions/runs/38338646621 (all six jobs green at `8deb03f`).

## Decisions and deviations

Recorded in PLAN.md Step 3 ("As built") and DESIGN.md §5 / §6.2:

- **Install rule** checks generation, epoch, key and entry in the panel, not only in the runner.
- **Running live:** the snapshot shown is the one captured; a Refresh answer installs although the
  emulator has moved on, labelled with its frame. A panel holding no snapshot (first look after
  connecting) captures once.
- **Auto-refresh** only with Follow PC, never over a running request, at most every 30 frames.
- **Recorded-frame identity** comes from App (per recording, per frame, per edit), because
  `ContextBackend::SourceId` moves on every scrub rebuild. A revisit still copies the snapshot to
  form its key but does not ask the engine.
- **The LRU** serves every source kind (entries and recorded frames), keyed by the full snapshot key,
  the entry and the names generation.
- **Call Stack "Decompile"** is a row context-menu item; Registers offers it on PC only.
- **The Rename modal** is drawn at app level (it lived inside the Call Stack window, so it could not
  open while the Call Stack was hidden).

## Review round 1 (ChatGPT, on `152bdd2..392cc43`)

| # | Finding | Fix | Regression test (fails with the fix removed) |
|---|---|---|---|
| 1 | Failing to start the optional worker thread threw out of `App::Initialize` and ended the app | `DecompilerRunner::Start` never throws: null plus a reason; the claim on the engine is released (already true of the constructor, now pinned); App opens without the decompiler, logs it, and the panel shows "Decompiler unavailable: …". A test-only hook makes the thread start fail as `std::thread` does (`std::system_error`) | `TestRunnerStartFailure`: Start fails with the reason, the next runner starts and is not disabled, the panel draws the unavailable state. Mutation: drop the claim release in the constructor's catch |
| 2 | `Update` judged finished answers before taking in the current view: an answer for B, completed unpolled while memory moved on to C, replaced A | `Update` now brings the view identity, names, held snapshot and wanted entry up to date first, then polls; `Accept` also requires the names generation the request was made under | `TestCompletedAnswerJudgedAgainstCurrentView`: A shown; B completes unpolled; C arrives; one Update keeps A (stale), discards B, asks for C; then `+ 3`. Same with a rename instead of an edit. Mutations: poll first; drop the names check |
| 3 | On a running emulator a BIOS change waited for Refresh | A held snapshot whose BIOS hash differs from the view's is recaptured at once on every kind of source; ordinary frame advances still are not | `TestBiosChangeWhileRunning`: add, replace, remove — one capture and one request each, the region-set hash moves each time (and returns to the original), nothing more while it runs. Mutation: drop the BIOS condition |
| 4 | Entry parsing accepted overflow and signs that wrapped into RAM (`106004000`, `-F9FFC000` → `06004000`) | `ParseHexAddress`: optional `0x`, one to eight hex digits, nothing else | `TestEntryParsing`: direct cases and both inputs typed into the box (error, no submission); `26005000` still decompiles. Mutations: allow a ninth digit; skip non-hex characters |

Verified by https://github.com/NoahSteam/RomHackingTools/actions/runs/38189199420 (all six jobs
green at `930f531`, the panel suite now 15 cases). All 20 mechanisms (the 14 above plus the six
round-1 ones) were removed in turn, each rebuilt and
run, and each made a test fail. After the fixes the panel and runner tests are clean under ASan +
UBSan (apart from the known vendored-Ghidra shift) and ThreadSanitizer. Coverage the review noted
and that stays as stated: App's recorded-frame identity is exercised only by a live run (manual
checklist); the Step 3 gate remains unmet until the Windows/macOS manual checks are done.

## Review round 2 (ChatGPT, on `8095982`)

| # | Finding | Fix | Regression test (fails with the fix removed) |
|---|---|---|---|
| 1 | Round 1's ordering held only while the window was shown: a hidden panel takes no snapshot, so an answer for B, completed unpolled while memory moved on to C, was still installed by a hidden `Update` | The install rule also requires the held snapshot to be what is on screen (`HeldIsOnScreen`: its identity equals the view's), so a hidden panel discards it without copying memory or asking anything. A running emulator is exempt by design: its held snapshot lags the view until Refresh | `TestCompletedAnswerJudgedAgainstCurrentView`, extended: E completes unpolled, memory moves to F, a hidden `Update` keeps D, discards E, captures and asks nothing; reopening asks for F once and shows `+ 6`. And a running emulator's answer is still installed while hidden. Mutations: drop the check; drop the running-live exemption |
| 2 | Ghidra's `~Funcdata` leaked the return-recovery state of an abandoned analysis | `patches/0004-funcdata-frees-active-output.patch`: `clearActiveOutput()` in the destructor (1 line plus the notice), applied by `vendor.py` -- which reproduces the vendored tree exactly with only `funcdata.cc` changing -- and recorded in `patches/README.md`, `VENDOR.md` and PLAN.md | `Sh2DecompilerTests` `TestFunctionFreesActiveOutput`: creates the state directly, watches its allocation through the test allocator, destroys the Architecture, and requires it freed. Deterministic -- it does not depend on where a cancel lands. Mutation: remove the patch's line (the test then reports it leaked) |

Verified by https://github.com/NoahSteam/RomHackingTools/actions/runs/38195479662 (all six jobs
green at `2f82f08`). Locally: `DecompilerRunnerTests` under ASan three times with no leak and no
error (it had leaked in two of three runs before), the panel and decompiler tests clean, the full
suite passing.

## Review round 3 (ChatGPT, on `f5ae399`)

| # | Finding | Fix | Regression test (fails with the fix removed) |
|---|---|---|---|
| 1 | Minor: round 2's running-live exemption also excused a BIOS change, so a hidden running panel installed an answer computed for the old BIOS image and showed it, stale, on reopening | The exemption covers only the emulator moving on: on a running emulator the held snapshot must still match the view's source identity and BIOS hash; frame number and derive serial may lag | `TestCompletedAnswerJudgedAgainstCurrentView`, extended: on a running emulator, for BIOS addition, replacement and removal in turn, an answer finished unpolled is discarded by a hidden `Update`, the old output stays, nothing is captured or asked while hidden, and reopening asks once. The plain frame-advance case (installed while hidden) is kept. Mutations: the exemption back to unconditional; drop only the BIOS-hash comparison |

Verified by https://github.com/NoahSteam/RomHackingTools/actions/runs/38200478875 (all six jobs
green at `40a2020`). Locally: the panel tests pass in the normal, ASan and TSan builds, and the
full suite passes.

## Review round 4 (ChatGPT, on `152bdd2..8deb03f`)

No production-code blocker. Verdict: gate not yet met, pending the items below.

| # | Finding | Resolution |
|---|---|---|
| 1 | Blocking: the bug-4 fix (`8deb03f`) has no real-app check | Targeted Windows re-test requested (pause → Launch, Follow PC on, Auto-refresh off, no decompiler BIOS image): recovery without Refresh, responsive, no further captures once the function is found. Result recorded below when done |
| 2 | Blocking for sign-off: PLAN/DESIGN's running-live policy did not list the two recovery captures | PLAN.md Step 3 "As built" gains the clarification (registers arriving; an unanswered PC retried after it changes, at most every 30 updates, never over a pending request; Auto-refresh off too), naming both tests; DESIGN.md 6.2's running-live row updated |
| 3 | Minor: `TestRunningLiveUnansweredFollowRetries` did not protect the debounce | The PC now moves inside the unmapped BIOS on every update for 90 updates: 2–3 captures, no request. Mutations: drop only the debounce (fails: one capture per update); drop the trigger (fails). Production code unchanged |
| -- | CI at `8deb03f` | All six jobs green: https://github.com/NoahSteam/RomHackingTools/actions/runs/38338646621 |
| -- | CI at `4fde640` (this round's changes) | All six jobs green: https://github.com/NoahSteam/RomHackingTools/actions/runs/38340559458 |

## Observations (not Step 3)

**Sanitizers on Step 2's runner test.** Re-running `DecompilerRunnerTests` under ASan after the
round-1 fixes turned up two things that predate Step 3:
- *Fixed (test harness only):* `AllocFailure.h` replaced the plain global `operator new`/`delete`
  but not the nothrow forms. libstdc++'s `stable_sort` buffer (used by Ghidra's switch printing)
  allocates through nothrow `new` and frees through the replaced `delete`, an alloc-dealloc
  mismatch. The nothrow forms are now replaced too.
- *Fixed in round 2 by a vendor patch:* vendored Ghidra's `~Funcdata` did not free `activeoutput`
  (`funcdata_varnode.cc:616`). An analysis abandoned between `ActionPrototypeTypes` and return
  recovery -- which a cancel can do -- leaked one `ParamActive` (~48 bytes); LeakSanitizer reported
  it in two of three runs, depending on where the cancel landed. See review round 2.

**Runner test under parallel load.** Locally, with `ctest -j8` on a 4-core container, Step 2's `DecompilerRunnerTests` fails "cancel at
50 % of followFlow" as inconclusive on every attempt (with or without the new test in the set):
the midpoint comes from a baseline measured under the same contention, so the spin overshoots
the phase. It passes run alone, and CI runs ctest serially. The only runner change in Step 3 is
round 1's `Start` and its thread-start test hook, which this case does not reach; left as a
follow-up for that test.
