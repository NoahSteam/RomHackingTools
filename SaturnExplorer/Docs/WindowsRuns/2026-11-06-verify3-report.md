# Windows verify report 3, 2026-11-06: the FileLock again (A9-46, A9-47, A9-48, A12-18)

Branch `windows/verify3-2026-11-06`, base origin/master `521dc0e5`. No source was changed; this branch adds only this report and one
screenshot. Master was not touched.

Machine: Windows 11 Home, VS 2022. Release build of the CMake tree, `SaturnExplorerFrontEnd.exe`. User settings backed up before and
restored byte-identical after (SHA-256 prefixes `1D7CC054...` settings.ini, `428E2FB9...` imgui.ini).

## Summary

| # | Check | Result |
|---|---|---|
| 1 | CMake Release build + ctest | **92 of 92 pass** (288 s). Build clean. |
| 2 | A9-47: lock path a folder / read-only lock file | **Pass.** Refused at once (0.15 s and 0.11 s after Open, no stall) with `cannot create or lock <path>: Access is denied.` |
| 2b | A9-47: holder for 10 s | **Pass.** UI blocked about 3 s, refused 3.14 s after Open, text names the lock and "more than 3 seconds". |
| 3 | A9-46: lock held 30 s+, close the window | **Pass.** Message box after **10.21 s**; process gone about 0.9 s later (that includes my 0.7 s screenshot wait before clicking OK). No other store waited. |
| 4 | A9-48: FlushFileBuffers, 20 imports | **Pass.** In the app: **127.7 ms average** click-to-file-on-disk. The write itself, timed alone: **3.6 ms with the flush, 2.0 ms without** (+1.6 ms). |
| 5 | A12-18: Log row cut off | **Not fixed in this build.** No tooltip, no wrap, no sideways scroll. I could read it only by widening the panel and using a short folder path. Full text quoted below. |

## 1. Build and ctest

`cmake -S . -B build -DCMAKE_BUILD_TYPE=Release`, `cmake --build build --config Release -j8` (configure 0, build 0, 4 m 45 s), then
`ctest --test-dir build -C Release --output-on-failure`: **100% tests passed, 0 tests failed out of 92**, total test time 287.8 s.
Three more tests than last time (89). `FrontEnd/tests/Area9Round5Tests.cpp` (the A9-46..A9-48 pins) is registered at `CMakeLists.txt:931`
inside a block commented "Linux only" (line 930), so it is **not built or run on Windows**: again the Windows `LockFileEx` branch,
the `ERROR_LOCK_VIOLATION` / `ERROR_SHARING_VIOLATION` filter, the 10 s exit wait and the `MessageBoxA` have no automated coverage here.
`SaturnExplorerFileWriteTests` passed (0.09 s) but cannot see the flush.

## 2. A9-47: only a lock someone holds is waited for

Setup: the app started with working directory `C:\se3` (short on purpose, see section 5), Call Stack tab, **Load Symbols...**, dialog filled
with a one-symbol file, then Open pressed with `BM_CLICK`. A probe called `SendMessageTimeout(WM_NULL, 1 s)` on the main window every
100 ms. Times are the Log row stamp against the click time.

| Case | How | Click | Log row | Latency | UI probe |
|---|---|---|---|---|---|
| a | `saturn_functions.txt.lock` replaced by a **folder** of that name | 19:33:09.125 | 19:33:09.27 | **0.15 s** | all answers under 20 ms |
| b | the lock a normal file with the **read-only** attribute | 19:33:44.228 | 19:33:44.34 | **0.11 s** | all answers under 20 ms |
| c | another process holds `LockFileEx` for 10 s (19:34:08.2 to 19:34:18.2) | 19:34:11.528 | 19:34:14.67 | **3.14 s** | two 1 s timeouts, then answered at 3.5 s; never "hung" |

Log text, cases a and b (identical apart from the stamp; read off the widened panel up to "saving without the l", the rest taken from
source, see section 5):

```
Could not save the function names: The function names: not saved, cannot create or lock C:\se3\saturn_functions.txt.lock: Access is denied.; saving without the lock could lose another instance's entries, so the file is left as it is and this session's changes are kept in memory only
```

Case c (same cut-off on screen, after "saving w"):

```
Could not save the function names: The function names: not saved, another process has held C:\se3\saturn_functions.txt.lock for more than 3 seconds; saving without the lock could lose another instance's entries, so the file is left as it is and this session's changes are kept in memory only
```

So the OS reason is now shown whole (`Access is denied.`; last time I could read only "Ac"), a folder that cannot make the lock no longer costs 3 s,
and a real holder still gets the 3 s wait and a refusal. `saturn_functions.txt` was not created in any of the three. Cosmetic leftovers: the
doubled prefix ("Could not save the function names: The function names: not saved") and a stray `.;` after the system's sentence
("Access is denied.;").

