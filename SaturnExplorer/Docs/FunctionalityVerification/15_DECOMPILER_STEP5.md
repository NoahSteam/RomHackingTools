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
| 4 | Superseded by run 2. |

No Developer ID certificate was on that Mac, so notarisation was not run.

## macOS run 2 (at `33326d2`, the same Mac, the documented recipe as written, no identity)

The bundle:
- Configure reported "`/opt/homebrew/lib/libSDL2-2.0.0.dylib` is sdl2-compat; bundling `/opt/homebrew/Cellar/sdl3/3.4.14/lib/libSDL3.0.dylib`".
- `codesign --verify --deep --strict` gives "valid on disk / satisfies its Designated Requirement". The signature is ad hoc, `flags=0x2(adhoc)`, with no hardened runtime.
- `Contents/Frameworks` holds `libSDL2-2.0.0.dylib` and `libSDL3.dylib`, and the same seven licence texts are present.
- `--selftest` prints "selftest ok: SDL 2.32.70".
- `open` launches the app normally.

Row 4, DEMO.md on this bundle:
- Steps 1–5 and 10 used a `.yss` savestate.
- Steps 6–8 used live Mednafen.

| Step | Result |
|---|---|
| 1 | Passed. "dump / savestate · frame 0 · func_0x06030548 @ 0x06030548 (entry from rts scan, 60%)" |
| 2 | Passed. A line click shows its instruction in SH-2 Assembly. |
| 3 | Passed. `puVar7 = &VDP2_TVMD;` |
| 4 | Passed. Renaming a callee updates the line at once. After decompiling the callee, the caller's call shows its 4-argument signature. The name does not show in SH-2 Assembly's rows, because that panel has no label column; DEMO.md and the guide now say so. |
| 5 | Passed. `fill_block(...);  // demo note`, with no re-decompile. |
| 6 | Passed. The stale badge while running; Refresh; Auto-refresh followed the PC. |
| 7 | Passed. With a breakpoint on a dispatcher's `jsr @r2`: `(*pcRam060a9460)();  // r2 = 0x0600580C at the halt`, and the status line. DEMO.md now says to use a breakpoint, because "step until" is not reproducible. |
| 8 | Passed. With Follow PC on, the panel follows the slave's PC. DEMO.md's expected text is corrected: the output stays only with Follow PC off. |
| 9 | Partial. The BIOS popup has its three items. The OS file picker cannot be driven by the tester; the BIOS paths were covered through `settings.ini` and in the Step 3 record. |
| 10 | Passed. |

Observation, not a gate: after Reset Layout, the bottom dock is about 175 px tall on a 1470×956 logical
screen. That is about four lines, until the splitter is dragged. The split is the app-wide default
layout (`App::BuildDefaultLayout`, 24% bottom), which is not decompiler code, so it is left as a
follow-up.
