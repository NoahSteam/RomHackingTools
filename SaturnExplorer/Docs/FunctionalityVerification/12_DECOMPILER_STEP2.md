# SH-2 Decompiler — Step 2 (engine wrapper, snapshot, finder, worker; headless)

Plan: [Docs/Decompiler/PLAN.md](../Decompiler/PLAN.md) §B Step 2, contracts A1–A4 (A7 for the gates).

**Verified commit:** `a570acc` (Step 2 series from `fda9455`)  
**Actions run (all six A7 jobs green):** https://github.com/NoahSteam/RomHackingTools/actions/runs/38143461729  
**Windows image:** windows-latest (VS 2026) · **macOS:** macos-latest (arm64), x86_64 under Rosetta

## What was added

| File (`FrontEnd/src/Decompiler/`) | Role |
|---|---|
| `MemorySnapshot.{h,cpp}` | Immutable LWRAM/HWRAM (+ optional BIOS) copy, both register files, `SnapshotKey` (A1). The A2 partition/fold rule itself now lives in `FrontEnd/src/Sh2Address` (moved there by `29931a2` so AI investigation shares it). |
| `CaptureLoadImage.{h,cpp}` | `ghidra::LoadImage` over a snapshot, two per Architecture: the translator's (instruction fetches) refuses uncaptured memory with `BadDataError` (flow halts there, function `truncated`); the Architecture's own (values) with `DataUnavailError`. |
| `SaturnArchitecture.{h,cpp}` | `SleighArchitecture` for `SuperH:BE:32:SH-2`, `readonlypropagate`, `max_instructions`, the cancel flag; spec directory from `SpecBundle`, once per process. |
| `Sh2Decompiler.{h,cpp}` | Two passes, literal pools from raw p-code, A3 detection, callee registration, packed markup → lines and tokens with instruction addresses. Phase observer for tests. |
| `Sh2FunctionFinder.{h,cpp}` | Entry ladder (user → symbol → call site → rts scan → callee), `CallTargetAt` (bsr, jsr through a literal), `FunctionBounds` (`saturn_function_bounds.txt`). |
| `DecompilerRunner.{h,cpp}` | The single engine thread: epoch, coalescing, cancel, budget, disable-on-unacknowledged-cancel, join + `SleighArchitecture::shutdown()` at exit. |

Targets: `SaturnExplorerMemorySnapshotTests`, `SaturnExplorerSh2FunctionFinderTests` (pure, no engine),
`SaturnExplorerSh2DecompilerTests`, `SaturnExplorerSh2DecompilerFixtureTests`,
`SaturnExplorerDecompilerRunnerTests` (120 s; the others 60 s). The desktop frontends and
`FrontEnd.vcxproj` (glob) compile the new sources; the vcxproj gained the engine and zlib include
paths and `_WINDOWS`. Fixture 3 generator: `Docs/Decompiler/spike/tools/fixture3.py` (Capstone-checked).
Patches: `0001-cancel-flag.patch` (Step 1), and from the round-2 review `0002-sleigh-reset-null-caches.patch` and `0003-translator-ownership-on-init-failure.patch` (below); no change to `include/`, `SE_ABI_VERSION`, Core or drivers.

## Contracts and the tests that pin them

| Contract | Mechanism | Test |
|---|---|---|
| A1 key | `{sourceId, deriveSerial, frame, regionSetHash}`; BIOS content hash computed once per image | `MemorySnapshotTests` TestCaptureAndKey (each component moves the key; same-size BIOS replacement is a new key); `Sh2DecompilerTests` TestEditsChangeKeyAndOutput (`add #1`→`#2` gives `+ 2`; literal repointed → new callee) |
| A2 | Partition classified before folding; only HWRAM's 1 MiB repeat folds | `MemorySnapshotTests` TestPartitions/TestFolding (all PLAN cases incl. `0x00300000`, the `0x060FFFFE` wrap, LWRAM-end straddle) |
| A3 (Step 2) | `Sh2DecodeAfterBranch` on each delayed branch's slot; literal excluded from read-only; `unsupported` + address + warning | `Sh2DecompilerTests` TestDelaySlotPcRelative (bra and bf/s; neither `0x11111111` nor `0x22222222` folded) |
| A4 | One engine thread; cancel flag; budget from `Poll`; ack limit → disabled; epoch; coalescing; join at exit | `DecompilerRunnerTests` (all cases, below) |

