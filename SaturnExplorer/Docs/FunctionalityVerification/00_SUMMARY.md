# Saturn Explorer Functionality Verification

**Date:** 2026-09-24  
**Verified commit:** `b37fa2d320629ff6ebdb25a3eb27396e22533e82`  
**Actions run:** https://github.com/NoahSteam/RomHackingTools/actions/runs/36074484045

Statuses: **Verified** = executed successfully; **Partial** = important automated coverage exists but not full user workflow; **Failed** = executable verification found a failure; **Blocked** = requires real emulator/game/savestate/interactive environment.

## Platform results

| Platform | Result |
|---|---|
| macOS native | **Verified build**; all 26 CTest tests passed |
| Linux native | **Failed**; frontend builds, 25/26 tests pass; ISO builder test fails |
| Windows native | **Failed**; test compile failure plus frontend link failure |
| Web/Emscripten | **Failed**; default viewer does not compile |
| Linux ASan/UBSan | **Failed**; 24/26 pass; ISO failure + UB in shadow call stack |

## Confirmed defects

- **FV-001 High:** Windows frontend references `ScspMixVoices` but `ScspMix.cpp` is omitted from the Windows frontend CMake source list, causing LNK2019.
- **FV-002 Medium:** `PanelInteractionTests.cpp` unconditionally includes `unistd.h`, so MSVC cannot compile the test target.
- **FV-003 High:** default web viewer fails to compile because `App.cpp` references live-only `AdoptNewEmulatorInstance` and unavailable `mPatchLib`.
- **FV-004 Medium:** ISO builder gives zero-length files zero sectors while `IsoFs::FileAt` treats them as spanning one sector. The next file can share the same LBA; Linux exposes this as `FAIL: FileAt resolves BGM01`.
- **FV-005 Medium:** UBSan catches left shift of negative BSR displacement in `Integration/Mednafen/se_mednafen_glue.c:444`.
- **FV-006 Medium:** on wasm32, FrameRecorder's 4 GiB `size_t` byte ceiling converts to 0, breaking Web Live history budgeting.

## System status

| System | Status |
|---|---|
| ABI/lifecycle | Partial |
| Snapshot/state ingestion | Partial |
| VDP1 | Partial |
| VDP2 | Verified for covered synthetic cases |
| Offline savestate inputs | Blocked for real files |
| Live transport | Partial |
| Emulator hooks | Failed under UBSan |
| CPU debugger | Partial |
| Memory debugger | Verified for covered tests |
| Timeline/rewind | Partial |
| Media/disc | Failed |
| ROM-hacking workflow | Partial |
| Native frontend | Failed on Windows; builds on macOS/Linux |
| Web frontend | Failed |
| Real emulator end-to-end | Blocked |

See the per-system files in this directory and `99_MANUAL_VERIFICATION_REMAINING.md`.
