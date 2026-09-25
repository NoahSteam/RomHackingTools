# Saturn Explorer Functionality Verification

**Supported platforms:** Windows and macOS only  
**Initial execution pass:** 2026-09-24  
**Initial verified commit:** `b37fa2d320629ff6ebdb25a3eb27396e22533e82`  
**Initial Actions run:** https://github.com/NoahSteam/RomHackingTools/actions/runs/36074484045

Statuses: **Verified** = executed successfully; **Partial** = important automated coverage exists but not full user workflow; **Failed** = executable verification found a failure; **Blocked** = requires real emulator/game/savestate/interactive environment.

## Supported-platform result

| Platform | Result |
|---|---|
| macOS | **Verified build**; native app bundle built and all 26 CTest tests passed |
| Windows | **Failed**; test compile failure plus frontend link failure |

Linux and Web are **not supported Saturn Explorer platforms** and are not release/functionality gates. Results observed there are retained only as development diagnostics because they exposed portable-code defects.

## Confirmed supported-platform defects

- **FV-001 High:** Windows frontend references `ScspMixVoices` but `ScspMix.cpp` is omitted from the Windows frontend CMake source list, causing LNK2019.
- **FV-002 Medium:** `PanelInteractionTests.cpp` unconditionally includes `unistd.h`, so MSVC cannot compile the test target.

## Development diagnostics discovered outside supported platforms

These do not change platform support status, but they identify real code issues worth fixing:

- **FV-003:** default Web/Emscripten target does not compile.
- **FV-004:** ISO builder has an order-dependent zero-length-file LBA overlap exposed on Linux.
- **FV-005:** UBSan catches left-shift of a negative BSR displacement in Mednafen shadow-call-stack tracking.
- **FV-006:** wasm32 converts the 4 GiB FrameRecorder byte ceiling to zero.

## System status

| System | Status |
|---|---|
| ABI/lifecycle | Partial |
| Snapshot/state ingestion | Partial |
| VDP1 | Partial |
| VDP2 | Verified for covered synthetic cases |
| Offline savestate inputs | Blocked for real files |
| Live transport | Partial |
| Emulator hooks | Partial; sanitizer diagnostic found UB |
| CPU debugger | Partial |
| Memory debugger | Verified for covered tests |
| Timeline/rewind | Partial |
| Media/disc | Partial; development diagnostic found ISO-builder bug |
| ROM-hacking workflow | Partial |
| macOS frontend | **Verified build + tests** |
| Windows frontend | **Failed build** |
| Real emulator end-to-end | Blocked |

See the per-system files in this directory and `99_MANUAL_VERIFICATION_REMAINING.md`.
