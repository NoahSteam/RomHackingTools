# Windows verify report 2, 2026-11-05: the Windows FileLock (A9-40, A9-42, A9-43)

Branch `windows/verify2-2026-11-05`, base origin/master `c5268769`. Nothing in the source was changed; this branch
adds only this report and one screenshot. Master was not touched.

Machine: Windows 11 Home, VS 2022 (MSVC 14.38). Release build of the CMake tree, `SaturnExplorerFrontEnd.exe`.

## Summary

| # | Check | Result |
|---|---|---|
| 1 | CMake Release build + ctest | **89 of 89 pass** (281 s). Build clean. No new test compiled on Windows. |
| 2 | Hold `saturn_functions.txt.lock` 10 s, save from the app | **Pass, with one wording caveat.** The save is refused after ~3.2 s with a Log error naming the lock. Details below. |
| 2b | After release, next save writes the change; name survives | **Pass.** `alpha`, `beta` (refused) and `gamma` (next save) are all in the file. |
| 3 | Lock file impossible to create | **Pass.** Two cases (lock path is a directory; lock file read-only). Log gives `cannot create or lock <path>: Ac...` (Access is denied). Last words truncated on screen, see note. |
| 4 | Exit writes `saturn_function_bounds.txt` after a refused function-start save (A9-43) | **Pass.** |

User settings backed up and restored byte-identical (SHA-256 prefixes `1D7CC054...` and `428E2FB9...`).

## 1. Build and ctest

`cmake -S . -B build -G "Visual Studio 17 2022" -A x64`, `cmake --build build --config Release --parallel 8`:
configure 0, build 0. `ctest --test-dir build -C Release --output-on-failure --timeout 900`:
**100% tests passed out of 89**, total test time 281.4 s.

The count did not move. `FrontEnd/tests/Area9Round4Tests.cpp` is new, but `CMakeLists.txt:911-926` registers
`SaturnExplorerArea9Round4Tests` inside the Linux-only block (next to LoaderFuzzTests), so it is not built or run on Windows.
That file holds the tests for A9-40's wait and refusal text (its header comment at line 193: "Another process holds
<file>.lock for five seconds"). **So the Windows `LockFileEx` branch has no automated coverage here; the manual run below is
the only check of it.** (Classification: expected, but worth knowing.)

## 2. The 3 s bounded wait (A9-40)

Method: started the exe with its working directory `%TEMP%\v2\start`, opened Call Stack, **Load Symbols...**, filled the Open
dialog with a symbol file in another folder (`symsA.txt`: alpha at 06001000, beta at 06002000) but did not press Open yet. Started
a second process that takes `LockFileEx` (exclusive, whole file, `hold.ps1`) on `start\saturn_functions.txt.lock` for 13 s.
Three seconds later pressed Open (`BM_CLICK`, at 17:24:08.458) and ran a probe that calls `SendMessageTimeout(WM_NULL, 1 s)` on
the main window every 100 ms.

- **Window responsiveness.** The probe's first two calls timed out at 1 s each (`ok=False`, so the UI thread was not pumping),
  the third answered after 498 ms, and every call after that returned in under 20 ms, for the rest of the 10 s hold. Windows'
  own hung-window check (`IsHungAppWindow`, 5 s threshold) never fired. **So the window is blocked for about 3 s, then
  responsive while the lock is still held** (the lock was held until 17:24:18.9, about 10.5 s after Open). It is not frozen for
  the whole hold as in the first report. The caveat: the wait loop runs on the UI thread (`FileLock` constructor, `Sleep(20)`
  retries), so the window does not repaint or take input during those 3 s. If "stays responsive" means no pause at all, it does
  not hold; if it means "no longer hangs until the holder lets go", it does.
- **Refusal time.** The Log error is stamped 17:24:11.66, 3.2 s after Open.
- **Log text** (screenshot `2026-11-05-verify2-log.png`, first ERROR row): `Could not save the function names: The function names:
  not saved, another process has held C:\Users\16306\AppData\Local\Temp\v2\start\saturn_functions.txt.loc` and then the panel edge
  cuts it. The rest comes from `StoreMerge.h:86`: `k for more than 3 seconds; saving without the lock could lose another
  instance's entries, so the file is left as it is and this session's changes are kept in memory only`. It names the lock file
  and the 3 s. The row directly below is the INFO `Imported 2 symbols from ...symsA.txt`, so the import itself still took effect
  in memory.
- **Cosmetic:** the prefix is doubled, `Could not save the function names: The function names: not saved`. Both the caller
  (`App.cpp:371`) and `SaveStore` (`what` = "The function names") add the subject. Not a bug.
