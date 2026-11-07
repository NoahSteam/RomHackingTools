# Windows verify report 7, 2026-11-07

Branch `windows/verify7-2026-11-07`, base origin/master `41b69ebf` ("Merge area 7 round 8 fixes"). Nothing in the source was changed; this branch adds only this report.
Master was not touched. Console only; no GUI, no emulator, no user settings touched.

Machine: Windows 11 Home, VS 2022 Build Tools, MSVC 19.44.35221 (toolset 14.44.35207), x64. (The verify6 report said 14.38.33130; the compiler on this machine is newer now.)
Fresh build directories under `%TEMP%`: `b7` Release, `b7d` Debug (CI-style), `b7w` Debug at /W3.

## Summary

| # | Item | Result |
|---|---|---|
| 1 | Release, CI-style Debug and /W3 builds | **All three: 0 errors.** `SaturnExplorerSh2GeneratedTests` builds (Release and Debug). **C4319 is gone.** No warning at all in SaturnExplorer files at the default level (/W1). Engine: 42 unique, unchanged. |
| 2 | `ctest -C Release` / `ctest -C Debug` | **92 of 92 pass in both** (Release 325.6 s, Debug 1519.3 s). `Sh2GeneratedTests` passes: Release 31.2 s, Debug 200.1 s. See "95 vs 92" below: 95 needs a `g++` on PATH; with one, the 3 extra tests pass too, so **95 of 95**. |
| 3 | `build-release.bat` | **"Build succeeded"**, exit 0, 0 errors, 0 warnings in SaturnExplorer files. It builds in the existing `build\` directory, so it was incremental. |
| 4 | `check-literal-size.py` | `raw string sizes: ok`, exit 0, on this CRLF checkout (`Sh2GeneratedTests.cpp` has CRLF line ends). |

## 1. Builds

- Release: `cmake -S . -B %TEMP%\b7 -G "Visual Studio 17 2022" -A x64`, `cmake --build ... --config Release --parallel 8`: configure 0, build 0.
- Debug as CI does (`-DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON`, `SaturnExplorerGhidraDecomp` alone, then everything with `--parallel 4`): configure 0, engine 0, full build 0.
- /W3: `-DCMAKE_CXX_FLAGS="/W3 /EHsc"`, Debug, `--parallel 8`: build 0.

Warnings (unique file/line/code):

| Build | Engine (`SaturnExplorerGhidraDecomp` + `se-sleighc`) | SaturnExplorer files |
|---|---|---|
| Release /W1 | 42: C4101 22, C4005 9, C4146 7, C4099 2, C4996 1, C4838 1 | **0** |
| Debug /W1 | 42 (same) | **0** |
| Debug /W3 | 42 (same) | **152, all C4996** (CRT deprecation, same sites as verify6: `WindowsPlatform.cpp:339`, `App.cpp:532,852,1302`, `AssemblyPanel.cpp:224`, `FileWrite.cpp:73,139,200`, `HexEditorPanel.cpp:250,607`, `Settings.cpp:89`, `Sh2Operands.cpp:108,111,114`, plus test files). verify6 had 153 = these 152 + the C4319, which is fixed. `App.cpp` C4244: 0. |

One MSBuild notice, not a source warning, as before: `MSB8029` (build dir under the Temp directory).

## 2. ctest

`ctest --test-dir %TEMP%\b7 -C Release --output-on-failure`: `100% tests passed out of 92`, 325.58 s.
`ctest --test-dir %TEMP%\b7d -C Debug --output-on-failure`: `100% tests passed out of 92`, 1519.27 s. (`Sh2DecompilerEngineTests` 365 s, `Sh2GeneratedTests` 200 s in Debug.)
Also pass: `FileLockTests` 3.45 s, `LogPanelTests` 0.08 s, `DiffToolTests` 0.33 s, `Utf8PathTests` 0.07 s.

### 95 vs 92 (environment, not a regression)

verify6 saw 95 registered tests; a plain configure here registers **92**, and the same 92 at the verify6 base `cd3e6c19` (I configured it in a throwaway worktree and diffed `ctest -N`: identical lists). The three missing are
`SaturnExplorerMednafenPadInjection`, `SaturnExplorerMednafenVdp1Latch` and `SaturnExplorerMednafenVdpPoke`: `CMakeLists.txt` (around line 1495) registers them only when `find_program(SE_CXX_FOR_PADTEST NAMES g++ c++ clang++)` finds a compiler, and this shell had none on PATH. So **"95" depends on a `g++`/`c++`/`clang++` on PATH at configure time; without one the expected number on Windows is 92.** The coordinator's "expect 95 of 95" holds only on a machine with one.

I then pointed CMake at the MSYS2 `g++` (`-DSE_CXX_FOR_PADTEST=C:/msys64/mingw64/bin/g++.exe`) in both trees: 95 tests registered, and the three pass with `C:\msys64\mingw64\bin` on PATH (Release: 2.11 s, 1.23 s, 3.51 s; Debug likewise).

A trap worth knowing: if CMake is given that `g++` path **without** its folder on PATH, the compiler cannot find its DLLs (libwinpthread etc.); the three tests then fail, and Windows shows a "missing DLL" system-error dialog for each attempt. That happened once during this run (my mistake: I ran them without the PATH entry), and the user saw the dialogs. The tests need the MinGW `bin` directory on PATH, not just the full path to `g++.exe`. Worth a line in `Docs/BUILD.md` or a PATH guard in the test command if the Windows CI is expected to run them.

## 3. build-release.bat

`build-release.bat` (no argument), output to `build\bin\Release`: ends with `=== Build succeeded ===`, exit 0. 0 `error`, 0 SaturnExplorer warnings. It reuses the existing `build\` (configured earlier, so it was incremental); `build\bin\Release\SaturnExplorerFrontEnd.exe` was relinked (6,639,104 bytes). The first attempt of mine failed only because my wrapper batch file called it by a relative name; the second, with the full path, is the one reported.

## 4. check-literal-size.py

`python -I .github\scripts\check-literal-size.py SaturnExplorer\FrontEnd SaturnExplorer\Drivers SaturnExplorer\include` prints `raw string sizes: ok`, exit 0.

## Not done / limits

- The GUI was not run this time (not requested); no emulator.
- `TIMEOUT 120` on DiffToolTests and the "CRT asserts to stderr" path are still unchecked (as in verify6).
- Leftovers on disk: `%TEMP%\b7`, `b7d`, `b7w` build directories, my `C:\se3` folder from verify6, and idle MSBuild nodes. I stopped nothing that I did not start.
