# Windows verify report 5, 2026-11-06

Branch `windows/verify5-2026-11-06`, base origin/master `54aa6c86` ("Merge CI green fixes (A10-13, A12-20..24; A2-125 filed)"). Nothing in the source was
changed; this branch adds only this report. Master was not touched. Console only: no window of the app was opened (one Debug-CRT dialog appeared for about four
seconds in the DiffTool investigation, see section 4; I read its text and killed the process, and did not click anything).

Machine: Windows 11 Home, VS 2022 (MSVC 14.38.33130, v143), x64. Fresh build directories under `%TEMP%` (`b5` Release, `b5d` Debug); `build-release.bat` used the
repo's own `build` folder. User settings were not touched (SHA-256 prefixes `1D7CC054...` / `428E2FB9...` still match the backups).

## Summary

| # | Item | Result |
|---|---|---|
| 1a | CMake Release build (fresh) | **Pass.** Exit 0, 0 errors, 15 m 40 s. |
| 1b | CMake Debug build as CI does (engine target, then everything) | **Pass.** Both steps exit 0, 0 errors (2 m 22 s and 3 m 38 s). |
| 1c | `build-release.bat` as shipped | **Pass.** `=== Build succeeded ===`, exit 0, 10 m 12 s. |
| 2 | Full ctest (Release) | **93 of 93 pass**, 298.9 s. `SaturnExplorerAssemblyPanelTests` is in and passes (0.12 s). |
| 3 | `SaturnExplorerDiffToolTests` | **Passes here in both configurations** (Debug 0.70 s, Release 0.31 s). **The CI hang is reproduced** by making the test's temp folder's parent missing: the Debug test then sits blocked on a Debug-CRT "vector subscript out of range" dialog, forever. Release crashes instead. Cause is in the test, not the product. Details in section 4. |

## 1. Builds

