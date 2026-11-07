# Windows verify report 6, 2026-11-06

Branch `windows/verify6-2026-11-06`, base origin/master `cd3e6c19` ("Ledger: A12-27 cause confirmed by Windows verify5"). Nothing in the source was changed; this branch
adds this report and one screenshot. Master was not touched. Console only, except item 4, for which the user said yes: the app window was used for about eight minutes
at the end (launch, a few clicks, quit; no game, no emulator).

Machine: Windows 11 Home, VS 2022 (MSVC 14.38.33130, v143), x64, screen 1920x1080 at 125% scaling. Fresh build directories under `%TEMP%` (`b6` Release, `b6d` Debug, `b6w` Debug at /W3).
User settings backed up before and restored byte-identical after (SHA-256 prefixes `1D7CC054...` / `428E2FB9...`). Only processes I started were stopped.

## Summary

| # | Item | Result |
|---|---|---|
| 1 | Release and Debug builds as CI does | **Both FAIL on master with one new error**: `FrontEnd\tests\Sh2GeneratedTests.cpp(7643,1): error C2026: string too big, trailing characters truncated`. Nothing else fails. The /W3 build fails the same way. Registered on Windows: `FileLockTests`, `Utf8PathTests`, `LogPanelTests` yes; `Area9Round5FullDisk` **no** (Linux-only block). |
| 2 | Full `ctest -C Debug` | **94 of 95 pass** (1367 s); the one is `Sh2GeneratedTests` "Not Run" (its exe cannot be built, item 1). `Utf8PathTests` passes (0.08 s). `DiffToolTests` passes (0.33 s). With TMPDIR, TEMP and TMP all at a folder whose parent is missing it **fails in 0.35 s, exit code 1, no dialog**. |
| 3 | Non-ASCII savestate path | **Works**: the driver opens it, and so does the app; a missing file now gives a Log error with the system reason. |
| 4 | Log pane detail | **The detail pane appears and its Copy button works.** The "no pane" of verify4 was my screenshot tool cropping the window, see section 4. |
| 5 | Warnings | Engine: **42** unique (was 45); the three that came from patches are gone. `App.cpp` C4244: **0** at /W3. One new SaturnExplorer warning at the default level: `MappingOracle.h(925,38)` C4319. |
| 6 | FileLock regression | `FileLockTests` passes (3.4 s). A second process holding the lock is reported after 3.03 s (10.01 s at exit), never waited on forever. |

## 1. Builds

Release: `cmake -S . -B %TEMP%\b6 -G "Visual Studio 17 2022" -A x64`, `cmake --build ... --config Release --parallel 8`: configure 0, **build exit 1** (15 m). Debug as CI does
(`-DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON`, the engine target alone, then everything, `--parallel 4`): engine step exit 0, **full build exit 1**. The only error in both, verbatim:

```
C:\Users\16306\Desktop\Rizwan\RomHackingTools\SaturnExplorer\FrontEnd\tests\Sh2GeneratedTests.cpp(7643,1): error C2026: string too big, trailing characters truncated [C:\Users\16306\AppData\Local\Temp\b6\SaturnExplorerSh2GeneratedTests.vcxproj]
```

Cause (repo bug, my reading): `Sh2GeneratedTests.cpp:7469` starts `static const char kCxxHeaderBase[] = R"CXX(// The engine's reading of its own pseudocode ...` and the raw string ends at
line 7644 (`)CXX";`). Those lines are **16,501 bytes**; MSVC refuses a single string literal over 16,380 bytes (C2026), and the diagnostic is reported at the last line of the literal. A GCC or clang build has no such
limit, which is why Linux is green. Splitting the literal into adjacent pieces below 16 KB would satisfy MSVC (not changed here). Other raw-string literals in the file (lines 5049, 5942, 7332, 7345, 7649, ...) compiled.

`ctest --test-dir b6 -C Release`: **99% tests passed, 1 tests failed out of 95**, 275.9 s; `13 - SaturnExplorerSh2GeneratedTests (Not Run)`.

