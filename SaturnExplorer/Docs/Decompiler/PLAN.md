# SH-2 pseudocode panel — implementation plan (Windows + macOS)

Companion to [DESIGN.md](DESIGN.md). Supported targets are **Windows x64 (MSVC)** and **macOS
(arm64 and x86_64, Apple clang)**. Linux stays a development convenience and the web build is
untouched; neither gates anything below. Every step ends with
`.github/workflows/saturn-explorer-verify.yml` (which already runs CMake + ctest on
`windows-latest` and `macos-latest`) green, because that workflow is the only place both
platforms are built on every push.

Rules that hold throughout:
- No change to `include/saturnexplorer/*.h`, `SE_ABI_VERSION`, Core, or the drivers.
- Ghidra sources are vendored **unmodified**. Anything we need is a subclass or a wrapper in
  `FrontEnd/src/Decompiler/`. If a patch to Ghidra ever becomes unavoidable it goes in
  `third_party/ghidra-decompiler/patches/` and `VENDOR.md`.
- Everything decompiler-related compiles only when `SE_ENABLE_DECOMPILER` is on, which is the
  default on desktop and forced off under Emscripten.
- Commit to master after each step, as the repo does.

---

## Step 1 — Vendor and build the engine (no SaturnExplorer code yet)

**Goal:** `SaturnExplorerGhidraDecomp` builds with MSVC and Apple clang, and a ctest decompiles
fixture 1 on both.

1. **Vendor** `FrontEnd/third_party/ghidra-decompiler/` from the Ghidra 12.3 tree (commit
   `918d44e`):
   - `cpp/`: the 84 `.cc`/`.hh` files of the Makefile's CORE, DECCORE and SLEIGH groups plus
     `sleigh_arch`, `inject_sleigh`, `libdecomp`, and the compiler files `slgh_compile`,
     `slghparse`, `slghscan` (generated parsers are upstream-checked-in; no bison/flex).
   - `processors/SuperH/`: `superh.sinc`, `sh-2.slaspec`, `superh.pspec`, `superh.cspec`,
     `superh.ldefs`.
   - `LICENSE` (Apache-2.0), `NOTICE`, `VENDOR.md` (commit, file list, bump procedure).
2. **Vendor zlib** (`FrontEnd/third_party/zlib/`, zlib 1.3.x, zlib license, ~15 C files). The
   `.sla` format is zlib-compressed, `find_package(ZLIB)` is not reliable on Windows, and the
   savestate driver's gzip path is today silently absent on Windows for the same reason. One
   static `SaturnExplorerZlib` target replaces `find_package(ZLIB)` on both platforms; the
   savestate driver gains `SE_HAVE_ZLIB` unconditionally on desktop as a side effect.
3. **CMake:**
   - `option(SE_ENABLE_DECOMPILER "Embedded Ghidra SH-2 decompiler" ON)`; `if(EMSCRIPTEN) set(... OFF)`.
   - `add_library(SaturnExplorerGhidraDecomp STATIC <84 files>)`, C++14, includes `cpp/`, links
     `SaturnExplorerZlib`. MSVC: `/EHsc /W3 /bigobj`, define `_WINDOWS` (what `filemanage.cc`
     tests for its `FindFirstFileA` directory scan); clang/GCC: `-Wno-sign-compare`. Warnings from
     this target are not promoted to errors anywhere.
   - `add_executable(se-sleighc ...)` from the same library plus the three compiler files, built for
     the host.
4. **SLEIGH spec compilation.** Check in `processors/SuperH/sh-2.sla` (13 KB, deterministic). A
   CMake custom target `se-regen-sla` rebuilds it with `se-sleighc`, and ctest
   `SaturnExplorerSlaFreshnessTests` compiles the spec to a temp file and fails if it differs
   from the checked-in one. Checking it in keeps the MSBuild path (Step 1.7) and IDE builds
   free of a host-tool dependency.
5. **Embed the five spec files** as byte arrays in a generated, checked-in
   `FrontEnd/src/Decompiler/Sh2SpecData.h`, using the same `binary_to_compressed_c` mechanism as
   `third_party/fonts`. At first use the engine writes them to
   `Settings::ConfigDir()/decompiler/ghidra-12.3/` and points `startDecompilerLibrary()` there.
   On macOS this keeps the signed `.app` untouched at runtime; on Windows the app stays a single
   `.exe`.
6. **First test:** `FrontEnd/tests/Sh2DecompilerEngineTests.cpp` — a minimal `LoadImage` +
   `SleighArchitecture` subclass (lifted from the spike), fixture 1 bytes as a C array, asserts
   the C output contains `func_0x06005000(param_1)` and `iVar1 + 1`. Registered with `add_test`
   like every other test; this is what proves MSVC and clang on CI.
7. **MSBuild path.** Add `FrontEnd/third_party/ghidra-decompiler/SaturnExplorerGhidraDecomp.vcxproj`
   and `third_party/zlib/SaturnExplorerZlib.vcxproj` (wildcard `ClCompile`, v142 toolset like the
   others) to `RomHackingTools.sln`; `FrontEnd.vcxproj` gets the two `ProjectReference`s, the
   include path, and `SE_ENABLE_DECOMPILER` in its preprocessor definitions. `FrontEnd.vcxproj`'s
   `src\**\*.cpp` wildcard picks up `Decompiler/*.cpp` automatically.
