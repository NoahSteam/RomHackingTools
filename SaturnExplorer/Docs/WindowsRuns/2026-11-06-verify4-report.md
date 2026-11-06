# Windows verify report 4, 2026-11-06

Branch `windows/verify4-2026-11-06`, base origin/master `9df1524b` (it contains the requested `5f002440`; the first message named `1efd1c26`, which
is older, so everything below ran on `9df1524b`). Nothing in the source was changed; this branch adds this report and one screenshot. Master was
not touched, nothing was tagged or released.

Machine: Windows 11 Home, VS 2022 (MSVC 14.38.33130, v143), x64. Builds are in fresh directories under `%TEMP%` (`b4` Release, `b4d` Debug, `b4w` Debug
at /W3, `fzb` scratch), which causes one harmless `MSB8029` warning per project ("Intermediate directory ... under the Temporary directory").
User settings backed up before and restored byte-identical after (SHA-256 prefixes `1D7CC054...` settings.ini, `428E2FB9...` imgui.ini).
Stopped only processes I started (my builds, the app, and the Mednafen that the app launched).

## Summary

| # | Item | Result |
|---|---|---|
| 1 | Release build + full ctest | **Build FAILS on master with one error**: `FrontEnd\tests\AssemblyPanelTests.cpp(20,10): error C1083: Cannot open include file: 'unistd.h'`. Every other target built. ctest: **92 of 93 pass**, `SaturnExplorerAssemblyPanelTests` "Not Run" (its exe is not built). 93 registered, not 89 or 90 (see below). App launches; menu bar and Log fine. |
| 2 | A12-5 warnings | Engine build: **45 unique warnings**, none in SaturnExplorer's code, all but 3 are upstream lines (3 come from patches 0001, 0044, 0068). SaturnExplorer's own files: **0 warnings** at the level CMake builds them (/W1); **157** at /W3 (all `C4996` CRT deprecations and 8 `C4244`). Tables below. |
| 3 | A12-17 W10 BIOS + game | **Run**: a BIOS and game exist on this machine. Game boots under the patched Mednafen launched from the app, the decompiler decompiles the game's entry (`0x06005000`), name and note were saved, the app was quit and relaunched, and both came back. Caveat on W10 itself below. |
| 4 | A12-15 recheck | `states` mtime unchanged by `SaturnExplorerSavestateSlotsTests` alone and by a full ctest. Values below. |
| 5 | A12-19 prep | 24 ctest tests are not registered on Windows. The six POSIX test targets, forced on in a scratch copy, each stop at their first include: `dirent.h` (five Area9 targets and LoaderFuzz). |
| 6 | Area 12 P3 checklist | a) pass (93 tests, FileLockTests 3.45 s). b) **partly fails**: savestate path with non-ASCII characters does not open (and says nothing). c) pass. d) pass (script only). e) tooltip and right-click copy pass; the detail pane's Copy button could not be reached. |
| 7 | GitHub CI | Both CI jobs reproduce locally with **the same single error as item 1** (`AssemblyPanelTests.cpp(20,10)`, `unistd.h`). |

## 1. Build, ctest, launch

`cmake -S . -B %TEMP%\b4 -G "Visual Studio 17 2022" -A x64`, `cmake --build ... --config Release --parallel 8`: configure 0, **build exit 1**, 15 m 30 s
(a fresh tree, engine included). The only error line, quoted verbatim:

```
C:\Users\16306\Desktop\Rizwan\RomHackingTools\SaturnExplorer\FrontEnd\tests\AssemblyPanelTests.cpp(20,10): error C1083: Cannot open include file: 'unistd.h': No such file or directory [C:\Users\16306\AppData\Local\Temp\b4\SaturnExplorerAssemblyPanelTests.vcxproj]
```

Cause (repo bug): `AssemblyPanelTests.cpp` lines 20-21 include `<unistd.h>` and `<sys/stat.h>`; commit `b845f08d` ("Area 9 fixer: A9-57 to A9-62 (WIP)", 2026-11-06) added the
`unistd.h` include. Lines 291 and 295 of the same file also call POSIX `mkdir("saturn_explorer_watches.json", 0755)` and `rmdir(...)`; on MSVC `mkdir` takes one
argument (`_mkdir`), so removing the include will expose those next. `SaturnExplorerAssemblyPanelTests` is registered in `CMakeLists.txt:1044-1060` for all platforms.