- **Data file during the hold.** `saturn_functions.txt` did not exist, so the refused save wrote nothing.

**2b. After release.** The holder released at 17:24:18.9. At 17:25:09 I imported `symsB.txt` (gamma at 06003000), which was the
next save. `saturn_functions.txt` then held:

```
06001000 alpha_func
06002000 beta_func
06003000 gamma_func
```

So the two names from the refused save were kept in memory and written by the next save, together with the new one.

## 3. Lock file cannot be created

Both cases: a pending change, then another import.

| Case | How | Log line (visible part) |
|---|---|---|
| a | `saturn_functions.txt.lock` replaced by a directory of the same name | `Could not save the function names: The function names: not saved, cannot create or lock C:\Users\16306\AppData\Local\Temp\v2\start\saturn_functions.txt.lock: Ac` |
| b | `saturn_functions.txt.lock` a normal file with the read-only attribute | same text, same truncation |

The reason is `std::system_category().message(GetLastError())` (`FileWrite.cpp:430`), so the cut-off remainder is the system's text for
`ERROR_ACCESS_DENIED`, "Access is denied." I could read only the first two letters ("Ac") because the Log row is wider than the panel
and the Log has no copy or wrap (Ctrl+C on a row copied nothing, right-click only selects it). I did not read the last words off the screen.
Both cases left `saturn_functions.txt` unchanged (three lines), so nothing was written without the lock. The app did not hang or pause in
either case.

What I did not test: a folder made unwritable by an ACL (deny create-file). I did not change any permissions. The two cases above hit the
same `CreateFileA` failure path (`ERROR_ACCESS_DENIED`), but not through a folder permission. A deleted startup folder could not be tried either,
because Windows will not delete or rename a running process's current directory.

## 4. Exit writes the function starts (A9-43)

Needs a function in the SH-2 Pseudocode panel. I had no game image, so I made a raw memory dump: 3 MiB, six SH-2 words at
`0x00201000` (low WRAM): `mov.l r1,@r4; mov.l r2,@(4,r4); mov.l r3,@(8,r4); mov.l r4,@(12,r4); rts; mov r5,r0`. (A first try with the code at
`0x06001000` in a 97 MB file failed with "Could not load the selected memory dump": the loader caps a file at 64 MiB,
`kMaxSavestateBytes`, `SavestateDriver.cpp:172`. Low WRAM fits.) Loaded through Session > Load Dump > Raw Memory Dump..., then in
SH-2 Pseudocode: Follow PC off, entry `00201000`. The panel decompiled it.

1. Started the holder on `start\saturn_function_bounds.txt.lock` for 25 s (17:27:27 to 17:27:52).
2. Right-clicked the line `param_1[1] = in_r2;`, chose **Set Function Start Here** (address `0x00201002`, even, not the entry).
3. The app paused about 3 s, then the Log showed `17:27:33.53 ERROR Could not save saturn_function_bounds.txt: The function starts: not ...`.
   `saturn_function_bounds.txt` did not exist. The panel did move to the new function (`func_0x00201002`), so the in-memory start was kept.
4. After the holder released (17:27:52), closed the window (`WM_CLOSE`). The process exited.
5. Result at 17:28:06:

```
; Function entries set by hand (Saturn Explorer, SH-2 Pseudocode)
00201002
```

`saturn_function_bounds.txt` was written by `App::Shutdown` (`App.cpp:394-398`). The same exit also wrote the two names that two earlier
refused saves had left pending (`delta` at 06004000 from case a, `epsilon` at 06005000 from case b), so `saturn_functions.txt` ended
with five lines (alpha, beta, gamma, delta, epsilon). That is the A9-43 behaviour for both stores.

## Other observations

- Running the new build created `%APPDATA%\SaturnExplorer\settings.ini.lock` (0 bytes), which was not there before. It is the lock file of
  the settings merge (`Settings.cpp:239`). I deleted it with the rest of my cleanup, since I created it. Expect users to see it; it
  is stay-behind by design, like the `.lock` files next to the data files.
- Running ctest changed the mtime of `%APPDATA%\SaturnExplorer\states` again (that is A12-15, `SavestateSlotsTests`, already known).
- The Log row is truncated at the panel edge with no way to read the end of a long error short of making the panel wider. A wrap or a
  tooltip on the Log row would have let me read "Access is denied." directly.

## Not done

- No ACL-denied folder case (see section 3).
- The lock holder was a PowerShell process using `LockFileEx` on the same range as the app (whole file, exclusive). A second Saturn Explorer
  instance as the holder was not used this time (the first report did two instances, sequentially).
- Only the function-names and function-starts stores were driven; assembly comments use the same `SaveStore` path but were not exercised.