8. **Docs:** `BUILD.md` gets a "Decompiler" paragraph (the option, zlib now vendored, the
   `se-regen-sla` target); `third_party/imgui/VENDOR.md`'s stale backend list is corrected while
   the directory is being touched.

**Expected MSVC friction, budgeted for:** `__attribute__` in the bison-generated parsers is
already guarded by `#if defined __GNUC__`; `filemanage.cc` has an `_WINDOWS` branch; Ghidra ships
`win_x86_64` from these exact sources with `/EHsc /O2`, so anything beyond flags is a surprise
and is handled by a wrapper, not a patch.

**Gate:** CI green on both OSes with `Sh2DecompilerEngineTests` and `SlaFreshnessTests` passing;
`build-release.bat` and `build-release-msbuild.bat` both produce an exe; `cmake --install` +
`cpack -G DragNDrop` with `SE_MACOS_BUNDLE_LIBS=ON` still produce a signed, verifiable bundle
(codesign `--verify --deep --strict` passes; nothing new is dynamically linked).

---

## Step 2 — Engine wrapper, snapshot, function finder, worker (headless)

**Goal:** everything the panel needs, exercised by tests, with no ImGui.

Files in `FrontEnd/src/Decompiler/` (added to the Win32 and desktop source lists in
`CMakeLists.txt`, not the web list):

| File | Contents |
|---|---|
| `MemorySnapshot.{h,cpp}` | `SnapshotKey`, region copy via `ReadRegionBytes` (LWRAM, HWRAM; optional BIOS image), both SH-2 register files via `se_get_sh2_regs`, `Canonical()` folding (bits 29–31, HWRAM 1 MiB repeat). |
| `CaptureLoadImage.{h,cpp}` | `ghidra::LoadImage` over a snapshot; `DataUnavailError` outside regions; `getReadonly` from a range list. |
| `SaturnArchitecture.{h,cpp}` | `SleighArchitecture` subclass: language `SuperH:BE:32:SH-2:default`, `readonlypropagate`, `maxinstructions`, loader injection, spec-path materialisation from `Sh2SpecData.h`. |
| `Sh2Decompiler.{h,cpp}` | `Decompile(snapshot, request) → DecompileResult`: register names and known functions, `followFlow`, literal pools from raw p-code (the per-instruction constant walk from the spike), constant call-target harvest, callee registration, `perform`, XML markup → `Line`/`Token` with addresses via `beginOpMain()`. Catches all `LowlevelError`s into `warnings`; rebuilds the engine after a hard failure. |
| `Sh2FunctionFinder.{h,cpp}` | Entry ladder: user → `FunctionNames` → call-site target (`DecodeCallTarget` + `jsr`-via-literal) → `rts` back-scan with `nop`/literal skipping validated by `Sh2Decode` → callee. `EntryGuess{entry, source, confidence}`. Persistence of user corrections in `saturn_function_bounds.txt`. |
| `DecompilerRunner.{h,cpp}` | One `std::thread` owning one `Sh2Decompiler`; mutex + condvar queue, per-panel coalescing (latest request wins), `Poll()` on the UI thread installs a result only when `generation` is current **and** `SnapshotKey` matches; `Stop()` joins. Modelled on `MemorySearchRunner`, stamps as in `FrameRecorder`. |

Tests (`FrontEnd/tests/`, each its own `add_executable` + `add_test`):
- `Sh2DecompilerTests`: fixtures 1, 1b (BIOS pointer, `truncated == false`), unmapped entry →
  `noCode`, mirrored entry `0x26004000` → same text, delay-loop `mov.w` literal folds, token
  addresses of the call and return lines (`0600400E`, `06004016`).
- `Sh2DecompilerFixtureTests`: skips without game data; opens `GS-9169_007.yss` via
  `se_savestate_open_yss`, decompiles `0x06031598`, asserts 13 literal pools and the callee set,
  and compares `Sh2Decompiler::Disassemble` with the checked-in Capstone-verified listing
  (`Docs/Decompiler/spike/real_06031598_listing.txt`). Golden C output reviewed on any Ghidra bump.
- `Sh2FunctionFinderTests`: hand-built byte vectors (functions separated by literal pools and
  `nop` padding; `bsr` and `jsr`-via-literal call sites).
- `DecompilerRunnerTests`: stale key dropped, stale generation dropped, coalescing, `Stop()` joins,
  a request that throws yields a result with `warnings` and no crash.

**Gate:** all four green on both OSes; a 286-byte function decompiles in under 20 ms in Debug on
CI (sanity bound, not a benchmark).

---

## Step 3 — The panel and App wiring

**Goal:** a dockable "SH-2 Pseudocode" window with navigation both ways and correct behaviour on
every source kind.

