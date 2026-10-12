# SH-2 Decompiler — Step 3 (the SH-2 Pseudocode panel and App wiring)

Plan: [Docs/Decompiler/PLAN.md](../Decompiler/PLAN.md) §B Step 3, contracts A1–A4 (A7 for the gates).

**Started from:** `152bdd2` (Step 2 review closed) · **Step 3 commits:** `97c496d` (implementation) and the commit adding this record  
**Actions run:** https://github.com/NoahSteam/RomHackingTools/actions/runs/38166760462 (all six A7 jobs green at `97c496d`)  
**Gate status: NOT complete.** CI is green (above), but PLAN's manual checklist is still open on
Windows and macOS (see "Manual checklist"); only a Linux run of the real app was possible here.

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
| `DecompilerPanelTests` total | 0.63 s (2.7 s ASan, 4.1 s TSan) | 1.51 s Debug · 0.39 s Release | 2.28 s · 1.85 s at `b3760496` | 4.80 s |
| Session change with the engine held (bound: 1 s) | 0.6 ms | 1.82 ms Debug · 0.60 ms Release | 15.8 ms · 11.57 ms at `b3760496` | 44.1 ms |
| Auto-refresh captures, 90 frames, PC leaving every other frame | 3 | 3 | 3 | 3 |
| App exit (SIGTERM → `Shutdown` → runner join → exit 0), real app | 145 ms | — | — | — |

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
| `.yss` open and edit | pending | **passed** | performed on Linux (above) |
| Live Mednafen paused | pending | **unavailable** (no BIOS image or game disc on this Mac; see "Real application run (macOS)") | unavailable here (no emulator); automated with a mock (`LivePaused` cases) |
| Live Mednafen running (stale badge, Refresh, auto-refresh) | pending | **unavailable** (no BIOS image or disc) | automated with a mock (`TestRunningLive`) |
| Live, after a Hex Editor write | pending | **unavailable** (no BIOS image or disc) | automated with a mock (`TestHexWriteReRequests`) |
| Live, while scrubbing | pending | **unavailable** (no BIOS image or disc) | automated with a mock (`TestScrubCache`); App's recorded-frame identity is exercised only by a live run |
| Unload and reconnect while a long decompile is in flight (A4) | pending | **unavailable** (the in-flight race cannot be hit by hand; only the idle Close Source was observed, no hang) | automated (`TestSessionChangeDoesNotWait`, Step 2 runner tests) |
| Master and slave | pending | **passed** | performed on Linux |
| BIOS call | pending | **unavailable** (no BIOS image on this Mac) | unavailable here (no BIOS image); the key change is automated |
| Hand-typed mirror; rejected cache-array address (A2) | pending | **passed** | performed on Linux |
| Fixture-3 delay-slot banner on a patched savestate (A3) | pending | **passed** | automated on fixture 3 |
| Native menu shows Windows → Debugger → SH-2 Pseudocode | pending | **passed** | menus are built from `PanelList()`; not run here |
| The MSBuild-linked exe decompiles | pending | — | CI builds and links it (`build-release-msbuild.bat` job); no CLI to load a savestate, so it needs a manual run |

The Step 3 gate ("manual checklist complete on Windows and macOS") is therefore **not met** yet.

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

**Bugs and observations** (not fixed, per the brief)

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

The Step 3 gate is **not** complete: Windows is still pending, and the macOS live checks (6–10) and the in-flight A4 race need a
Mac with a BIOS image and a disc.

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