Each mechanism was removed in turn and its test failed (A3 literal exclusion, partition rejection,
epoch filter, coalescing, budget, disable, literal-pool skip in the rts scan). The tests are also
clean under ASan + UBSan on Linux (the only UBSan reports are signed shifts inside vendored
Ghidra: `address.hh:605`, `slghpatexpress.cc:979`).

## Timings (from the run above)

| | Windows | macOS arm64 | macOS x86_64 (Rosetta) |
|---|---|---|---|
| Engine library build (timed step) | 2 m 52 s | 1 m 10 s | 44 s |
| `DecompilerRunnerTests` | 6.6 s Debug (a tenth-size fixtures) · 14.2 s Release (full size) | 9.7 s | 12.4 s |
| `Sh2DecompilerTests` total | 350 ms | 537 ms | 442 ms |
| Fixture 1 (two passes) | 37.4 ms | 11.4 ms | 17.4 ms |
| `.yss` 2b (0x06031598, 286 bytes) | 174 ms (Debug) | 8.2 ms | 15.1 ms |
| Snapshot capture, LWRAM + HWRAM | 0.98 ms | 25.2 ms | 39.0 ms |

Runner, full-size fixtures (Windows Release · macOS arm64 · Rosetta):

| | Windows | arm64 | Rosetta |
|---|---|---|---|
| Baseline diamonds / straight / jumptable | 1061 / 936 / 713 ms | 724 / 456 / 475 ms | 1222 / 668 / 525 ms |
| Cancel at 50 % of followFlow → result (diamonds) | 11.1 ms | 13.6 ms | 18.3 ms |
| Cancel at 50 % of actions → result (straight: a long action body) | 269 ms | 250 ms | 214 ms |
| Cancel during print | completes, text unchanged | same | same |
| 100 ms budget on diamonds → TooLarge | after 204 ms | 151 ms | 256 ms |
| Epoch bump: `InvalidateSession` + `Poll` | 0.003 ms | 0.007 ms | 0.022 ms |
| Destructor with a request in actions | 21.7 ms | 10.6 ms | 22.5 ms |

Every cancel requested in followFlow was caught in followFlow and every one in actions in actions,
at the start of the phase (worker held there) and midway (phase read before and after the flag);
reruns after cancellation were byte-identical; the following request after every case was correct.

Real code (`GS-9169_007.yss`, every platform): PC → `0x06030548` and PR → `0x06031598` by the rts
scan; 2a folds `param_1 * 7000`; 2b has 13 PC-relative loads from 11 literal words (the spike's
"13 literal pools" counted loads), 8 named callees and no `(*(code *)` left; the SLEIGH listing
equals the Capstone-verified one, 143 of 143.

## Decisions and deviations

- **Uncaptured code → `BadDataError`, not `DataUnavailError`.** Flow following does not catch
  `DataUnavailError` (it would abort the whole function), and a bounded `followFlow` leaves the
  branch without a target op. PLAN.md A2 and DESIGN.md are reworded to match. Fetches and data
  reads are told apart by *who asks*: `SaturnArchitecture::buildTranslator` hands the SLEIGH
  translator its own fetch-only load image. (The first version went by request size, 16 bytes,
  which a data read can also be — review finding below.) Ghidra's bad-data allowance
  (`max_baddata`, default 4) is raised to the instruction cap, since each uncaptured branch target
  costs one.
- **Calls into uncaptured memory stay opaque** (fixture 1b: `(*(code *)0x600)(param_1)`); only
  callees whose code is captured are registered.
