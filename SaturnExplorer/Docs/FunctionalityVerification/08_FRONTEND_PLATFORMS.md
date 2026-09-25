# Frontend / Supported Platform Verification

Saturn Explorer supports **Windows and macOS only**.

## macOS — Verified build

The native macOS app bundle built successfully and all 26 CTest tests passed.

This verifies compile/link integration for the supported macOS target, including App, panels, SDL2/OpenGL backend, native menu source, live driver, audio mix source, rewind, patch library, and disc tools.

Interactive whole-app workflows still require manual/fixture-backed verification.

## Windows — Failed build

Two confirmed problems block current supported-platform verification:

1. `FrontEnd/tests/PanelInteractionTests.cpp` includes POSIX-only `unistd.h`, so the test target does not compile under MSVC.
2. `SaturnExplorerFrontEnd` fails to link `ScspMixVoices` because `FrontEnd/src/ScspMix.cpp` is omitted from the Windows frontend target source list.

Until these are fixed, the Windows supported build is not verified.

## Linux / Web

Linux and Web are not supported Saturn Explorer release platforms.

They may still be useful as development portability/sanitizer environments, but failures there must not be reported as supported-platform failures.

## CI policy

`.github/workflows/saturn-explorer-verify.yml` should gate only:

- `windows-latest`
- `macos-latest`

Development-only portability or sanitizer workflows should be kept separate if retained.