`ctest --test-dir b4 -C Release --output-on-failure`: **99% tests passed, 1 tests failed out of 93**, 301 s. The one is `58 - SaturnExplorerAssemblyPanelTests (Not Run)`.
92 ran and passed. **Why 93 and not 89 or 90:** 89 was last round's count before I counted properly; the Windows count was already **92** at verify3
(`521dc0e5`), and `SaturnExplorerFileLockTests` makes 93. `SaturnExplorerFileLockTests`: `Passed 3.45 sec`; its output includes
`missing folder: cannot create or lock C:\Users\16306\AppData\Local\Temp/__se_filelock_30800/no_such_folder/x.lock: The system cannot find the path specified`.

Launch (Release exe from this build): menu bar `Session  Layers  Run  Data  Patch  Windows  Demo  Tools  Settings  Help` (native Win32 menu; `Demo` is present in a
local CMake build, the release package builds without it). Log panel, first row: `INFO Saturn Explorer started`. One oddity, not explained: the very first
launch of the session (pid 9680) was gone when I looked about a minute later, with no crash entry in the Application event log and no settings written.
It did not happen in any of the later launches, so I do not call it a bug.

## 2. A12-5, every MSVC warning

How the levels come out: CMake's Visual Studio generator compiles SaturnExplorer's targets at **/W1** (93 of 95 compiler invocations); only the engine
(`SaturnExplorerGhidraDecomp`, `se-sleighc`) is built with an explicit `/W3`. So at default settings SaturnExplorer's own files give no warnings at all. I also
ran a Debug build with `-DCMAKE_CXX_FLAGS=/W3` (in `b4w`) to list what /W3 would say about them. Unique (file, line, code) triples, from the
Release build log for the engine and the /W3 log for SaturnExplorer.

### 2a. Engine build (third_party/ghidra-decompiler): 45 warnings (33 in `SaturnExplorerGhidraDecomp`, 12 in `se-sleighc`)

"Origin" is how I decided: each warned line (and the line above it) was matched against the `+` lines of the patches that touch the same file
(`patches/*.patch`, 72 files); no match means the line is the unmodified pinned upstream (`918d44eb`). There is no upstream clone here to diff against, so "upstream"
means "no patch adds this line".