Registered on Windows (`ctest -N`, 95 tests): `SaturnExplorerFileLockTests`, `SaturnExplorerUtf8PathTests`, `SaturnExplorerLogPanelTests`, `SaturnExplorerDiffToolTests`, `SaturnExplorerAssemblyPanelTests`.
**Not registered:** `SaturnExplorerArea9Round5FullDisk` (`CMakeLists.txt:982-983`, inside the `CMAKE_SYSTEM_NAME STREQUAL "Linux"` block with the other Area9 tests).

## 2. `ctest -C Debug`, the full run

`ctest --test-dir b6d -C Debug --output-on-failure`: **94 of 95 passed**, total 1367.07 s (22.8 min; `SaturnExplorerSh2DecompilerEngineTests` alone took 375.9 s in Debug). Failed: only `13 - SaturnExplorerSh2GeneratedTests (Not Run)`,
because its exe does not exist (item 1). Notable lines: `Utf8PathTests Passed 0.08 sec`, `DiffToolTests Passed 0.33 sec`, `FileLockTests Passed 3.44 sec`, `LogPanelTests Passed 0.11 sec`, `AssemblyPanelTests Passed 0.28 sec`.

**DiffToolTests with a missing temp parent.** I set `TMPDIR`, `TEMP` and `TMP` all to `C:\se_no_such_parent_dir` (does not exist) and ran the exe directly, with a 130 s limit and a check for dialogs:

| Config | How it ended |
|---|---|
| Debug | exited after **0.35 s**, **exit code 1**, no dialog window. stdout: 54 lines starting `FAIL ...\DiffToolTests.cpp:143: MakeDirectory(root)`, then `:149 WriteSnapshotFolder(sa, a, error)`, `:150`, `:161 Read(...)`, `:162 got == ...`, `:163 got.size() == ...`; stderr empty. |
| Release | exited after **0.34 s**, **exit code 1**, no dialog, 54 lines on stdout (same), no crash event. |
| Debug, normal environment | exited 0.32 s, exit code 0, `DiffTool: all checks passed`. |

So the hang of verify5 is gone: the failed writes now end in a failed test, quickly. The message is on **stdout** (the `FAIL` lines), not stderr; the "CRT asserts sent to stderr" part was not exercised because no assertion fires any more. I did not check the `TIMEOUT 120` property.

## 3. A12-4 / A12-25, non-ASCII savestate path