- **Engine "rebuild after a hard failure"** is per request: each decompilation builds and destroys
  its own Architecture (resetting the shared translator), so nothing survives an ordinary failure.
  Out of memory *inside* the engine is the exception: Ghidra is not exception-safe there (an
  injected failure left the shared translator half-reset and the next request crashed in
  `Sleigh::reset`), and the translator cannot be rebuilt from outside, so after that the
  decompiler answers Unavailable for the rest of the process.
- **Entry ladder** ranks sources for the same entry and is applied to the nearest known entry: a
  nearer symbol/call-site/callee entry starts a later function, so an older user entry further
  back does not claim it (DESIGN.md 6.4 now says so; review finding 3 was not taken).
- **Phase-boundary check-points** of our own (after flow following, after actions) so a cancel set
  late in a phase is honoured there — the engine's last check-point can come well before the
  phase ends, and the raw-p-code scan has none.
- **Windows Debug fixtures.** The Debug CRT's checked iterators make the engine ~20× slower there
  (22 s for diamonds), so the Debug runner test uses a tenth of each fixture; CI runs the same test
  at full size in Release on Windows.

## External review, round 1 (at `08dbb10`)

**Fixes verified by:** https://github.com/NoahSteam/RomHackingTools/actions/runs/38151578818 at
`5ccc5c0` — all six jobs green. In the Windows Release step the injection cases ran in full
(6 of 6 pre-engine failures reported; engine failures in child processes → Failed, then
Unavailable, snapshot references unchanged; runner construction failed at 6 points with the
engine claim released each time); in the Windows Debug step they print SKIPPED, as intended.

Every fix below has a test that was run with the fix reverted and failed.

| Finding | Fix | Test that fails without it |
|---|---|---|
| 1 Allocation failure escapes the worker | One exception boundary around the whole of `Sh2Decompiler::Decompile`, request preparation included; the worker records results inside its own boundary (a failure there disables the session); the worker's own start-up is guarded | `Sh2DecompilerTests` TestAllocationFailureIsReported (every allocation before the engine, failed in turn) and TestEngineAllocationFailureInChild; `DecompilerRunnerTests` TestWorkerAllocationFailure (child process; engine-side only -- the recording path is round 2's `--disable-oom`) |
| 2 Stale register in `CallTargetAt` | Accepts only intervening instructions proven not to touch rN: no register field naming it, no implicit r0 write (0x8xxx/0xCxxx), no branch | `Sh2FunctionFinderTests` TestCallTargets (mova, `@r1+`, branch between load and call) |
| 3 Nearer symbol beats an older user entry | Not changed: by design (see Decisions); pinned | TestLadder (older user entry + nearer symbol / callee) |
| 4 Five uncaptured branches fail the function | `max_baddata` = instruction cap | TestManyUncapturedBranches |
| 5 Timeout disable is permanent | Disable is per session: a later session re-enables once the stuck request has ended, on the same worker | TestDisableOnUnacknowledgedCancel (both orders) |
| 6 Direct CALL targets not harvested | Targets from each call spec's entry address | TestCalleeHarvest (bsr; already-known callee) |
| 7 Call through a mirror stays opaque | The callee is also registered at each alias it is called through; tokens navigate to the canonical entry | TestCalleeHarvest (0x26005000, 0x06105000) |
| 8 Immediate TooLarge skips coalescing | Same-slot queue removal and cancel happen before the early answer | TestBudget (queued request replaced by a TooLarge answer) |
| 9 Thread start failure keeps the engine claim | Claim released if `std::thread` throws | TestConstructionFailureReleasesEngine |
| 10 Architecture construction failure leaks the loader | `SaturnArchitecture` owns both load images from construction (`unique_ptr` until `buildLoader`) | (covered by ASan with the allocation sweeps; no leak reported) |
| 11 Back-scan skips the bottom candidates | Scan runs from `a - 4` down to the floor inclusive | TestScanFloor (rts at the region base, at the span floor and floor + 2) |
| 12 Failed read writes part of the buffer | Two walks: validate every span, then copy | `MemorySnapshotTests` (buffer unchanged) |
| Risk: size 16 is not only a fetch | Separate fetch and data load images (above) | TestFetchAndDataRefusals (16- and 4-byte data reads → `DataUnavailError`; fetch → `BadDataError`) |
| Gap: A3 only bra/bf/s with mov.l | `rts`+mov.w, `bsr`+mova, `jmp`+mov.l, `braf`+mov.l added | TestDelaySlotPcRelative |
| Gap: `fixture3.py` not self-checking | Checks each mnemonic and each slot load's address against Capstone; exits non-zero on any mismatch | (a deliberately wrong word makes it exit 1) |

