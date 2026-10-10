# SH-2 pseudocode panel — implementation plan (Windows + macOS)

Companion to [DESIGN.md](DESIGN.md). Revision 2, after review of `dfe6d61`: the review found the
main risks in correctness and lifecycle behaviour rather than in MSVC compilation, and this
revision turns those into explicit contracts (§A) that the steps (§B) implement and test.

Supported targets are **Windows x64 (MSVC)** and **macOS (arm64 and x86_64, Apple clang)**.
Linux stays a development convenience and the web build is untouched; neither gates anything.

Rules that hold throughout:
- No change to `include/saturnexplorer/*.h`, `SE_ABI_VERSION`, Core, or the drivers.
- Ghidra sources are vendored **unmodified**, with one planned exception: the delay-slot
  SLEIGH fix in contract A3, kept as a patch file in `third_party/ghidra-decompiler/patches/`
  and recorded in `VENDOR.md`. Everything else is a subclass or wrapper in `FrontEnd/src/Decompiler/`.
- Everything decompiler-related compiles only when `SE_ENABLE_DECOMPILER` is on (default on
  desktop, forced off under Emscripten).
- Commit to master after each step, as the repo does.

---

## A. Contracts

Each contract names the behaviour, the mechanism, and the test that pins it. Steps in §B
reference them by number.

### A1. Results are invalidated by memory revision, for every source kind

A savestate or dump is editable in place (Hex Editor writes land in the context through
`se_write_vram`), and every such write bumps `se_derive_serial` without changing `SourceId`.
So a result's validity is never tied to the source kind.

- **Key.** `SnapshotKey = {sourceId, deriveSerial, frameNumber, regionSetHash}`.
  `regionSetHash` covers which regions are present and, for the optional BIOS image, its path
  and size, so adding or removing the BIOS image is a new snapshot.
- **Rule.** Before drawing, the panel recomputes the key from the displayed context. If it
  differs from the key of the shown result, the result is marked stale; on a dump, savestate or
  paused source it is re-requested immediately, on a running live source only per the refresh
  policy (A4 shows the badge meanwhile). Cached results (per entry) are kept only under the key
  they were produced with.
- **Non-memory invalidations:**
  - Function rename (`FunctionNames`): re-run the request for every shown result; names are
    engine input, not a display overlay. Cost is milliseconds.
  - User comments (`AssemblyPanel::UserComments`): display-layer only; no re-run.
  - Function-boundary correction: the entry is part of the request; the old result for that
    entry is dropped and the new entry requested.
  - BIOS image change: new `regionSetHash`, everything stale.
- **Tests.** `Sh2DecompilerTests`: decompile fixture 1, overwrite the `add #1,r0` word with
  `add #2,r0` in the snapshot, re-key, assert the new output says `+ 2`. Same with the literal
  pool word changed to point at a different registered callee. `DecompilerPanelTests`: a Hex
  Editor write through the mock backend bumps the mock's serial and the panel re-requests.

### A2. Address folding classifies the SH-2 partition before aliasing anything

The SH-2 uses bits 31–29 to select a partition. Only two of them are aliases of the bus:

| Bits 31–29 | Partition | Decompiler treatment |
|---|---|---|
| `000` | Cached bus access (`0x00000000–0x1FFFFFFF`) | Fold to the bus map below |
| `001` | Cache-through bus access (`0x20000000–0x3FFFFFFF`) | Same bus map |
| `010` | Associative purge | Reject: not memory |
| `011` | Cache address array | Reject: not memory |
| `100`, `101` | Reserved | Reject |
| `110` | Cache data array | Reject |
| `111` | On-chip peripherals / reserved | Reject |

Within the bus map, the only aliases accepted are the documented ones (`Docs/Saturn/MemoryLayout.txt`,
`SaturnMemory.txt`): HWRAM repeats every 1 MiB across `0x06000000–0x07FFFFFF`; LWRAM is the
1 MiB at `0x00200000`. Nothing else is assumed to repeat. Anything that classifies as "reject"
or lands outside a captured region raises `DataUnavailError` and the function is reported
`truncated` at that address with the partition name in the warning ("cache address array").