| File:line | Code | Message | Origin |
|---|---|---|---|
| `space.cc:292`, `space.cc:647` | C4101 | `'err'`: unreferenced local variable (`catch(LowlevelError &err)`) | upstream |
| `address.cc:966` | C4838 | conversion from `__int64` to `ghidra::uintb` requires a narrowing conversion (`0xffff...LL` table) | upstream (patch 0070 touches `address.cc`, not this table) |
| `fspec.hh:368` | C4099 | `ParameterPieces`: first seen as `class`, now `struct` | upstream (0028 touches `fspec.hh`, not this line) |
| `fspec.hh:385` | C4099 | `PrototypePieces`: first seen as `class`, now `struct` | upstream (same) |
| `database.cc:678` | C4101 | `'err'` (`catch(RecovError &err)`) | upstream |
| `database.cc:743` | C4146 | unary minus applied to unsigned type | upstream |
| `database.cc:2683` | C4101 | `'err'` (`} catch(LowlevelError &err) {`) | **patch 0044** (`engine-round5-printed-value-fixes`; it re-indents the catch that 0037 added) |
| `stringmanage.cc:472` | C4101 | `'err'` | upstream |
| `fspec.cc:2435`, `fspec.cc:4210` | C4101 | `'err'` (`ParamUnassignedError`) | upstream |
| `op.cc:531` | C4101 | `'err'` (`EvaluationError`) | upstream |
| `jumptable.cc:255`, `1646`, `1752`, `2005`, `2153`, `2835`, `2840` | C4101 | `'err'` | upstream |
| `jumptable.cc:564` | C4146 | unary minus applied to unsigned type | upstream |
| `funcdata_block.cc:559` | C4101 | `'err'` (`catch(CancelError &err)`) | **patch 0001** (`cancel-flag`) |
| `funcdata_block.cc:588` | C4101 | `'err'` (`JumptableThunkError`) | upstream |
| `funcdata_varnode.cc:703` | C4101 | `'err'` (`DataUnavailError`) | upstream |
| `ruleaction.cc:3463`, `4024`, `5940`, `7160` | C4146 | unary minus applied to unsigned type | upstream |
| `coreaction.cc:4178` | C4101 | `'err'` (`catch(DataUnavailError &err)`) | **patch 0068** (`engine-deadcode-late-stack-reads`) |
| `printc.cc:1905` | C4146 | unary minus applied to unsigned type | upstream |
| `memstate.cc:374`, `memstate.cc:394` | C4101 | `'err'` | upstream (no patch touches `memstate.cc`) |
| `sleigh_arch.cc:131`, `inject_sleigh.cc:322` | C4101 | `'err'` (`DecoderError`) | upstream |
| `slgh_compile.cc:2385` | C4101 | `'e'` (`const out_of_range &e`) | upstream (0074 touches the file elsewhere) |
| `slgh_compile.cc:3836` | C4101 | `'err'` (`DecoderError`) | upstream |
| `slghscan.cc:3159` | C4996 | `'fileno'`: POSIX name deprecated, use `_fileno` | upstream, flex output checked in (patch 0022 edits the file elsewhere) |
| MSVC `stdint.h:49,50,51,53,54,55,57,58,59` (compiling `slghscan.cc`) | C4005 | `INT8_MIN`, `INT16_MIN`, `INT32_MIN`, `INT8_MAX`, `INT16_MAX`, `INT32_MAX`, `UINT8_MAX`, `UINT16_MAX`, `UINT32_MAX`: macro redefinition | upstream flex output: `slghscan.cc:333-357` defines them first ("see previous definition") |

Counts by code: C4101 25, C4005 9, C4146 7, C4099 2, C4996 1, C4838 1 = 45. Three lines come from patches (0001, 0044, 0068); the rest are upstream. The engine
project's `/W3` is the setting from `VENDOR.md` ("MSVC /EHsc /W3 /bigobj").

### 2b. SaturnExplorer's own files