The allocation-failure cases skip themselves in an MSVC Debug build: its STL allocates
iterator-debugging proxies inside `noexcept` container moves, so an injected failure there is
`std::terminate` by that library's design (the first CI run showed it). CI runs them on Windows in
the Release step, with the full-size runner fixtures.

Also: the epoch-bump timing assertion is 50 ms (it measured scheduler noise as well as
non-blocking behaviour); a throwing phase observer can no longer terminate from the teardown
destructor.

## External review, round 2 (at `01d12b4`)

The round-1 fixes were confirmed; finding #1 was incomplete. Each fix has a test that fails with
the fix reverted (all four run as child processes, since they leave the engine damaged; the two
reset children return normally instead of `_Exit`, so they also prove normal shutdown).

| Finding | Fix | Test that fails without it |
|---|---|---|
| 1 Shutdown after an engine OOM double-frees | Vendor patch `0002-sleigh-reset-null-caches.patch`: `Sleigh::clearForDelete` nulls the caches it deletes. Testing it found a second path, fixed by `0003-translator-ownership-on-init-failure.patch`: `restoreFromSpec` no longer holds the shared translator in a `unique_ptr` across `initialize()` | `Sh2DecompilerTests` reset children (+0: in `Sleigh::reset`; +1: in `Sleigh::initialize`), each returning normally through static destruction; `DecompilerRunnerTests` `--reset-oom` (also destroys the runner first) |
| 2 `Disassemble` bypassed the damage guard | Checked first; any non-engine exception there also marks the engine damaged | reset children: disassembly answers `<unavailable: …>` |
| 3 Disabling could allocate before clearing the queue | `DisableLocked(const char*) noexcept`: disabled flag, epoch and queue clearing first, reason best effort; the worker never dequeues while disabled | `DecompilerRunnerTests` `--disable-oom`: persistent failure while a request is being recorded, another queued -- disabled, the queued request never starts, its snapshot released |
| 4 `SetError(r, e.what())` allocated outside the guard | `const char*` and `const std::string&` overloads, both `noexcept` | `Sh2DecompilerTests` observer child: a throwing observer plus a failed message copy → Failed, nothing escapes |
| Gap: worker OOM test raced the scheduler | The worker is held at the start of the engine's work before the failure is armed | `--worker-oom` child |
| Gap: `fixture3.py` checked only mnemonics and slot addresses | Compares every decoded instruction in full -- registers, immediates, branch targets, PC-relative addresses | a wrong target, register or immediate each make it exit 1 |
| PLAN.md still promised an engine rebuild and the original ladder | PLAN.md §B Step 2 states both deviations; its patch rule lists 0002 and 0003 | — |

Not survivable, and so not tested as such: persistent allocation failure inside Ghidra's action
pipeline leaves its objects inconsistent, and their own destructors crash (seen while writing the
`--disable-oom` test, which therefore starts the failure after the engine's work). The host's
policy -- engine marked damaged, never touched again -- limits what follows; it cannot make
Ghidra's teardown exception-safe.

## Not done / outstanding

- `DecompilerPanelTests` and the A1 "Hex Editor write re-requests" case are Step 3 (panel).
- The A3 SLEIGH fix (bra/bsr slot addressing) is Step 4; fixture 3 stays `unsupported` until then.
- `SleighArchitecture::shutdown()` is a no-op in this Ghidra version; it is called as A4 says.
- `MemoryBackend.cpp`'s broader `& 0x07FFFFFF` fold remains the logged follow-up (A2).