`MemoryBackend.cpp`'s `& 0x07FFFFFF` is not reused: it exists for panel convenience and is
broader than the hardware. The discrepancy is logged as a follow-up for that file; it does not
define decompiler correctness.

- **Tests** (`MemorySnapshotTests`, pure functions over byte vectors): `0x26004000` → the same
  bytes as `0x06004000`; `0x06104000`, `0x07F04000` → `0x06004000`; `0x66004000`,
  `0x46004000`, `0xC6004000`, `0xFFFFFE00` → rejected; `0x00300000` → rejected (no LWRAM
  repeat is assumed); a 4-byte read straddling `0x060FFFFE` into the next HWRAM repeat returns
  the wrapped bytes; a read straddling the end of LWRAM into unmapped space throws.

### A3. PC-relative loads in delay slots: detect in Step 2, fix in Step 4, never silently wrong

On SH-2 a PC-relative load or `mova` that executes in the delay slot of a taken branch sees
`PC = branch target + 2`, not its own address + 4; `Sh2DecodeAfterBranch` already models this
(`PcRelDirectAddress` vs `PcRelSlotAddress`). The Ghidra spec computes `inst_start + 4`
unconditionally, so its p-code is wrong for that case. A matching disassembly listing does not
establish correct semantics, and a ROM-hacking tool meets hand-written assembly.

- **Step 2 policy (detect, refuse to guess).** While walking the function's raw p-code (the same
  pass that finds literal pools) the engine decodes each instruction with `Sh2Decode`; when an
  instruction with `HasDelaySlot` is followed by one with `PcRel != None`, the function is marked
  `unsupported` with the address, the literal address at that site is **not** marked read-only
  (so it is not folded to a possibly wrong constant), and the panel shows a banner: "PC-relative
  load in a delay slot at 0x…: pseudocode for this function is not trustworthy." The result is
  still shown, greyed, because the rest of the function is usually fine.
- **Step 4 fix.** A SLEIGH patch in `patches/` makes the branch constructors publish their
  static target through a context register (`globalset` on the delay-slot address) and the
  `disppc2`/`disppc4` sub-constructors use it when set. This corrects `bra`/`bsr` and
  constant-target `jsr`/`jmp` slots; conditional `bt/s`/`bf/s` and register-target slots stay
  flagged as `unsupported` (the value depends on whether the branch is taken). The patch is
  reviewed against the manual, applied by the vendoring script, and the `sh-2.sla` regenerated.
- **Tests.** Synthetic fixture 3 (`spike/tools/fixture3.py`, Capstone-checked like fixture 1):
  `bra target` with `mov.l @(disp,PC),r1` in the slot, where the direct and slot-relative
  addresses hold **different** literals, plus a `bf/s` variant. Step 2 asserts `unsupported`,
  the address, and that neither literal was folded. Step 4 asserts the `bra` case decompiles to
  the slot-relative literal's value and the `bf/s` case remains flagged.

### A4. Session changes never wait on the worker; shutdown is bounded

`Stop()`-and-join on every source change would block the UI for as long as the current
decompilation takes, and Ghidra's `max_instructions` bounds instruction count, not time.

- **Session epoch.** `DecompilerRunner` has an atomic `epoch`. A source change, close or
  reconnect bumps it and clears the queue; it does not join. Every request and result carries
  the epoch it was issued under; `Poll()` drops results from an older epoch, and the worker
  skips queued requests from an older epoch before starting them.
- **Cancellation at action granularity.** Ghidra's `ActionDatabase::registerAction` accepts a
  root `ActionGroup` of our own. `CancellableRoot : ActionGroup` wraps the universal action
  and its `apply()` returns the "partial completion" code when an atomic cancel flag is set;
  `Action::perform` then returns `-1` and the worker abandons that `Funcdata` and its
  `Architecture`. Individual actions are small, so cancellation latency is milliseconds for
  normal functions. `followFlow` (before actions) is bounded by `max_instructions`. Validating
  this wrapper on the real action tree is the first task of Step 2; if it does not hold, the
  fallback below is the mechanism.
