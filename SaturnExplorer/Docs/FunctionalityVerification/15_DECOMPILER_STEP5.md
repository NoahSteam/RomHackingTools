# SH-2 decompiler — Step 5 (Release) verification

**Gate status: MET.**
- CI run [38410604062](https://github.com/NoahSteam/RomHackingTools/actions/runs/38410604062) at `33326d2` passed all seven jobs, including the macOS bundle job's new `--selftest` launch without Homebrew SDL. Run 38405230972 at `9639d2a` had also passed all seven.
- The Windows column (at `9639d2a`) and macOS run 2 (at `33326d2`) are below.
- The only failures found were the bundle blockers and doc wording. All are fixed, and none were in decompiler code.

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

## Windows run (at `9639d2a`, `build-release-msbuild.bat`)

The default build printed "Toolset: v143" and built. `SE_TOOLSET=v142` printed "Toolset: v142" and then
failed with MSB8020, because this PC has no VS 2019 build tools. CI's v142 job is the proof for VS 2019,
and it was green on `9639d2a`.

| Row | Result |
|---|---|
| 1 | Passed. Six rows, the NSA sentence, and "Licence texts: the "licenses" folder beside the executable"; the dialog fits and Close works. |
| 2 | Passed. `x64\Release\licenses` holds exactly the seven texts. A clean rebuild recreated them byte-identical to the sources. |
| 3 | Passed. Registers' Decompile is on the PC value cell in the SH-2 tab, not on the "PC" label; the guide now says so. |
| 4 | Passed except steps 4 and 8, both of which were wording. Steps 1–3, 5 and 7 passed, as did 9 (`bios_0x00000600()`, and back to `(*(code *)0x600)()` on Stop Using BIOS Image) and 10. Step 4: the rename and the recovered signature work. The name was absent from SH-2 Assembly, which has no label column, and from a Call Stack frame showing "Func unknown". Call Stack reads the same names, but only for frames whose function it has identified. DEMO.md and the guide now say both. Step 8: the same Follow PC wording as macOS, now corrected. |

Observations outside Step 5, left as follow-ups:
- the MSBuild build's About shows "Build: unknown", because no commit is stamped;
- with a savestate open, the live Connect reported "No compatible live emulator endpoint was found" until Close Source;
- the Rename dialog's text box does not take focus on open.

## Follow-ups fixed after the gate

The four follow-ups from the Windows and macOS runs, fixed after the gate was met:
- **Bottom dock.** Reset Layout gives the debugger strip at least 18 lines, capped at 45% of the window (`BottomDockFraction`, `TestBottomDockFraction`). A tall screen keeps 24%.
- **"Build: unknown" (MSBuild).** `FrontEnd.vcxproj` writes `BuildInfo.h` from git before compiling, as the CMake build does. CI greps the MSBuild exe for the commit.
- **Connect with a savestate open.** A user's Connect now replaces the open source. It was refused and reported as "No compatible live emulator endpoint". Auto-connect still never replaces a source. A source opened during the attempt is kept, with its own message (`LiveOpenMayAttach`, `TestLiveOpenMayAttach`, mutation-checked). Launch with a source open now says why it did not connect.
- **Rename dialog.** The name box takes focus when the dialog opens, with the old name selected.

### Follow-up round 2 (macOS at `fecdac6`)

What passed:
- Connect over an open savestate replaced it and went live. The pseudocode re-keyed, and an entry that was stale for the new state showed `halt_baddata()` rather than a stale function: A1 working as specified.
- The rename box takes focus with the old name selected.
- About shows "Build: fecdac62d0".

What was fixed:
- **Bottom dock.** It showed 12 output lines, not the intended number, because the first version counted the panel's tab bar, toolbar and status line as output. `BottomDockFraction` now asks for 16 output lines plus 5 lines of panel chrome, capped at half the window, and its test uses the Mac's measured line height.
- **Escape on the rename dialog.** It did not cancel, because the active text field consumes Escape. The dialog body is now `NameDialogBody` in `PanelWidgets.h`, which also treats Escape as Cancel. `TestNameDialogEnterAppliesTypedName` and `TestNameDialogEscapeCancels` cover it. Removing the Escape check, or the focus-on-open, fails them. A redundant AutoSelectAll flag, which no test could tell apart, was dropped: focus set from code already selects the text.

### Follow-up round 3 (macOS at `16a70c0`)

The rename dialog passed: Escape closes it without renaming, and Enter applies. The bottom strip showed
14 full output lines of the 16 targeted. The panel's chrome measured about 7 lines rather than 5, so
`BottomDockFraction` now allows 7, which gives about 0.46 of the window on this screen.

### Follow-up round 4 (macOS at `de8b136`): the result

The Mac still showed 14 lines, the same as before. Its measurement showed why: the strip already sits
on the 50% cap. The dock area is about 735 px in its screenshot space and the strip about 365 px, so
the chrome allowance changes nothing on that screen. Reaching 16 lines would take about 60% of the
window, which would leave the VDP and image views above it too short. The cap stays at half.
**Outcome:** Reset Layout on a 1470×956 screen gives 14 pseudocode lines and 12 to 13 assembly rows,
up from about 4. Taller screens get 16 or more.

### Review: inputs that outlive their snapshot (at `16a70c0`)

A review of every engine and finder input found five findings. Each one now has a test in
`DecompilerPanelTests` that fails without its fix.

1. **Learned signatures outlived the signatures they came from.** If B was decompiled before C, its
   return value came from C's r0. After C was found to be void, B's signature was obsolete, but A was
   still given it. Each learned signature now records the snapshot, the function set and the callee
   signatures it was given. When a signature changes, everything learned from it is dropped, all the
   way up the call chain. (`TestPrototypeProvenance`)
2. **Function boundaries were not part of the question.** The user's function starts and the named
   addresses decide whether a branch counts as a call. Their fingerprint is now part of each request
   and acceptance check, each cache key, and each learned signature. A correction now outdates its
   callers' cached results as well as its own. (`TestFunctionSetKeysQuestions`)
3. **Discovered callees were hints for any memory.** Each callee is now recorded with the snapshot
   that called it, and it is a hint only for that snapshot. (`TestCalleeHintsBelongToTheirSnapshot`)
4. **Moving from scrubbing to a running emulator kept the scrubbed snapshot.** When the source
   changes, the panel now captures again at once, even with auto-refresh off.
   (`TestRunningAfterScrubCaptures`)
5. **A new symbol or start did not change which function contained an address.** The panel now
   finds the containing function again when the function set, or the callees found for the held
   snapshot, change. (`TestHintsReResolve`)

The finder's call-site hints are still read only during resolution, never every frame, as
`DecompilerInputs::callSites` requires. A new call-stack entry therefore takes effect at the next
resolution.
