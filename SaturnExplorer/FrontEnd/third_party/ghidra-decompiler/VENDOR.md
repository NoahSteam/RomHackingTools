# Vendored: Ghidra decompiler + SuperH SLEIGH spec

- **Upstream:** https://github.com/NationalSecurityAgency/ghidra
- **Commit:** `918d44ebc2962a53d23eeee52b8f44c7816e7815` (`master`, `application.version=12.3`,
  "DEV"; the tree the feasibility spike was run against). Pinned in `vendor.py`.
- **License:** Apache-2.0 — `LICENSE` and `NOTICE` are upstream's, copied verbatim, and must ship
  with any distribution (Docs/Decompiler/DESIGN.md §8).
- **Modified:** only by the patches in `patches/`, applied by `vendor.py`. Each patched file
  carries a "Modified for SaturnExplorer" line in its licence header (Apache-2.0 §4(b)).

Why it is here, and how it is used, is in `Docs/Decompiler/DESIGN.md` and `PLAN.md`.

## What's included

| Path | Contents |
|---|---|
| `cpp/` | From `Ghidra/Features/Decompiler/src/decompile/cpp`: the 84 sources of the Makefile's CORE + DECCORE + SLEIGH groups plus `sleigh_arch`, `inject_sleigh` and `libdecomp`; the SLEIGH compiler's `slgh_compile`, `slghparse`, `slghscan` (the bison/flex outputs upstream checks in, so neither tool is needed); and the 94 headers those reach. Not the Java-process bridge (`ghidra_*`), BFD, the XML/raw load images, the console or the tests. |
| `processors/SuperH/` | From `Ghidra/Processors/SuperH/data/languages`: `sh-2.slaspec`, `superh.sinc`, `superh.pspec`, `superh.cspec`, `superh.ldefs`. |
| `processors/SuperH/sh-2.sla` | **Generated**, checked in: `se-sleighc` output for `sh-2.slaspec`. |
| `sources.cmake` | **Generated** by `vendor.py`: the library and compiler source lists. |
| `patches/` | The upstream patches, with a README giving each one's reason and the test that pins it. |
| `vendor.py` | Copies the file set from a Ghidra checkout and applies `patches/`. |
| `CMakeLists.txt`, `SaturnExplorerGhidraDecomp.vcxproj` | The two builds of the library (the `.vcxproj` for `RomHackingTools.sln`). |

The embedded copy of the runtime files is `FrontEnd/src/Decompiler/Sh2SpecData.h`, also generated.

## Patches

| Patch | Purpose |
|---|---|
| `0001-cancel-flag.patch` | Host-owned cancellation flag and `CancelError` check-points (PLAN.md A4). |
| `0002-sleigh-reset-null-caches.patch` | `Sleigh::clearForDelete` nulls the cache pointers it deletes, so a `reset` interrupted by an allocation failure cannot make the translator's destructor delete them again at exit. |
| `0003-translator-ownership-on-init-failure.patch` | `Architecture::restoreFromSpec` assigns the translator before `initialize()` instead of holding it in a `unique_ptr`, which deleted `SleighArchitecture`'s shared translator when `initialize()` failed. |
| `0004-funcdata-frees-active-output.patch` | `Funcdata`'s destructor frees the return-recovery state (`ParamActive`) that an analysis abandoned mid-way -- a cancel -- still owns; it leaked one per such cancel. |
| `0005-sleigh-delay-slot-pc.patch` | `bra` and `bsr` give their delay slot the PC it executes at (branch target + 2), so a PC-relative load or `mova` there reads the right literal (PLAN.md A3). Changes `superh.sinc`; `sh-2.sla` and `Sh2SpecData.h` are regenerated from it. |
| `0006-sleigh-sh2-semantics.patch` | Fixes six SH-2 semantics errors in `superh.sinc`, checked against Mednafen's SH-2 core. `bsr`/`bsrf`/`jsr` now write PR before their delay slot. `MAC.L` had a wrong carry; it is rewritten as a 64-bit accumulate with 48-bit saturation. `MAC.W` now does word loads, sign-extends, and saturates at 32 bits. `XOR.B` was an AND. `mov.x Rm,@-Rn` now reads Rm before Rn is decremented. `DIV1` now reads Rm after Rn is shifted. `sh-2.sla` and `Sh2SpecData.h` are regenerated. |
| `0007-cspec-wide-return-order.patch` | `superh.cspec` put a 64-bit return's high half in r1. Ghidra's `piece1` is the most significant piece, and the big-endian SH ABI returns the high half in r0, so the join is now `piece1="r0" piece2="r1"`. `Sh2SpecData.h` is regenerated. |
| `0008-cspec-return-address.patch` | `superh.cspec` declares PR as the return address, so `jmp @rN` with rN copied from PR is a return, not an unrecovered jump table. `Sh2SpecData.h` is regenerated. |