- **Fallback for a wedged worker.** If a request has not finished `T` seconds (default 5)
  after being cancelled, the runner spawns a replacement worker thread and orphans the old one
  with its engine (leaked on purpose, logged once, the entry recorded as "blocked this session"
  so it is not retried automatically). The panel shows "decompiler restarted".
- **Application exit.** `~DecompilerRunner` sets cancel + epoch, waits up to 2 s for the worker,
  then detaches it. Engine objects owned by a detached worker are leaked intentionally; nothing
  in the engine touches process state that matters at exit.
- **Tests** (`DecompilerRunnerTests`): a mock engine that blocks on a latch; bump the epoch while
  it is blocked and assert `Poll()` returns within one frame and the later result is dropped;
  a cancelled request on a real engine completes with "cancelled" and no result installed;
  the orphan path is exercised with `T` set to 50 ms; the destructor with a blocked worker
  returns within the bound. Unload/reconnect during a blocked worker is the manual check in
  Step 3.

### A5. Spec freshness covers what the application actually loads

Three copies of the spec exist: the `.slaspec` sources, the checked-in `sh-2.sla`, and the
embedded `Sh2SpecData.h`; the app reads a fourth, the files it materialises.

- **Freshness test** compiles the `.slaspec` with `se-sleighc` and asserts byte-equality with
  the checked-in `.sla` **and** with the `.sla` bytes inside `Sh2SpecData.h`; the `.ldefs`,
  `.pspec`, `.cspec` embedded copies are compared with the vendored files too.
- **Materialisation.** The target directory is
  `ConfigDir()/decompiler/<sha256 of the embedded bundle, first 16 hex>/`, so any change in
  content is a new directory and an upgrade within the same Ghidra version cannot alias a stale
  one. Files are written to `<dir>.tmp-<pid>/`, each verified by re-reading and hashing, then
  the directory is renamed into place (atomic on both platforms for a directory rename within
  the same volume); a `manifest.json` lists each file's size and hash. On open, the engine
  re-verifies the manifest; any mismatch or missing file deletes the directory and re-extracts
  once, and failing that the decompiler reports itself unavailable with the path.
- **Smoke test.** `Sh2DecompilerEngineTests` (Step 1) loads the spec **through the
  materialisation path**, not from the source tree, so CI exercises what users run.
  Additional cases: a pre-corrupted file in the directory is repaired; a directory for a
  different hash is left untouched; a read-only `ConfigDir` yields "unavailable", not a crash.

### A6. The raw MSBuild path carries zlib into the savestate driver

Gzip savestates are decoded inside `SaturnExplorerSavestateDriver`, so `FrontEnd.vcxproj`
changes do not reach it.

- `Drivers/Savestate/SaturnExplorerSavestateDriver.vcxproj` gains the zlib include path,
  `SE_HAVE_ZLIB`, and a `ProjectReference` to `SaturnExplorerZlib.vcxproj`; `FrontEnd.vcxproj`
  links zlib through that reference plus the decompiler project.
- **Verification**, both Windows build paths: `SavestateShapeTests` already synthesises
  containers; add a gzip-wrapped Mednafen container case that is skipped without
  `SE_HAVE_ZLIB` and fails if the define is set but inflate fails. On the MSBuild path, which
  has no ctest, the release checklist opens a real gzip `.mcs` state in the built exe.

### A7. CI implements the gates it is credited with

The existing workflow builds Debug with CMake and runs ctest on one Windows and one macOS
runner. That is necessary but does not cover the release batch files, the solution build, DMG
packaging, or both Mac architectures.