Also seen by accident: closing the app while case a was still in force (lock path a folder, names pending). The exit message box
appeared **0.25 s** after WM_CLOSE, not after 10 s, as expected for a non-retryable error.

## 3. A9-46: exit waits up to 10 s, then says so

Holder started at 19:35:15.0 (locks `saturn_functions.txt.lock` for 40 s, until 19:35:55.0). In the app: Load Symbols (`symsD`, the 3 s
refusal, names pending in memory), then at 19:35:33.30 `WM_CLOSE`.

- The **message box appeared at 19:35:43.51, 10.21 s after the close.** The holder was still holding (released 19:35:55.0).
- Box text, verbatim (read from its controls): `These changes could not be saved and are lost:` / `  the function names` / (blank) /
  `Another program holds their lock file or the disk refused the write; the Log panel has the reason, but it closes with the window.`
  Title `Saturn Explorer`, one OK button.
- I clicked OK 0.7 s after it showed; the process was gone at 19:35:44.38, 11.08 s after the close in total. **Close duration: about
  10.2 s to the box, plus however long the user takes to click OK.**
- **No other store waited.** The total was one 10 s wait, not several. Only `saturn_functions.txt.lock` was held, so the settings, the
  watches (`saturn_explorer_watches.json` was written in `C:\se3`), the assembly notes and the function starts did not wait; the function
  starts had nothing pending. (If more than one had been held, each would add its own 10 s, as `App::Shutdown` saves them one after
  another; I held only one.)
- `saturn_functions.txt` did not exist afterwards: nothing was written without the lock, and the box says so.
- For comparison, a normal exit (nothing pending, no holder) took 0.18 s from the close to the process gone, with no box.

## 4. A9-48: FlushFileBuffers cost

Two measurements, because the app one cannot isolate the write.

**In the app.** 20 imports in a row of 20 different one-symbol files (so the names file changes every time), each: Load Symbols..., filled
dialog, Open. Per import, the time from `BM_CLICK` on Open until `saturn_functions.txt`'s last-write time changed (polled every 2 ms):
158, 124, 132, 125, 122, 123, 142, 127, 126, 125, 124, 127, 126, 126, 126, 125, 124, 122, 125, 125 ms. **Average 127.7 ms, min 122,
max 158.** That is dominated by closing the dialog, parsing and one UI frame, not the write. The file ended with all 20 names.

**The write alone.** A small program linking `FrontEnd/src/FileWrite.cpp` (this branch) and calling `WriteFileAtomically` 20 times on a
57-byte file in `%TEMP%`, against the same program built with `FileWrite.cpp` from `c5268769` (before A9-48), three runs each, `/O2`
(average of 20 per run):

| | run 1 | run 2 | run 3 |
|---|---|---|---|
| with FlushFileBuffers (521dc0e5) | 3.66 ms | 3.54 ms | 3.60 ms |
| without (c5268769) | 2.04 ms | 2.09 ms | 2.01 ms |

So the flush adds about **1.6 ms per save** (3.6 ms vs 2.0 ms) on this machine. Nothing a user could notice.

## 5. A12-18: the Log row

Not fixed in this build. Hovering over a long row only highlights it (no tooltip), there is no wrap, and the mouse wheel (vertical or
horizontal) does not scroll the message column. What worked: drag the splitter between the bottom-left tab group and the SH-2 panel to the
right (Log panel about 1450 px wide), and start the app from a short folder (`C:\se3`). With the earlier long folder
(`C:\Users\16306\AppData\Local\Temp\v3\start`) the row still ended at `...\saturn_functions.txt.lock:`. Even at that width the row ends at
"...saving without the l", so the last part ("ock could lose another instance's entries, so the file is left as it is and this session's
changes are kept in memory only") is quoted from `StoreMerge.h:86-87`, not read from the screen.

Screenshot `2026-11-06-verify3-log.png`: the Log panel widened, INFO hidden, the three refusal rows of cases a, b and c; the third row is the
3-second one. Also: with the default layout the Log panel sits partly below the bottom of an 823 px-high window, so the newest rows are
out of view unless a filter hides the older ones (Auto-Scroll was on).

## Other observations

- The new build again creates `%APPDATA%\SaturnExplorer\settings.ini.lock` (0 bytes); I deleted it after restoring the settings.
- `C:\se3` (my short test folder, holding `saturn_functions.txt`, `saturn_explorer_watches.json` and the lock files) is still on disk:
  the shell refused to delete a path directly under `C:\`. The user can delete it.
- Not done: an ACL-denied folder (no permission changes were made); a second real Saturn Explorer instance as the lock holder (a
  PowerShell `LockFileEx` process stood in); the assembly-notes store under a held lock.
