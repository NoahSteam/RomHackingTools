# Windows fix report, 2026-11-05

Branch `windows/fix-2026-11-05`. Master was not touched. Base: origin/master 16629836.
Commits on the branch (before this report):

- `16c54aa8` MSBuild projects: toolset v143 and the native menu bar (A12-12, A12-13)
- `cecc6adf` RomHackingTools.sln: header says Visual Studio 17 (A12-12)

Machine: Windows 11 Home, VS 2022 Community only (MSVC 14.38 / v143). No VS 2019.

## Summary

| Task | Result |
|---|---|
| 1. A12-12 toolset v143, build as shipped | **Done, links.** 0 errors, 0 unresolved externals, no `/p:PlatformToolset` override. |
| 2. A12-13 `SE_NATIVE_MENUBAR` in FrontEnd.vcxproj | **Done, verified.** MSBuild exe has a native menu bar, identical to the CMake build's. |
| 3. cmake build + ctest | **89 of 89 pass**, no regression. |
| 4. A12-15 which test touches `%APPDATA%\SaturnExplorer\states` | **`SaturnExplorerSavestateSlotsTests`** (the only one of 89). Not fixed. |

User settings (`settings.ini`, `imgui.ini`) were backed up and are restored byte-identical
(SHA-256 prefixes `1D7CC054…` and `428E2FB9…` match the originals). Running the GUI rewrites both
files, so I restored them once at the end, after the last launch.

## 1. A12-12: v143 in every SaturnExplorer .vcxproj

Changed `<PlatformToolset>v142</PlatformToolset>` to `v143` in all seven SaturnExplorer projects
(4 lines each, 28 lines): `Core/SaturnExplorer`, `Drivers/Common`, `Drivers/Live`,
`Drivers/Savestate`, `FrontEnd/FrontEnd`, `third_party/ghidra-decompiler`, `third_party/zlib`.
`grep v142` over them is now 0.

`RomHackingTools.sln` header moved from `# Visual Studio Version 16` / `VisualStudioVersion =
16.0.31424.327` to `17` / `17.0.31903.59`. It was not strictly required (MSBuild ignores it), but
the projects now need VS 2022, so the header should say so. `MinimumVisualStudioVersion` is
unchanged.

**Build as shipped.** I committed first, then `git archive HEAD` into a clean, short path
(`%TEMP%\sb2`, which avoids the FTK1011 path-length error from last time) and ran, from a VS 2022
developer shell:

```
msbuild RomHackingTools.sln -t:FrontEnd -p:Configuration=Debug -p:Platform=x64 -m
```

No `PlatformToolset` override. Result: `exit=0`, `0 Error(s)`, 176 warnings, 3 min 30 s from clean.
`FrontEnd.vcxproj -> ...\x64\Debug\SaturnExplorer.exe`. No LNK2019/LNK2001/LNK1120, so the
unresolved-externals concern in `WindowsFollowUps.md` item 1 does not occur. MSB8020 is gone.
The warnings are the existing ones (Ghidra C4099/C4101/C4838, `strncpy` C4996, and MSB8029 because
the build dir is under `%TEMP%`).

Scope limit: I built the FrontEnd target and its dependencies, not every project in the solution.
The solution also holds ten projects outside SaturnExplorer (BinPatcher, DragonForce, FindData,
MemoryAnalyzer, ImGuiLib, NSSaturnTool, SakuraTaisen, SakuraTaisen2, Utils,
YabuseMednafenSaveConverter). They still pin v142 for the most part (65 v142 lines before my change
across the repo, 28 of them mine), so "Build Solution" on a VS-2022-only machine will still stop with
MSB8020 on those. I left them alone (not SaturnExplorer, outside the task). **Classification: repo
issue, out of scope; worth a follow-up if "Build Solution" must be green.**

## 2. A12-13: SE_NATIVE_MENUBAR in FrontEnd.vcxproj

Added `SE_NATIVE_MENUBAR` to the ClCompile `PreprocessorDefinitions` in `FrontEnd.vcxproj`. That
ItemDefinitionGroup has no Configuration condition, so it applies to all configurations
(Debug/Release x Win32/x64). A comment next to it points at CMakeLists.txt. The source needs no
other change: the native-menu bridge in `App.cpp` and `IPlatform.h` is compiled everywhere and
switched by this one macro.

**Verification** (script: `EnumWindows` for the process, then `GetMenu` on the main window):