| Gate | How it is proven |
|---|---|
| CMake Debug build + all ctests, Windows x64 and macOS arm64 | Existing `build-and-test` job (unchanged) |
| `build-release.bat` (CMake, VS 2022, Release) | New Windows job running the batch file, asserting `build\bin\Release\SaturnExplorerFrontEnd.exe` exists |
| `build-release-msbuild.bat` (checked-in solution) | New Windows job running it, asserting the exe exists; this is also what proves A6's project wiring compiles |
| macOS x86_64 | New job on the arm64 runner with `-DCMAKE_OSX_ARCHITECTURES=x86_64` building `SaturnExplorerGhidraDecomp` and the decompiler tests only (they need no SDL2) and running them under Rosetta; the full x86_64 app is a manual, recorded build until an Intel runner is available |
| macOS bundle + DMG | New job: `-DSE_MACOS_BUNDLE_LIBS=ON`, `cmake --install`, `cpack -G DragNDrop`, assert the `.app` and `.dmg` exist and `codesign --verify --deep --strict` passes **ad-hoc signed**; Developer ID signing and notarisation stay a manual, recorded step since they need a certificate |
| The macOS frontend exists | The existing existence check is extended from `se-render` to `SaturnExplorerFrontEnd.app` |
| Timing | No hard millisecond gate. Decompiler tests get a generous ctest `TIMEOUT` (60 s) and print their elapsed times, which the step record copies into `Docs/FunctionalityVerification/` |

---

## B. Steps

### Step 1 — Vendor and build the engine (no SaturnExplorer code yet)

**Goal:** `SaturnExplorerGhidraDecomp` builds with MSVC and Apple clang on CI, and a ctest
decompiles fixture 1 through the materialised spec path (A5).

1. Vendor `FrontEnd/third_party/ghidra-decompiler/` from Ghidra 12.3 (commit `918d44e`):
   `cpp/` (the 84 CORE + DECCORE + SLEIGH sources plus `sleigh_arch`, `inject_sleigh`,
   `libdecomp`, and the compiler files `slgh_compile`, `slghparse`, `slghscan`),
   `processors/SuperH/` (five spec files), `LICENSE`, `NOTICE`, `VENDOR.md`, an empty
   `patches/` with a README, and `vendor.py` that copies the file list from a Ghidra tree and
   applies `patches/`.
2. Vendor zlib 1.3.x as `FrontEnd/third_party/zlib/` with a `SaturnExplorerZlib` static target
   that replaces `find_package(ZLIB)` on desktop; the savestate driver gets `SE_HAVE_ZLIB`
   unconditionally on desktop.
3. CMake: `option(SE_ENABLE_DECOMPILER ON)`, off under Emscripten; `SaturnExplorerGhidraDecomp`
   (C++14; MSVC `/EHsc /W3 /bigobj`, define `_WINDOWS`; clang `-Wno-sign-compare`; built `/O2`
   even in Debug); `se-sleighc` host tool; `se-regen-sla` custom target.
4. Check in `processors/SuperH/sh-2.sla` and the generated `FrontEnd/src/Decompiler/Sh2SpecData.h`
   (`binary_to_compressed_c`, as the fonts). `SaturnExplorerSlaFreshnessTests` per A5.
5. `FrontEnd/src/Decompiler/SpecBundle.{h,cpp}`: materialisation per A5 (hash directory,
   temp-and-rename, manifest, verify, repair). This is the one piece of production code in
   Step 1 because the smoke test must go through it.
6. `Sh2DecompilerEngineTests`: minimal `LoadImage` + `SleighArchitecture` subclass from the
   spike, fixture 1 as a C array, spec loaded via `SpecBundle`, asserts
   `func_0x06005000(param_1)` and `iVar1 + 1`; plus the A5 corrupt/foreign-hash/read-only cases.
7. MSBuild: `SaturnExplorerGhidraDecomp.vcxproj`, `SaturnExplorerZlib.vcxproj`, solution
   entries, `FrontEnd.vcxproj` references and `SE_ENABLE_DECOMPILER`, and the savestate project
   wiring from A6.
