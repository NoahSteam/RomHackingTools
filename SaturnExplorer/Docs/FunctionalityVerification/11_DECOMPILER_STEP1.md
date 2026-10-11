# SH-2 Decompiler — Step 1 (engine vendored and built)

Plan: [Docs/Decompiler/PLAN.md](../Decompiler/PLAN.md) §B Step 1, contracts A4–A7.

**Verified commit:** `47eba3e` (code), on top of the Step 1 series from `6610253`  
**Actions run (all six A7 jobs green):** https://github.com/NoahSteam/RomHackingTools/actions/runs/38136040106  
**Windows image:** windows-2025-vs2026 (Visual Studio 18 2026, MSVC 19.51) · **macOS:** macos-latest (arm64)

## Gates (A7)

| Gate | Result |
|---|---|
| CMake Debug build + all ctests, Windows x64 | **Verified** (61/61) |
| CMake Debug build + all ctests, macOS arm64 | **Verified** (72/72) |
| `build-release.bat` → `build\bin\Release\SaturnExplorerFrontEnd.exe`, licence files beside it | **Verified** |
| `build-release-msbuild.bat` → `x64\Release\SaturnExplorer.exe`, licence files beside it | **Verified** |
| macOS x86_64: engine + decompiler tests built for x86_64, run under Rosetta | **Verified** |
| macOS bundle + DMG, ad-hoc signed, `codesign --verify --deep --strict` on the installed app and inside the DMG, licence files in `Contents/Resources/licenses/` | **Verified** |
| `SaturnExplorer.app` exists (frontend existence check) | **Verified** |

## Decompiler test output (from the run above)

| | Windows | macOS arm64 |
|---|---|---|
| Engine library build (`SaturnExplorerGhidraDecomp`, timed step) | 2 m 54 s | 44 s |
| `se-sleighc` compiling `sh-2.slaspec` | 80 ms | 13 ms |
| Spec bundle materialisation (first run) | 5.0 ms | 3.1 ms |
| Fixture 1: library start + init + decompile + print | 20.8 ms | 4.7 ms |
| Fixture 1 again (same translator) | 9.3 ms | 1.8 ms |
| `Sh2DecompilerEngineTests` total | 394 ms | 95 ms |

Both platforms print, and the tests assert:
- `SlaFreshnessTests`: the freshly compiled `.sla`, the checked-in `sh-2.sla` and the embedded
  copy agree (13 051 bytes).
- Fixture 1 decompiles to `iVar1 = func_0x06005000(param_1); iVar1 = iVar1 + 1;` through the
  materialised spec directory.
- `jump-table cancel: literal read during recovery yes, CancelError reached the host`.
- `concurrent repair: 1 of 8 instances repaired`.
- `unwritable config dir: writes denied on the directory` — mode 0500 on macOS, a deny-write
  ACE on Windows; "unavailable" with the path, and a damaged bundle in a read-only config dir is
  reported, not repaired.
- `SavestateShapeTests`: a gzip-wrapped Mednafen container decodes (vendored zlib, A6).

Earlier runs measured the macOS engine build at 37 min before CI builds were bounded to
`--parallel 4` and the engine's Debug info reduced to `-g1` (fe132d6).

## Review fixes verified by this run

From the external review of `f4ef3d5..fe132d6`: CancelError swallowed by jump-table recovery
(patch 0001), spec-bundle repair following a symlink, concurrent repair deleting another
instance's verified copy, publication before verification, licence texts missing from the
shipped app, and `GenerateSpecData.cmake` ignoring a failed write. Each code fix has a test
that fails without it (see the commit messages of `c061215`, `27aa5cd`, `47eba3e`).

## Outstanding (manual, not done in Step 1)

| Check | Why it is manual | Status |
|---|---|---|
| The raw-MSBuild exe opens a real gzip Mednafen `.mcs` state (A6) | Needs a real state file and the GUI; CI only proves the project wiring compiles and links, and the CMake-built tests prove the inflate path | **Outstanding** |
| Full x86_64 macOS app build and launch (A7) | No Intel runner; CI covers the engine and its tests under Rosetta only | **Outstanding** |
| Developer ID signing and notarisation (A7) | Needs a certificate | **Outstanding** |
| The MSBuild-linked exe initialises the engine (print languages present) | The engine is first used from the app in Step 3; until then only the CMake-built test exercises engine start-up | **Outstanding until Step 3** |