1. `DecompilerPanel.{h,cpp}`: header row (CPU combo via `ComboWidth`, entry hex box, Follow PC,
   Refresh, status text: source kind, frame, "stale"/"decompiling…", entry source), body as a
   clipped list of lines with highlight colours per `TokenKind`, whole-row selection through
   `RowSelectable`, click → `Request.jumpAssembly{cpu, addr}`, right-click: Rename (→
   `FunctionNames`), Set function start here, View in Hex, Copy function. Hover on a line shows
   the contributing instruction addresses.
2. `App`: `Panels::decompiler`, `PanelList()` row `{"decompiler","SH-2 Pseudocode",…,"Debugger"}`,
   `BuildDefaultLayout` + `AdoptNewPanels` dock it beside "SH-2 Assembly", `BuildUI` draws it
   after the `ScopedContextSwap`, `DecompilerRunner` member, snapshot capture on request with the
   `{SourceId, se_derive_serial, se_frame_number}` key, `ResetSessionDebugState` → `Stop()`.
3. Entry points into it: Assembly panel context menu "Decompile here"; Call Stack row button
   "Decompile"; Registers panel PC → "Decompile".
4. Source-kind policy exactly as DESIGN.md §6.2: dump/savestate cache per entry; paused live
   re-snapshots on `deriveSerial`/frame change; running live refreshes only on Refresh or the
   opt-in debounced auto-refresh and shows a stale badge otherwise; scrub frames keep a small LRU.
5. Settings: panel visibility (already via `PanelList`), auto-refresh flag, BIOS image path,
   under a `[decompiler]` section.
6. Native menus pick the panel up through `PanelList` (Win32 `HMENU` and macOS `NSMenu` both
   mirror it); verify the macOS menu item toggles it.

Tests: `DecompilerPanelTests` on `ImGuiHarness` — click a line raises `jumpAssembly` with that
line's first address; CPU switch keeps the result and changes only the followed PC; a result
with a stale key is never shown; the row Selectable is the `RowSelectable` helper (same
protection the Assembly panel has).

Manual pass on both OSes, recorded in `Docs/FunctionalityVerification/`: open a `.yss`; live
Mednafen paused at a breakpoint, running, after a hex-editor write, and while scrubbing; master
and slave views; a function calling into the BIOS; a mirrored address typed by hand.

**Gate:** CI green; manual checklist complete on Windows and macOS.

---

## Step 4 — Quality

- Two-pass callee registration on by default; callee prototypes from their own decompile when
  cached.
- Saturn hardware map as named globals in the `Scope` (VDP1/VDP2/SCU/SMPC/SCSP register blocks,
  the same addresses `MemoryBackend.cpp` already lists), so `*(short *)0x25F80000` reads as
  `VDP2_TVMD`.
- Optional BIOS image region; `bios_` naming.
- Register-value hints at a halt for computed `jsr @rN` targets.
- User comments from `AssemblyPanel::UserComments()` shown as end-of-line comments on the lines
  their addresses map to.
- Fixture goldens updated and reviewed.

---

## Step 5 — Release

- About dialog: "Third-party" section (Dear ImGui, IPA Gothic, Liberation Sans, zlib, "Ghidra
  decompiler and SuperH SLEIGH specification, Apache-2.0, National Security Agency").
- `Integration/DISTRIBUTION.md`: the vendored `LICENSE`/`NOTICE` files ship with the app;
  nothing else changes for distribution (no new DLLs, no new dylibs, so the macOS bundling and
  signing steps are unchanged).
- `DECOMPILER_GUIDE.md`, a demo script, `CLAUDE.md` pointer updated.
- Tag the Ghidra commit and the zlib version in `VENDOR.md`; write the bump checklist
  (copy groups → build → `se-regen-sla` → `SlaFreshnessTests` → review goldens).

---

## Effort and order

| Step | Rough size | Depends on |
|---|---|---|
| 1 Vendor + build | 1–2 days, most of it MSVC/vcxproj/CI iteration | — |
| 2 Engine + tests | 3–4 days | 1 |
| 3 Panel + wiring | 3–4 days | 2 |
| 4 Quality | 2–3 days, divisible | 3 |
| 5 Release | 1 day | 3 |

Steps 1 and 2 are pure build and headless code and can be reviewed without running the app.
No UI is written until Step 3, after the engine is proven on both platforms by CI.

## Risks and the fallback for each

| Risk | Fallback |
|---|---|
| An MSVC compile error inside a vendored Ghidra file | A `patches/` entry with the one-line fix and a `VENDOR.md` note; Ghidra builds these files with MSVC, so expect zero or one. |
| `filemanage.cc` directory scan behaves differently on Windows paths | Point `startDecompilerLibrary` at the exact materialised directory (no scanning of a Ghidra tree), which the spike already does. |
| `/bigobj` or link-time size on MSVC Debug | Build the Ghidra library `/O2` even in Debug configurations (it is third-party code we never step into). |
| Config dir not writable (spec materialisation fails) | Engine reports "decompiler unavailable: <path>" in the panel; nothing else in the app is affected. |
| Live auto-refresh churn | Off by default; debounced; stale badge instead of silent replacement. |
