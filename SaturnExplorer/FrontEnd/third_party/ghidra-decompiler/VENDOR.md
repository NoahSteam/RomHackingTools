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

PLAN.md A3 adds a second, the delay-slot SLEIGH fix, in Step 4. MSVC and Apple clang needed no
patch beyond these.

## Builds

- CMake: `SaturnExplorerGhidraDecomp` (static), `se-sleighc` (host tool), `se-regen-sla`
  (regenerates the checked-in spec artifacts), all only when `SE_ENABLE_DECOMPILER` is on.
- MSBuild: `SaturnExplorerGhidraDecomp.vcxproj`, referenced by `FrontEnd.vcxproj`. It globs
  `cpp\*.cc` minus the three compiler files, which works because `vendor.py` puts nothing else
  there.
- Both link the library whole (`/WHOLEARCHIVE`, `-force_load`, `--whole-archive`; MSBuild's
  `UseLibraryDependencyInputs`). The engine's print languages and other capabilities register
  through static objects nothing references by name, so an ordinary library link may drop
  them, and the first `Architecture` then throws "No print languages registered".
- Both: C++14, optimised in every configuration, MSVC `/EHsc /W3 /bigobj` with `_WINDOWS`
  (upstream's own Windows define), clang/gcc with the sign-compare and similar warnings off.

## Bumping Ghidra

1. Check out the new Ghidra commit. A sparse clone is enough:
   ```
   git clone --filter=blob:none --sparse https://github.com/NationalSecurityAgency/ghidra.git
   cd ghidra && git sparse-checkout set --no-cone /Ghidra/Features/Decompiler/src/decompile/cpp/ \
       /Ghidra/Processors/SuperH/data/languages/ /LICENSE /NOTICE
   git checkout <commit>
   ```
2. Update `PINNED_COMMIT` in `vendor.py` and the commit above, then run
   `python3 -I vendor.py <ghidra-checkout>`. It stops, changing nothing, if a patch no longer
   applies; re-base the patch by hand against the new tree and re-prove it
   (`Docs/Decompiler/spike/cancel_spike.cpp`, and `DecompilerRunnerTests` once Step 2 lands).
3. If the Makefile's source groups changed, update the lists in `vendor.py`.
4. Build, then `cmake --build build --target se-regen-sla` to regenerate `sh-2.sla` and
   `Sh2SpecData.h`.
5. Run `SaturnExplorerSlaFreshnessTests` and `SaturnExplorerSh2DecompilerEngineTests`, then the
   decompiler golden tests, and review any golden-output change before committing.