8. CI: the new jobs from A7 (release batch, msbuild batch, x86_64 library + tests, bundle + DMG,
   frontend existence check). They are added in this step so every later step is gated by them.
9. Docs: `BUILD.md` "Decompiler" paragraph; `third_party/imgui/VENDOR.md` backend list corrected.

**Gate:** all A7 jobs green; `SlaFreshnessTests`, `Sh2DecompilerEngineTests` and the gzip
savestate case pass on both OSes.

### Step 2 — Engine wrapper, snapshot, function finder, worker (headless)

Files in `FrontEnd/src/Decompiler/` (Win32 and desktop source lists, not the web list):

| File | Contents |
|---|---|
| `MemorySnapshot.{h,cpp}` | `SnapshotKey` per A1; regions via `ReadRegionBytes` (LWRAM, HWRAM, optional BIOS); both register files; `Classify()` and `Canonical()` per A2. |
| `CaptureLoadImage.{h,cpp}` | `LoadImage` over a snapshot; `DataUnavailError` with partition name; read-only ranges. |
| `SaturnArchitecture.{h,cpp}` | `SleighArchitecture` subclass: language id, `readonlypropagate`, `max_instructions`, loader injection, `CancellableRoot` registration (A4), spec dir from `SpecBundle`. |
| `Sh2Decompiler.{h,cpp}` | Two-pass decompile (names, known functions, `followFlow`, literal pools from raw p-code, delay-slot detection per A3, callee harvest and registration, `perform`, markup → `Line`/`Token` with addresses). All `LowlevelError`s become warnings; engine rebuilt after a hard failure. |
| `Sh2FunctionFinder.{h,cpp}` | Entry ladder (user → `FunctionNames` → call-site target → `rts` back-scan validated by `Sh2Decode` → callee) with `EntryGuess{entry, source, confidence}`; persistence in `saturn_function_bounds.txt`. |
| `DecompilerRunner.{h,cpp}` | Worker thread, epoch, cancel flag, coalescing, orphan fallback, bounded destructor, all per A4. |

First task of the step: prove or disprove `CancellableRoot` on the real action tree with a
test; record the outcome in `DESIGN.md` §6.3.

Tests: `MemorySnapshotTests` (A2), `Sh2DecompilerTests` (fixtures 1, 1b, 3 per A3, unmapped,
mirror, `mov.w` fold, token addresses, A1 edit cases), `Sh2DecompilerFixtureTests` (the `.yss`
real-code fixture: 13 literal pools, callee set, listing equal to the Capstone-verified file;
skips without game data), `Sh2FunctionFinderTests`, `DecompilerRunnerTests` (A4). All with a
60 s ctest timeout and printed timings.

**Gate:** CI green on both OSes; timings recorded.

### Step 3 — The panel and App wiring

1. `DecompilerPanel.{h,cpp}`: header (CPU combo via `ComboWidth`, entry box, Follow PC,
   Refresh, status: source kind, frame, stale/decompiling/unsupported, entry source), clipped
   line list with per-`TokenKind` colours, `RowSelectable` rows, click → `Request.jumpAssembly`,
   right-click: Rename, Set function start here, View in Hex, Copy. A3's banner and A4's
   "restarted" notice live here.
2. `App`: `Panels::decompiler`, `PanelList()` row, `BuildDefaultLayout` + `AdoptNewPanels` beside
   "SH-2 Assembly", draw after `ScopedContextSwap`, `DecompilerRunner` member, key computation
   per A1 every frame, `ResetSessionDebugState` → epoch bump (not join).
3. Entry points: Assembly context menu "Decompile here", Call Stack row "Decompile", Registers
   PC → "Decompile".
4. Refresh policy: stale-by-key is universal (A1). Dump/savestate/paused re-request at once;
   running live re-requests only on Refresh or the opt-in debounced auto-refresh and shows the
   stale badge otherwise; scrub frames keep a small LRU keyed by full `SnapshotKey`.
