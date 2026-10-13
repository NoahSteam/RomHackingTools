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
| 3 | `DECOMPILER_GUIDE.md`: every menu path and label it names exists in the app as written (Windows → Debugger → SH-2 Pseudocode, Decompile Here, Call Stack and Registers Decompile, Rename Callee, Set Function Start Here, BIOS → Choose BIOS Image…, Edit Comment). | | |
| 4 | `Docs/Decompiler/DEMO.md` run end to end on a real game: each step does what its right-hand column says. Note any step that does not, and why. | | |