- **Default build (/W1): 0 warnings** in `FrontEnd`, `Core`, `Drivers` and the test targets.
- **At /W3** (`b4w`, Debug, `-DCMAKE_CXX_FLAGS=/W3`): **157 warnings**, only two codes. (A further 12 `C4530` "exception handler used" appear only because that
  command line replaces CMake's default flags and drops `/EHsc`; I left them out as an artifact.)

Non-test files (31):

| Code | Where |
|---|---|
| C4996 `fopen` | `Drivers/Savestate/src/SavestateDriver.cpp:192`, `FrontEnd/src/FileRead.h:51`, `FrontEnd/src/FileWrite.cpp:159` |
| C4996 `strerror` | `FrontEnd/src/FileWrite.cpp:69`, `:135` |
| C4996 `_open` | `FrontEnd/src/FileWrite.cpp:196` |
| C4996 `strncpy` | `FrontEnd/Platforms/Windows/WindowsPlatform.cpp:339`, `FrontEnd/src/App.cpp:850` |
| C4996 `getenv` | `FrontEnd/src/Settings.cpp:89` |
| C4996 `sscanf` | `FrontEnd/src/App.cpp:530`, `AssemblyPanel.cpp:224`, `HexEditorPanel.cpp:250`, `:607`, `Sh2Operands.cpp:108, 111, 114, 120, 123, 125, 304, 319, 321, 323` |
| C4244 `uint64_t` to `uint32_t`, possible loss of data (argument) | `FrontEnd/src/App.cpp:8219, 8225, 8244, 8256, 8506, 8514, 8531, 8537` |

Test files (126): all `C4996` (mostly `getenv`, some `fopen`): `Sh2GeneratedTests.cpp` 35, `Sh2DecompilerTests.cpp` 29, `DecompilerTrustTests.cpp` 28, `Sh2DecodeTests.cpp` 11,
`DecompilerModelTests.cpp` 11, `HardwareWidthTableTests.cpp` 2, `DiffToolTests.cpp` 2, and one each in `SavestateShapeTests.cpp`, `YssFixtureTests.cpp`,
`Sh2DecompilerEngineTests.cpp`, `SavestateSlotsTests.cpp`, `FileLockTests.cpp`, `FileWriteTests.cpp`, `PanelInteractionTests.cpp`, `MappingOracle.h`.
Third-party (imgui, zlib, nlohmann) gave none in either build. Note `SavestateDriver.cpp:192` is the `fopen(path.c_str())` that is the cause of the
non-ASCII path failure in 6b. The Debug CI-style build (item 7) ended with MSBuild's summary "109 Warning(s)" (it counts repeats and the `MSB8029` ones; I did not break that count down).

## 3. A12-17, W10: BIOS and game

Present on this machine (the user's own, not obtained or copied by me): Saturn BIOS files in `_emu\mednafen\firmware` (`mpr-17933.bin`, `sega_101.bin`, 512 KiB each)
and several game discs under `Emulators\Saturn\roms`. I used the disc that `settings.ini` already points at (Sakura Wars 2, disc 1). Nothing was copied, committed or
modified there. The app ran with its working folder `%TEMP%\v4run`, so the names and notes files went there.

1. App started, **Session > Launch** (F5). The Log: `Launched Mednafen with SAKURA TAISEN2_1.cue`, then `Connected to emulator (live): default` two seconds later. The
   patched `_emu\mednafen\mednafen.exe` booted the game (BIOS logo, then the title screen "PRESS START BUTTON" and the intro): so the BIOS in Mednafen's firmware
   folder was found and read.
2. SH-2 Pseudocode, Follow PC on: it decompiled the function around the PC (`func_0x060101ac`).
3. The game's entry: `0x06004000` (the usual 1ST_READ address) is not code in this game: the panel said
   `Flow runs into bytes that do not decode at 0x06004000: embedded data, or the entry is wrong; the function stops there.` The disc's IP.BIN header
   (read only: first sector, offset `0xF0`) gives the first-read address `0x06005000`. Typing `06005000` and Enter **decompiled it**: `void func_0x06005000(void)`
   with `Write_SR(in_sr & 0x3f3 | 0xf0); Write_GBR(0x6002400); (*(code *)0x600509c)(); ...` and two amber warnings
   (`0x06005098: Could not recover jumptable ... Too many branches`, `Treating indirect jump as call`).
4. Names: Call Stack > **Load Symbols...** with `06005000 game_entry_v4`; the file `saturn_functions.txt` held that line at once. Notes: in SH-2 Assembly (Follow PC off),
   double-click on the Comment cell of `06005204`, type `v4_note_one`, Enter; `assembly_comments.txt` held `06005204 v4_note_one`.
5. Quit the app (clean, 1 s, no message box); the Mednafen it had launched stayed running, so I stopped that process myself. Relaunched, F5 again, same game: the pseudocode
   for `06005000` was headed `game_entry_v4 @ 0x06005000`, and SH-2 Assembly at `06005204` showed `v4_note_one` in the Comment column. **Both survived.**

What this does not prove, for W10: `-filesys.path_firmware` passed as an absolute path by a **packaged** build. This run used the dev checkout's Mednafen from
`settings.ini`, whose own config finds `firmware` beside it; the release zip's `mednafen-se\` was not built or run. I did not use the decompiler's
BIOS button (Choose BIOS Image) either. So: BIOS read and game run on Windows, yes; the absolute-path claim, not tested.

## 4. A12-15 recheck (states folder mtime)

| When | `%APPDATA%\SaturnExplorer` | `...\states` |
|---|---|---|
| Start of this session (before any ctest) | `2026-11-05T19:37:39.65` | `2026-11-05T19:29:22.2163464` |
| After the **full** ctest (93 tests) | unchanged | `2026-11-05T19:29:22.2163464` (unchanged) |
| Immediately before `ctest -R SaturnExplorerSavestateSlotsTests` | `2026-11-05T19:37:39.6527935` | `2026-11-05T19:29:22.2288636` |
| Immediately after it (`Passed 0.04 sec`) | `2026-11-05T19:37:39.6527935` | `2026-11-05T19:29:22.2288636` |

Neither time moved (the two `states` readings differ by 12.5 ms only because `Get-ChildItem` and `Get-Item` read the entry differently; the minute and second are
the same as the value the verify3 run's ctest left at 19:29:22 yesterday). No `__se_slots_cfg_*` or `__se_split_settings__*` (or any `__se_*`) folder was left in
`%TEMP%` after the full run. **A12-15 holds.**

## 5. A12-19 prep

### 5a. ctest tests not registered on Windows (24)

Compared with the 111 `add_test` calls in `CMakeLists.txt` (the `foreach` ones expand to several): the tests below sit under a condition that is false on Windows.

| Test | Condition (CMakeLists line) |
|---|---|
| `SaturnExplorerExportAttachTests` | `BUILD_TESTING AND NOT WIN32 AND NOT EMSCRIPTEN` (204) |
| `SaturnExplorerDecompilerFaultTests` | `if(UNIX)` (365) |
| `SaturnExplorerSh2PrintedCTests` | `if(UNIX)` (434) |
| `SaturnExplorerDataSearchWalkTests` | `if(NOT WIN32)` (617) |
| `SaturnExplorerArea9Round3Tests` | `CMAKE_SYSTEM_NAME STREQUAL "Linux"` (695) |
| `SaturnExplorerLoaderFuzzTests`, `Area9Round4Tests`, `Area9Round5Tests`, `Area9Round5FullDisk`, `Area9Round6Tests`, `Area9Round7Tests` | `CMAKE_SYSTEM_NAME STREQUAL "Linux"` inside `SE_ENABLE_DECOMPILER` (922-1005) |
| `SaturnExplorerPosixSpawnTests`, `LaunchPathTests`, `SettingsTests`, `AIInvestigationLintTests` | `NOT WIN32 AND NOT EMSCRIPTEN` (1132, 1152, 1184, 1388) |
| `SaturnExplorerLiveReconnectTests`, `LiveRestoreSignalTests`, `LiveDecompilerCaptureTests`, `RewindPipelineTests`, `LivePokeTests`, `MemoryRoundTripTests`, `LiveExporterRobustnessTests`, `LiveHaltRaceTests` | `BUILD_TESTING AND NOT WIN32 AND NOT EMSCRIPTEN` (1550-1714) |

Notably `SaturnExplorerSettingsTests` is among the skipped ones. The 93 that are registered include the decompiler tests (`Sh2Decompiler`, `DecompilerRunner`, `Sh2Semantics`,
`Sh2Decode`, `Sh2Generated`, `MemorySnapshot`, `Sh2FunctionFinder`) and `FileLockTests`.

### 5b. Forced on (scratch copy, nothing in the repo changed)

`git archive HEAD SaturnExplorer` into `%TEMP%\fz`, the `if(CMAKE_SYSTEM_NAME STREQUAL "Linux")` conditions at lines 685 and 897 turned into `if(TRUE)`
(the `-rdynamic` one at 362 left alone), configured to `%TEMP%\fzb`, each target built on its own (Release, VS 2022):

| Target | First compile error (the build stops there; a missing include is fatal) |
|---|---|
| `SaturnExplorerArea9Round3Tests` | `FrontEnd\tests\Area9Round3Tests.cpp(24,10): error C1083: Cannot open include file: 'dirent.h': No such file or directory` |
| `SaturnExplorerArea9Round4Tests` | `Area9Round4Tests.cpp(23,10): error C1083: ... 'dirent.h' ...` |
| `SaturnExplorerArea9Round5Tests` | `Area9Round5Tests.cpp(24,10): error C1083: ... 'dirent.h' ...` |
| `SaturnExplorerArea9Round6Tests` | `Area9Round6Tests.cpp(28,10): error C1083: ... 'dirent.h' ...` |
| `SaturnExplorerArea9Round7Tests` | `Area9Round7Tests.cpp(26,10): error C1083: ... 'dirent.h' ...` |
| `SaturnExplorerLoaderFuzzTests` | `LoaderFuzzTests.cpp(69,10): error C1083: ... 'dirent.h' ...` |

Each is the only error in its log, because the compiler stops at the first missing header. The other POSIX-only includes that would follow, from the source:
Round3: `fcntl.h signal.h sys/wait.h unistd.h`; Round4: `fcntl.h signal.h sys/file.h sys/time.h sys/wait.h unistd.h`; Round5: `dlfcn.h fcntl.h signal.h sched.h sys/file.h sys/mount.h
sys/resource.h sys/wait.h unistd.h`; Round6 and Round7: `dlfcn.h fcntl.h signal.h sys/file.h sys/wait.h unistd.h`; LoaderFuzz: `fcntl.h malloc.h signal.h sys/resource.h
sys/wait.h unistd.h`. (`sys/stat.h` and `fcntl.h` exist on MSVC; the rest do not.) `DecompilerFaultTests` was not tried (it links the engine, which would rebuild it all;
its own comment says fork, `-rdynamic`).

## 6. Area 12 P3 checklist

**a) Build and `FileLockTests`.** The build produces `SaturnExplorerFileLockTests.exe`; `direct.h`, `process.h`, `_getpid`, `_mkdir`, `_rmdir` compile (and
`PanelInteractionTests`, `SavestateSlotsTests` with `_putenv_s`). ctest registers **93** (see item 1), not 90; `FileLockTests` passes in 3.45 s.
The target that fails to build is not one of these.

**b) A12-4, savestate on the command line** (`SaturnExplorerFrontEnd.exe`, the build-release.bat product; `SaturnExplorer.exe` from `build-release-msbuild.bat` was **not** built).
Test file: a Yabause `.yss` from the repo's `SakuraTaisen2` folder.

- ASCII path (`%TEMP%\v4run\state1.yss`): **opens**. Log `Loaded savestate: ...\state1.yss`; VRAM, VDP and SH-2 Assembly show data (PC `0603055A`); no polling.
- **Non-ASCII path (`%TEMP%\テスト\ステート.yss`): does not open, and nothing is said.** The panels say "No data loaded", the Log has only the start line, and the app falls back to
  polling. Cause: `Drivers/Savestate/src/SavestateDriver.cpp:192` calls `std::fopen(path.c_str(), "rb")` on the UTF-8 path that `WinMain.cpp` builds from `__wargv`;
  on Windows `fopen` takes the ANSI code page, so any path outside the active code page fails (the known limit already filed for Mednafen's path). `App::OpenSavestate`
  returns false and writes no Log line, so the failure is silent. (Repo bug; the same `se_savestate_open` serves the toolbar's Open, so it is not specific to the command line.)
- Missing file argument: **no crash**, the window comes up, polling as with no argument, no Log line either.
- `--live`: no crash; with no emulator running there is nothing to connect to (Log only has the start line). That auto-connect works is shown another way: the F5 launch in
  section 3 logged `Connected to emulator (live): default`. **Not tested:** `--live <endpoint>`, and `--live` against a separately started emulator.

**c) A12-15.** See section 4: unchanged mtime across a full ctest; no leftover `__se_*` folders.

**d) A12-16.** I did not run `make-release.bat` (it builds Mednafen through MSYS2, may upgrade MSYS2 and writes `settings.ini`). I ran the generator it uses:
`py -3 Integration\make_source_offer.py --out %TEMP%\v4src\SOURCE.txt --mednafen-dir _emu\mednafen --se-ref 9df1524b`. The generated `SOURCE.txt` says:
`On Windows it passes CPPFLAGS="-DUNICODE=1 -D_UNICODE=1 -DSE_MDFN_REWIND=1": without the two UNICODE defines Mednafen builds its Windows 98 variant, which refuses to start on Windows NT. Use that CPPFLAGS for a Windows rebuild.` Pass.

**e) A12-18.** Trigger: `saturn_functions.txt.lock` made a folder in the working folder, then Load Symbols. The row, cut at the panel edge in the table, was:
`The function names: not saved, cannot create or lock C:\Users\16306\AppData\Local\Temp\v4run\saturn_functions.txt.lock: Access is denied; saving without the lock could lose another instance's entries, so the file is left as it is and this session's changes are kept in memory only`
(the doubled prefix and the stray `.;` from verify3 are gone).