5. Settings `[decompiler]`: auto-refresh, BIOS image path.
6. Native menus pick the panel up through `PanelList`; verify on macOS.

Tests: `DecompilerPanelTests` on `ImGuiHarness` — click → `jumpAssembly` with the line's first
address; CPU switch keeps the result; stale key never shown; Hex Editor write re-requests (A1);
`RowSelectable` in use.

Manual pass on both OSes, recorded in `Docs/FunctionalityVerification/`: `.yss` open and edit;
live Mednafen paused, running, after a Hex Editor write, while scrubbing; unload and reconnect
while a long decompile is in flight (A4); master and slave; BIOS call; a hand-typed mirror and a
rejected cache-array address (A2); the fixture-3 delay-slot banner (A3) on a patched savestate.

**Gate:** CI green; manual checklist complete on Windows and macOS.

### Step 4 — Quality

- A3 SLEIGH patch for static-target branch slots; fixture 3 goldens flip from `unsupported` to
  correct for `bra`/`bsr`; `VENDOR.md` documents the patch and `vendor.py` applies it.
- Callee prototypes from cached callee decompiles.
- Saturn hardware map as named globals (VDP1/VDP2/SCU/SMPC/SCSP register blocks).
- Optional BIOS image region with `bios_` naming (A1 covers its invalidation).
- Register-value hints at a halt for computed `jsr @rN` targets.
- User comments shown on mapped lines (display layer, per A1).

### Step 5 — Release

- About dialog "Third-party" section (Dear ImGui, IPA Gothic, Liberation Sans, zlib, Ghidra
  decompiler + SuperH spec, Apache-2.0, NSA).
- `Integration/DISTRIBUTION.md`: `LICENSE`/`NOTICE` ship; no new dynamic libraries, so macOS
  bundling and signing are unchanged; Developer ID signing + notarisation recorded manually
  per A7.
- `DECOMPILER_GUIDE.md`, demo script, `CLAUDE.md` pointer; `VENDOR.md` bump checklist
  (`vendor.py` → build → `se-regen-sla` → regenerate `Sh2SpecData.h` → `SlaFreshnessTests` →
  review goldens).

---

## Effort and order

| Step | Rough size | Depends on |
|---|---|---|
| 1 Vendor, build, spec bundle, CI jobs | 2–3 days | — |
| 2 Engine, snapshot, finder, runner + tests | 4–5 days (A3 detection and A4 validation add about a day over revision 1) | 1 |
| 3 Panel + wiring | 3–4 days | 2 |
| 4 Quality incl. the SLEIGH patch | 3–4 days, divisible | 3 |
| 5 Release | 1 day | 3 |

Steps 1 and 2 are build and headless code and are reviewable without running the app. No UI is
written until Step 3, after the engine is proven on both platforms by CI.

## Risks and fallbacks

| Risk | Fallback |
|---|---|
| An MSVC compile error inside a vendored Ghidra file | A `patches/` entry and a `VENDOR.md` note; Ghidra builds these files with MSVC, so expect zero or one. |
| `CancellableRoot` does not interrupt the real action tree | A4's orphan-and-replace path is the mechanism; the UI never blocks either way. |
| The A3 SLEIGH context patch proves awkward | The function stays flagged `unsupported`; the Step 2 detection already guarantees nothing wrong is shown as trustworthy. |
| `filemanage.cc` directory scan on Windows paths | `startDecompilerLibrary` is pointed at the exact materialised directory; no tree scanning. |
| `/bigobj` or link-time size on MSVC Debug | The Ghidra library is built `/O2` in every configuration. |
| Config dir not writable | "Decompiler unavailable: <path>" in the panel; the rest of the app is unaffected (A5). |
| Live auto-refresh churn | Off by default; debounced; stale badge instead of silent replacement (A1/A4). |
| No Intel macOS runner | Library and tests cross-built and run under Rosetta in CI; full app build recorded manually (A7). |