MSVC and Apple clang needed no patch beyond these.

## Builds

- CMake: `SaturnExplorerGhidraDecomp` (object library), `se-sleighc` (host tool), `se-regen-sla`
  (regenerates the checked-in spec artifacts), all only when `SE_ENABLE_DECOMPILER` is on.
- MSBuild: `SaturnExplorerGhidraDecomp.vcxproj`, referenced by `FrontEnd.vcxproj`. It globs
  `cpp\*.cc` minus the three compiler files, which works because `vendor.py` puts nothing else
  there.
- Both link every engine object into the consumer (CMake: an OBJECT library; MSBuild:
  `UseLibraryDependencyInputs`). The engine's print languages and other capabilities register
  through static objects nothing references by name, so an ordinary library link may drop
  them, and the first `Architecture` then throws "No print languages registered".
- Both: C++14, optimised in every configuration, MSVC `/EHsc /W3 /bigobj` with `_WINDOWS`
  (upstream's own Windows define), clang/gcc with the sign-compare and similar warnings off.

## Bumping Ghidra — checklist

Each step's failure stops the bump; nothing is committed until the last step passes. Order
matters: the spec artifacts are generated from the patched `superh.sinc`, so `vendor.py` (which
applies 0005 to 0008) must run before `se-regen-sla`.

- [ ] **Check out** the new Ghidra commit. A sparse clone is enough:
  ```
  git clone --filter=blob:none --sparse https://github.com/NationalSecurityAgency/ghidra.git
  cd ghidra && git sparse-checkout set --no-cone /Ghidra/Features/Decompiler/src/decompile/cpp/ \
      /Ghidra/Processors/SuperH/data/languages/ /LICENSE /NOTICE
  git checkout <commit>
  ```
- [ ] **`vendor.py`**: update `PINNED_COMMIT` and the commit at the top of this file, then
  `python3 -I vendor.py <ghidra-checkout>`. It stops, changing nothing, if a patch no longer
  applies. Then re-base that patch by hand and re-prove it:
  - 0001: `Docs/Decompiler/spike/cancel_spike.cpp` and `DecompilerRunnerTests`;
  - 0002 to 0004: the ASan run of `DecompilerRunnerTests`;
  - 0005: `TestDelaySlotPcRelative`;
  - 0006: `SaturnExplorerSh2SemanticsTests` (runs the instructions on the p-code emulator against a
    reference model);
  - 0007: `TestPrototypeEligibility` (a 64-bit return and its caller);
  - 0008: `TestFlowUncertainty` (a return through `jmp @r1`).

  If upstream's Makefile source groups changed, update the lists in `vendor.py`.
  If `LICENSE` or `NOTICE` changed, check that the About dialog's wording still holds.
- [ ] **Build** with `SE_ENABLE_DECOMPILER=ON` on clang/gcc, and MSVC through both
  `build-release.bat` and `build-release-msbuild.bat`. A new MSVC error inside a vendored file is a
  new `patches/` entry plus a row above, never an edit in place.
- [ ] **`se-regen-sla`**: `cmake --build build --target se-regen-sla` regenerates
  `processors/SuperH/sh-2.sla` and `FrontEnd/src/Decompiler/Sh2SpecData.h` together. Commit both:
  the app embeds the header, and `SpecBundle` rewrites a user's materialised copy when its hash
  changes.
- [ ] **`SaturnExplorerSlaFreshnessTests`**: it proves the checked-in `.sla` and header match what
  this `se-sleighc` produces from the checked-in spec.
- [ ] **Engine tests**:
  - `SaturnExplorerSh2DecompilerEngineTests`;
  - `SaturnExplorerSh2DecompilerTests` (fixture 3, hardware names, BIOS names, prototypes);
  - `SaturnExplorerSh2DecompilerFixtureTests`;
  - `SaturnExplorerDecompilerRunnerTests`;
  - `SaturnExplorerDecompilerPanelTests`.
- [ ] **Review goldens**: the decompiler tests print each fixture's pseudocode. Save
  `ctest -V -R 'Sh2Decompiler|DecompilerPanel'` output before and after the bump and diff it. Accept a change only when it reads as correct for the bytes. A literal changing, or a
  flagged slot no longer flagged, is a regression even if a test was loosened to pass. Record the
  bump, with the CI run, in `Docs/FunctionalityVerification/`.
