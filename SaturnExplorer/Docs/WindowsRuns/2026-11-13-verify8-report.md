# Windows verify report 8, 2026-11-13

Branch `windows/verify8-2026-11-13`, base origin/master `3c39e2c2` (tag `v1.0`, "MappingOracle.h: drop C++17 if-initializer"). Nothing in the source was changed; this branch adds only this report.
Master was not touched. Console only; no GUI, no emulator, no user settings touched.

Machine: Windows 11 Home, VS 2022 Build Tools (MSVC 19.44), CMake 4.4.0, x64. Fresh build directories under `%TEMP%`: `b8` Release (plain MSVC, no `g++` found), `b8d` Debug CI-style with `-DSE_CXX_FOR_PADTEST=C:/msys64/mingw64/bin/g++.exe`.
The shell PATH had **no** MSYS2/MinGW folder in it (checked: 0 entries), i.e. the exact A12-29 scenario.

## Summary

| # | Item | Result |
|---|---|---|
| 1 | A12-29: g++ by full path, its bin folder NOT on PATH | **Fixed.** `SaturnExplorerMednafenPadInjection`, `...Vdp1Latch`, `...VdpPoke` ran and **passed** (2.27 s, 0.50 s, 1.26 s in the full Debug run); **no "missing DLL" dialog** (none seen while they ran; the Windows session was not driven by anyone). Each of the three carries `ENVIRONMENT_MODIFICATION = PATH=path_list_prepend:C:/msys64/mingw64/bin` (read from `ctest --show-only=json-v1`). Registered: **92** on plain MSVC (Release tree), **95** with the g++ (Debug tree). Ran: 92 of 92 and 95 of 95. |
| 2 | A12-5: engine (`SaturnExplorerGhidraDecomp`) at /W3 | **0 warnings.** The vcxproj has `WarningLevel Level3`; `ghidra-decompiler/CMakeLists.txt:21,27` adds `/W3` and `/wd4101 /wd4005 /wd4146 /wd4099 /wd4838 /wd4996`. Built three ways (Release, Debug engine alone, Debug full): 0 warnings each (was 42). `se-sleighc` is covered too: its 12 warnings are gone. |
| 3 | Build + full ctest | **Release: build 0 errors, 0 warnings, `100% tests passed out of 92`, 399.67 s.** **Debug (CI-style): engine step exit 0, full build exit 0, 0 errors, 0 warnings, `100% tests passed out of 95`, 1672.12 s.** `Sh2GeneratedTests` passes: Release 56.5 s, Debug 255.0 s. |

"0 warnings" means 0 lines with `warning`, apart from `MSB8029` (build directory under `%TEMP%`; a MSBuild notice, not a source warning, present in every Windows run since verify3).

## 1. A12-29 in detail

Setup: `cmake -S . -B %TEMP%\b8d -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON -DSE_CXX_FOR_PADTEST=C:/msys64/mingw64/bin/g++.exe`, run from a shell whose PATH has no `C:\msys64\...`.
Before running them I read the three tests' properties (so as not to trigger a dialog if the fix were missing); then ran them alone:

`ctest --test-dir %TEMP%\b8d -C Debug -R "Mednafen(PadInjection|Vdp1Latch|VdpPoke)" --output-on-failure --timeout 120` -> 3 of 3 passed (3.03 s, 0.54 s, 1.42 s), and again in the full run.
For comparison, verify7 (before the fix) had these same three fail with the compiler's DLL dialogs when PATH lacked the folder.
Plain-MSVC case: the Release tree has no `g++` on PATH, so the three are simply **not registered** (92 tests), with no failure and no dialog.

## 2. Builds and warnings

| Build | Errors | Warnings (excl. MSB8029) |
|---|---|---|
| Release (`b8`, `--parallel 8`) | 0 | 0 |
| Debug engine alone (`--target SaturnExplorerGhidraDecomp`) | 0 | 0 |
| Debug full (`b8d`, `--parallel 4`) | 0 | 0 |

verify7 had 42 engine warnings and 0 in SaturnExplorer files at the default level. The /W3 whole-tree count (152 C4996 in SaturnExplorer files) was **not** re-run this time; only the engine target's own /W3 was in scope.

## 3. ctest

- Release (`b8`): `100% tests passed, 0 tests failed out of 92`, 399.67 s.
- Debug (`b8d`): `100% tests passed, 0 tests failed out of 95`, 1672.12 s.

## Not done / limits

- GUI, emulator and `build-release.bat` were not run (not asked).
- The whole-tree /W3 count, `TIMEOUT 120` on DiffToolTests and the CRT-assert-to-stderr path were not rechecked.
- I could not verify "no dialog" by looking at the screen (no screenshots; console only); the three tests completing within seconds, passing and the earlier dialogs being modal (they would have hung the tests until the 120 s timeout) is the evidence.
- Leftovers: `%TEMP%\b8` and `%TEMP%\b8d`. The cleanup you were asked about for verify7 (`b7*` folders were already gone; `C:\se3` and the idle MSBuild nodes) is still unconfirmed on my side.