- **Hover:** a tooltip shows the whole text wrapped, including `Access is denied;` (screenshot `2026-11-06-verify4-log-tooltip.png`). **Pass.**
- **Right-click > Copy message:** the menu offers it; the clipboard then held exactly the text above (read back with `Get-Clipboard`). **Pass.**
- **Detail pane's Copy button: not verified.** Left-clicking a row highlights it, but no detail pane appears below the table, at 1550x830 (maximised) or 1280x720 (restored),
  and with the Log tab made taller by dragging the splitter. `LogPanel.cpp:207-238` draws the pane for a selected row; either the click did not select (the highlight stays after the
  mouse leaves, so it looks selected) or the pane is below the visible area of the window. I could not tell which; a person pressing the button would settle it.

The exit message box also changed: with the same refused save, quitting now shows `These changes were not saved:` / `The function names: not saved, cannot create or lock ...: Access is denied; saving without the lock could lose another instance's entries, so the file is left as it is` / `The files were left as they were, and the Log panel closes with the window, so the changes are lost. At exit all the saves share 10 seconds of waiting for another instance.` (0.6 s after the close, since access denied is not retried).

## 7. GitHub CI, reproduced

**"Supported platform build + tests (windows-latest)"** (`saturn-explorer-verify.yml`: `cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON`, the engine target alone, then
`cmake --build build --config Debug --parallel 4`): configure 0, engine target 0, full build **exit 1** (7 m, fresh tree). The only error:

