# SH-2 decompiler — Step 5 (Release) verification

**Gate status: open.** Waiting for CI and for the Windows and macOS columns.

This record covers PLAN.md Step 5:
- the About dialog's "Third-party" section;
- every third-party licence text shipping with the binary;
- `Integration/DISTRIBUTION.md` "Shipping Saturn Explorer itself";
- `DECOMPILER_GUIDE.md`, the demo (`Docs/Decompiler/DEMO.md`) and the `CLAUDE.md` pointers;
- the `VENDOR.md` "Bumping Ghidra — checklist".

## Automated (CI)

| Check | Where |
|---|---|
| All seven licence texts beside the CMake exe | windows `build-release.bat` job |
| All seven beside the MSBuild exe, toolsets v143 and v142 | windows `build-release-msbuild.bat` matrix |
| All seven inside the installed `.app` and inside the DMG's `.app`, signature verified | macOS bundle job |
| Both bundled binaries start (`--selftest`) with Homebrew's SDL uninstalled | macOS bundle job |

The seven texts:
- `imgui/LICENSE.txt`
- `fonts/LICENSE-IPAfont.txt`
- `fonts/LICENSE-Liberation.txt`
- `zlib/LICENSE`
- `nlohmann-json/LICENSE.MIT`
- `ghidra-decompiler/LICENSE`
- `ghidra-decompiler/NOTICE`

## Manual

| # | Check | Windows | macOS |
|---|---|---|---|
| 1 | Help → About opens. Expand "Third-party": six rows (Dear ImGui, IPAGothic, Liberation Sans, zlib, JSON for Modern C++, Ghidra decompiler), with the NSA attribution sentence and the licence-folder line for this OS. The dialog fits on screen and Close works. | | |
| 2 | The folder named in row 1 exists next to the shipped app and holds the seven texts (Windows: `x64\Release\licenses`; macOS: `SaturnExplorer.app/Contents/Resources/licenses`). | | |
| 3 | `DECOMPILER_GUIDE.md`: every menu path and label it names exists in the app as written (Windows → Debugger → SH-2 Pseudocode, Decompile Here, Call Stack and Registers Decompile, Rename Callee 0x..., Set Function Start Here, the BIOS button → Choose BIOS Image..., Edit Comment). | | |
| 4 | `Docs/Decompiler/DEMO.md` run end to end on a real game: each step does what its right-hand column says. Note any step that does not, and why. | | |

## macOS run 1 (at `9639d2a`, Mac arm64, macOS 26)

The content rows (1 to 3) passed on a hand-patched copy of the bundle. The bundle the documented
recipe produces did **not start**, for three independent reasons:

| | Symptom | Cause | Fix |
|---|---|---|---|
| A | No identity given: `codesign --verify` "code has no resources but signature indicates they must be present"; launch killed (Code Signature Invalid) | `fixup_bundle` and the added Resources invalidate the linker's ad-hoc signature, and nothing re-signed it | The install step always signs, `SE_MACOS_SIGN_IDENTITY` defaulting to `-` |
| B | `-` identity: verifies, but dyld aborts: libSDL2 "not valid for use in process ... different Team IDs" | `--options runtime` turns on library validation; ad hoc has no Team ID | Hardened runtime only with a real identity |
| C | Re-signed without runtime: "Fatal error! Cannot continue! Failed loading SDL3 library." | Homebrew's sdl2 is sdl2-compat, which dlopens libSDL3; `fixup_bundle` never sees it | Detected at configure time; `libSDL3.dylib` bundled beside libSDL2 |

CI's `codesign --verify` passed on B, so A7 now also launches both bundled binaries with
`--selftest` after uninstalling Homebrew's SDL.

| Row | Result |
|---|---|
| 1 | Passed, on the patched copy. Six rows, the NSA sentence and "Licence texts: SaturnExplorer.app/Contents/Resources/licenses"; the dialog fits and Close works. |
| 2 | Passed. Exactly the seven texts, in both staged bundles. |
| 3 | Passed. Every label exists. The guide's "…" was changed to the app's ASCII "...", and BIOS is now described as a toolbar button. |
| 4 | Pending a rerun on a bundle built by the fixed recipe. Every step was exercised on the dev build earlier the same day without a disagreement. |

No Developer ID certificate was on that Mac, so notarisation was not run.