`cmake -S SaturnExplorer -B %TEMP%\b5 -G "Visual Studio 17 2022" -A x64`, `cmake --build ... --config Release --parallel 8`: configure 0, build 0, `239 Warning(s)`, `0 Error(s)`
(the warnings are the same engine and `MSB8029` ones as in verify4's section 2).

CI-style Debug: `cmake -S . -B %TEMP%\b5d -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON`, then `--target SaturnExplorerGhidraDecomp` (exit 0, 132 warnings), then the whole tree
`--parallel 4` (exit 0, 110 warnings, 0 errors).

`build-release.bat` (no argument, newest Visual Studio, x64, `cmake --build build --config Release --parallel`): `=== Build succeeded ===`, `App: ...\build\bin\Release\SaturnExplorerFrontEnd.exe`,
exit 0. `AssemblyPanelTests` now compiles: the `<unistd.h>` include is guarded for POSIX and the file uses `_mkdir` / `_rmdir` on Windows (`AssemblyPanelTests.cpp` lines 24, 49-58), which fixes the error of verify4.

## 2. Full ctest

`ctest --test-dir %TEMP%\b5 -C Release --output-on-failure`: **`100% tests passed, 0 tests failed out of 93`**, total test time 298.89 s. Of note: `40 SaturnExplorerFileLockTests` 3.43 s,
`58 SaturnExplorerAssemblyPanelTests` 0.12 s, `63 SaturnExplorerDiffToolTests` 0.31 s. The count is the expected 93.
Not run: the full Debug ctest (the workflow's own comment says the Debug CRT makes the engine tests very slow); only the DiffTool test was run in Debug, below.

## 3. What was asked about `SaturnExplorerDiffToolTests`

(a) **Debug, `ctest -C Debug -R SaturnExplorerDiffToolTests --timeout 120 --output-on-failure -V`.** Complete output of the test part:

```
63: Test command: C:\Users\16306\AppData\Local\Temp\b5d\bin\Debug\SaturnExplorerDiffToolTests.exe
63: Working Directory: C:/Users/16306/AppData/Local/Temp/b5d
63: Test timeout computed to be: 120
63: DiffTool: all checks passed
1/1 Test #63: SaturnExplorerDiffToolTests ......   Passed    0.70 sec
100% tests passed out of 1
```

(b) **Exe run directly, 2-minute limit, in a console:** Debug exited after 0.5 s printing `DiffTool: all checks passed`; Release the same. No hang on this machine.

(c) **Children and waiting state:** none to report for the normal run (it finishes). For the reproduced hang (section 4): no child processes, 3 threads, 0.1 s of CPU in 120 s,
154 handles, no stdin or pipe involved (the test reads no input and starts no process, `DiffToolTests.cpp` has none of `popen`, `system`, `CreateProcess`, `fork`, `stdin`). It is
blocked in a modal dialog.

(d) **Release:** passes (above). With the reproducing condition it crashes (below).

## 4. Why it hangs on CI, and the reproduction

Reading `FrontEnd/tests/DiffToolTests.cpp` (nothing was changed): `TempRoot()` (lines 28-34) builds its folder from `getenv("TMPDIR")` and, if that is unset, the string
`/tmp`: `/tmp/se_difftool_test_<time>`. On Windows `TMPDIR` is normally unset, and a path starting with `/` means "the root of the current drive": `C:\tmp\...` here, `D:\tmp\...` on a
runner whose workspace is on `D:`. `MakeDirectory` creates one folder level only, so if `\tmp` does not exist on that drive, `MakeDirectory(root)` (line 129) fails, and so does
every write after it. The loop at lines 137-151 then reads files that were never written:

```cpp
CHECK(Read(a + PathSeparator() + DiffRegionFileName(rid), got));   // false, `got` stays empty
CHECK(got == sa.regions[i].bytes);                                  // false
CHECK(got.size() == Traits(rid).size);                              // false
CHECK(got[0] == i + 1);                                             // line 150: got[0] on an EMPTY vector
```

`CHECK` only prints and counts; it does not stop, so line 150 indexes an empty `std::vector`. In a Debug build the MSVC STL asserts on that.
(Lines 225-226, the same pattern, are guarded by `&&` and are safe.)

**Why it passes here:** `C:\tmp` exists on this machine (left by earlier test runs; it holds old `__se_split_settings__*` folders), so `/tmp/...` works.

**Reproduction.** I ran the Debug exe with `TMPDIR=C:\se_no_such_parent_dir` (a folder that does not exist, the same situation as a missing `\tmp`):

- Started directly with a 2-minute limit: **still running after 120 s**, 0.1 s CPU, 3 threads, no child processes, no output. (stdout is a file here, so nothing printed.)
- Re-run, and after 4 s I read the process's window: a modal dialog, class `#32770`, title `Microsoft Visual C++ Runtime Library`, buttons `Abort / Retry / Ignore`, text:

```
Debug Assertion Failed!

Program: ...Data\Local\Temp\b5d\bin\Debug\SaturnExplorerDiffToolTests.exe
File: C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.38.33130\include\vector
Line: 1894

Expression: vector subscript out of range
```

On a CI runner nobody can click it, so ctest waits until its own timeout (1500 s by default). The same dialog is what `Sh2DecompilerEngineTests.cpp:3771` already guards against ("a
Debug-CRT assertion or abort() otherwise opens a message box and waits for a click"). I killed the process; I did not click Abort/Retry/Ignore.
- **Release, same condition:** the exe is gone after about 3 s with an access violation (Application Error event, exception `0xc0000005`, in `SaturnExplorerDiffToolTests.exe`), because the
same `got[0]` is then undefined behaviour instead of an assertion. So in Release the test would fail fast rather than hang, which is why the Release CI job is not affected.

I cannot see the runner, so "the runner has no `\tmp` on its working drive" is an inference from the reproduction, not an observation. It fits every fact given (hangs only in Debug on Windows, 0.09 s on Linux, where `/tmp` exists).

Findings, without changing code: (1) `TempRoot()` should use a folder that exists on Windows (`TEMP`/`TMP`, as `PanelInteractionTests` and `SavestateSlotsTests` were changed to do for A12-15); (2) the loop should stop
(or `got.empty()` be checked) before `got[0]` when `Read` fails, so that a failed write fails the test instead of asserting; (3) the `/tmp` fallback is also what left old `__se_split_settings__*` style folders in `C:\tmp` on this machine
(not from this branch).

## Not done

- The full ctest in Debug.
- The DiffTool hang was not observed on a real runner; section 4 reproduces it by simulating the missing folder.
- Leftover `MSBuild.exe` node processes from my builds were idling when I finished (MSBuild's node reuse); they exit on their own after a few minutes.