```
C:\Users\16306\Desktop\Rizwan\RomHackingTools\SaturnExplorer\FrontEnd\tests\AssemblyPanelTests.cpp(20,10): error C1083: Cannot open include file: 'unistd.h': No such file or directory [C:\Users\16306\AppData\Local\Temp\b4d\SaturnExplorerAssemblyPanelTests.vcxproj]
```

**"build-release.bat (Windows, CMake Release)"**: ran the script as shipped (`build-release.bat`, newest VS, x64, `cmake --build build --config Release --parallel`): `=== Build FAILED ===`, exit 1. The only error:

```
C:\Users\16306\Desktop\Rizwan\RomHackingTools\SaturnExplorer\FrontEnd\tests\AssemblyPanelTests.cpp(20,10): error C1083: Cannot open include file: 'unistd.h': No such file or directory [C:\Users\16306\Desktop\Rizwan\RomHackingTools\SaturnExplorer\build\SaturnExplorerAssemblyPanelTests.vcxproj]
```

Both jobs hit it because the plain `cmake --build` builds every registered test target, and `AssemblyPanelTests` is registered for all platforms. File: `FrontEnd/tests/AssemblyPanelTests.cpp`,
line 20 (`#include <unistd.h>`), introduced by `b845f08d`; the next ones to expect once it is gone are the POSIX `mkdir(path, 0755)` and `rmdir(path)` at lines 291 and 295.
MSBuild continued with the other projects and nothing else failed, so the tails of those logs hold this error and the final "Build FAILED". (The local VS is 14.38; windows-latest ships a newer 14.4x
toolset; I do not expect it to change this error.)

## Not done

- `SaturnExplorer.exe` from `build-release-msbuild.bat` (A12-4 asked for both exes).
- `--live <endpoint>`; `--live` against a separately started emulator.
- The detail pane's Copy button (A12-18), see 6e.
- W10 in the packaged form (`mednafen-se\` and an absolute `-filesys.path_firmware`); the decompiler's Choose BIOS Image button.
- `make-release.bat` itself (the generator it calls was run directly).
- `DecompilerFaultTests` forced on; a ctest run of the Debug build (the Debug CRT makes the engine tests very slow, per the workflow's own comment).
- The first launch that vanished (section 1), not explained.
