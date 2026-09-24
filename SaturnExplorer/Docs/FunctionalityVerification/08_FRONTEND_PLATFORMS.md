# Frontend / Platform Verification

## macOS — Verified build
Native app bundle built successfully and all 26 CTest tests passed.

## Linux — Partial/Failed suite
SDL2/OpenGL frontend linked successfully. 25/26 tests passed; ISO-builder failure stopped the job before the Xvfb startup smoke step.

## Windows — Failed build
Two confirmed problems:
1. `PanelInteractionTests.cpp` includes POSIX-only `unistd.h`.
2. frontend link fails because `ScspMix.cpp` is omitted from the Windows target while `App::PlaySoundFrame` references `ScspMixVoices`.

## Web — Failed build
Default Emscripten viewer fails in `App.cpp` because `AdoptNewEmulatorInstance` and `mPatchLib` are unavailable in that configuration.

## Recommendation
Keep `.github/workflows/saturn-explorer-verify.yml` permanently. It caught platform regressions that a single-platform build would miss.
