# Remaining Manual / Fixture-Backed Verification

**Supported platforms: Windows and macOS only.**

A feature should not be marked Verified until it has a repeatable automated fixture or documented manual reproduction tied to a build SHA.

## Real state files

Yabause, Yaba Sanshiro, Kronos, Mednafen, Beetle Saturn, full/region dumps.

## Real graphics

Golden `.sedump` captures for VDP1/VDP2 modes, windows, rotation, color calculation, interlace/high-res and mixed priorities.

## Patched emulator

Connect/reconnect, pause/resume, frame/instruction stepping, Step Over/Out, both SH-2 CPUs, conditional BPs, watchpoints, tracepoints, call stack, controller injection, all memory writes, audio/CD status, slots, rewind + scrub edits.

## Real disc workflow

Search/mapping -> memory edit -> generated patch with baseline protection -> BIN/CUE rebuild -> verify every track -> boot modified image.

## Supported platform UI

After Windows build defects are fixed, verify on both supported platforms:

### Windows
- launch and shutdown
- D3D11 rendering
- native menus
- file dialogs
- audio playback
- live emulator connection
- keyboard/controller input
- debugger interactions

### macOS
- launch and shutdown
- SDL2/OpenGL rendering
- native menu bar
- file dialogs
- audio playback
- live emulator connection
- keyboard/controller input
- debugger interactions

Linux and Web do not require release verification.