Copy of a Yabause `.yss` (the repo's `SakuraTaisen2` folder) to `%TEMP%\テスト\ステート.yss` (code points `30C6 30B9 30C8` / `30B9 30C6 30FC 30C8`).

- **Console probe** (my own 15-line program linking the real `SaturnExplorerSavestateDriver`, taking a wide `argv` and converting to UTF-8 as `WinMain` does): ASCII path `se_savestate_open -> 0 (opened)`; **Japanese path `-> 0 (opened)`**; a missing file `-> -8 (failed)`, exit 1. Never silent.
- **In the app** (command line `SaturnExplorerFrontEnd.exe "<that path>"`): opens. Log: `INFO Loaded savestate: C:\Users\16306\AppData\Local\Temp\テスト\ステ...`; VRAM, VDP tables and SH-2 Assembly show the state (PC `0603055A`); status bar `Dump: ステート.yss`, `Loaded ステート.yss`. Fixed.
- **Missing file argument:** no crash; Log `ERROR Could not open savestate C:\Users\16306\AppData\Local\Temp\v6run\nope.yss: No such file or directory` (the tooltip shows the whole text); the status bar says `Waiting for emulator live endpoint...`, so it falls back to polling as designed.

## 4. A12-26 / A12-18, the Log detail pane

Trigger as before (`saturn_functions.txt.lock` a folder, then Load Symbols), then a left-click on the error row. **The detail pane appears under the table**: `17:54:51.478 [ERROR] frame 0`, the whole message
wrapped (`The function names: not saved, cannot create or lock C:\Users\16306\AppData\Local\Temp\v6run\saturn_functions.txt.lock: Access is denied; saving without the lock could lose another instance's entries, so the file is left as it is and this session's changes are kept in memory only`),
and a **Copy message** button. Clicking that button put exactly that text on the clipboard (read back with `Get-Clipboard`). Hover tooltip and right-click copy were already checked in verify4.
Screenshot: `2026-11-06-verify6-log-detail-pane.png`.

**Correction to my earlier reports.** My screenshot helper captured a region as large as the window rectangle that Windows reports to a non-DPI-aware process (1550x830), but the screen is scaled to 125%, so the picture covered only the **top-left 1550x830 physical pixels of a 1920x1080 window**:
the bottom fifth and right fifth of the window were cut off. Clicks were not affected. What was affected: in verify4 (A12-26) the detail pane was simply below the cropped edge, and in the verify3 report (section 5) my note that the Log panel "sits partly below the
bottom of an 823 px-high window" was the same artifact (also the buttons "cut off at the right"). In this run I used a full-screen physical-pixel capture and the pane is there. I did **not** test a build from before `292aec7c`, so I cannot say whether the pane was missing without that fix; the
A12-26 ledger row's description (the pane was given too little room in a short panel) may or may not have been real, but the symptom I reported was mine.

## 5. A12-5 warnings

| Level | Result |
|---|---|
| Engine (`SaturnExplorerGhidraDecomp` + `se-sleighc`) at CI level (/W3, Release build; the Debug builds give the same 42) | **42 unique** warnings: C4101 22, C4005 9, C4146 7, C4099 2, C4996 1, C4838 1. Down from 45: `coreaction.cc` C4101 (was patch 0068), `database.cc` C4101 (was patch 0044) and `funcdata_block.cc` C4101 (was patch 0001) are gone, so none of the remaining 42 comes from a patch; all are unmodified upstream lines. |
| SaturnExplorer at CI level (/W1) | **1 warning**: `FrontEnd\tests\MappingOracle.h(925,38): warning C4319: '~': zero extending 'uint32_t' to 'uint64_t' of greater size` (line 925 is `case 0x7: return NumberValue(~a);`, in `Sh2DecompilerTests`/`Sh2GeneratedTests`). New since verify4, and C4319 is a level-1 warning, so it shows at default settings. It means `~` is done in 32 bits and then widened, which looks like a real width bug in the test oracle (it may also be intended). |
| SaturnExplorer at /W3 (`-DCMAKE_CXX_FLAGS="/W3 /EHsc"`, Debug) | **153**: C4996 152, C4319 1. **`App.cpp` C4244: 0** (was 8 at verify4). |

Non-test C4996 (CRT deprecation) at /W3: `Platforms/Windows/WindowsPlatform.cpp:339` (strncpy); `src/App.cpp:532, 852, 1302`; `src/AssemblyPanel.cpp:224`; `src/FileWrite.cpp:73, 139, 200`; `src/HexEditorPanel.cpp:250, 607`; `src/Settings.cpp:89`;
`src/Sh2Operands.cpp:108, 111, 114, 120, 123, 125, 304, 319, 321, 323`; `include/saturnexplorer/SeFilePath.h:106, 109`. The rest (129) are in test files (mostly `getenv`). `SavestateDriver.cpp:192` and `FileRead.h:51` (the `fopen` ones) are no longer listed.

## 6. FileLock regression

`SaturnExplorerFileLockTests`: passes in Debug (3.44 s) and Release (3.45 s). Second process: a small program (`FileLock` from this tree's `FileWrite.cpp`) asked for the lock while a PowerShell process held it with `LockFileEx` for 30 s.

| Case | Result |
|---|---|
| Normal wait | `held=0 after 3.03 s`, `why: another process has held ...\x.lock for more than 3 seconds`, exit 1 |
| After `SetClosing(true)` (as at exit) | `held=0 after 10.01 s`, `why: ... for more than 10 seconds`, exit 1 |
| After the holder released | `held=1 after 0.00 s` |

So a lock held by another process is reported, with the right bound, and never waited on forever.

## Not done

- A Debug `SaturnExplorerSh2GeneratedTests` or a Release run of it: it cannot be built (item 1).
- The `TIMEOUT 120` property of DiffToolTests; the "CRT asserts to stderr" path (no assertion fires now).
- A build from before `292aec7c` to see whether the Log pane was ever missing.
- `build-release.bat` (not asked this round; it builds the same targets and would hit the same error).