| | MSBuild (`x64\Debug\SaturnExplorer.exe`) | CMake (`build\bin\Release\SaturnExplorerFrontEnd.exe`) |
|---|---|---|
| Main window class | `SaturnExplorerWindow` | `SaturnExplorerWindow` |
| `GetMenu` | non-zero (25626357) | non-zero (107481907) |
| Top-level menus | 10 | 10 |

Top level, in order: `&Session`, `&Layers`, `&Run`, `&Data`, `&Patch`, `&Windows`, `De&mo`,
`&Tools`, `Se&ttings`, `&Help`. I dumped the full recursive menu tree of both (191 lines each,
including every submenu, separator and command id) and `diff` reports them **identical**.

Notes: (a) The "ImGui Platform" viewport window belongs to the same process and has `GetMenu=0`;
that is correct, the menu is on the main window. (b) `De&mo` appears in both because neither exe
was built with the release flag that hides it (that is separate, see the earlier Demo-menu change).
(c) Output rename: **not needed.** `FrontEnd.vcxproj` already has `<TargetName>SaturnExplorer`, so
the MSBuild exe is `SaturnExplorer.exe`, while CMake's is `SaturnExplorerFrontEnd.exe`; I left both
as they are.

## 3. Regression: cmake build + ctest

On the branch (base 16629836 plus the project edits), `cmake -S . -B build -G "Visual Studio 17 2022"
-A x64`, `cmake --build build --config Release --parallel 8`: configure 0, build 0, no errors, about
5 min incremental. The vcxproj edits do not enter the CMake build, so this confirms nothing else moved.

I ran ctest one test at a time (`ctest -R "^name$" -C Release --timeout 900`) so I could watch the
config folder per test (task 4). **89 tests, 0 failed, summed test time 306 s, 5 min 46 s wall.**
The same 89 as last time: no fault suite or LoaderFuzz on Windows by design (`if(UNIX)` /
`if Linux` in CMakeLists.txt).

The four suites you named:

| Test | Result | Time |
|---|---|---|
| SaturnExplorerSh2DecompilerTests | pass | 42 s |
| SaturnExplorerSh2GeneratedTests | pass | 25 s |
| SaturnExplorerDecompilerTrustTests | pass | 60 s |
| SaturnExplorerLoaderFuzzTests / fault suite | not built on Windows | n/a |

Slowest others: Sh2DecompilerEngineTests 69 s, DecompilerRunnerTests 37 s, DecompilerModelTests 23 s.

## 4. A12-15: the test that touches `%APPDATA%\SaturnExplorer\states`

**`SaturnExplorerSavestateSlotsTests`** (source `FrontEnd/tests/SavestateSlotsTests.cpp`).

Method: before and after each of the 89 tests, I recorded the mtime of the config dir, `states\`,
`exports\`, `decompiler\`, `settings.ini` and `imgui.ini`, plus the `states\` listing. Exactly one
test changed anything: `SavestateSlotsTests`. `states\` mtime went from `2026-11-05 08:57:25` (the
earlier run of this same test, during the first report) to `14:03:45`. The directory listing is the
same before and after (the user's three `*.slot0.sestate` files), so the test creates and then
removes its own files, and only the directory mtime moves. No other path changed, and none of the
other 88 tests touched the folder.

Why: `Settings::ConfigDir()` reads `%APPDATA%` (`Settings.cpp:84-90`), and the test calls
`SavestateSlots::SlotPath()` / `SaveToSlot()` directly (`SavestateSlotsTests.cpp:140,155,175`) with
slot 3 and slot 5, then `std::remove`s them (lines 194, 197). Those resolve under the real
`%APPDATA%\SaturnExplorer\states\` with `create=true` for `SaveToSlot`. Collision risk: the file
names are `<rom base name>.slotN.sestate` from the test's own fake ROM names, so they cannot match a
user's slot files unless a user has a ROM called the same thing, but a crash mid-test would leave a
stray file in the user's folder. Not fixed, as asked. A fix would point `APPDATA` (or
`XDG_CONFIG_HOME`) at a temp dir for the test, via a CMake `ENVIRONMENT` property.

## Not done / caveats

- Only the SaturnExplorer projects were moved to v143; ten other projects in the solution still pin
  v142 (section 1).
- I built the MSBuild **Debug x64** configuration only, not Release or Win32.
- The MSBuild exe was checked for its menu bar and a clean exit via WM_CLOSE only, not driven
  through a full session.
- Docs: `Docs/WindowsFollowUps.md` and the ledger rows A12-12/A12-13/A12-15 should be updated by the
  coordinator; I did not edit them on this branch.
