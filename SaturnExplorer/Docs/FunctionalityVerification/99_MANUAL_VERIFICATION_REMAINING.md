# Remaining Manual / Fixture-Backed Verification

A feature should not be marked Verified until it has a repeatable automated fixture or documented manual reproduction tied to a build SHA.

## Real state files
Yabause, Yaba Sanshiro, Kronos, Mednafen, Beetle Saturn, full/region dumps.

## Real graphics
Golden `.sedump` captures for VDP1/VDP2 modes, windows, rotation, color calculation, interlace/high-res and mixed priorities.

## Patched emulator
Connect/reconnect, pause/resume, frame/instruction stepping, Step Over/Out, both SH-2 CPUs, conditional BPs, watchpoints, tracepoints, call stack, controller injection, all memory writes, audio/CD status, slots, rewind + scrub edits.

## Real disc workflow
Search/mapping -> memory edit -> generated patch with baseline protection -> BIN/CUE rebuild -> verify every track -> boot modified image.

## Platform UI after build defects are fixed
Windows D3D11/native menus/audio; macOS native menu/audio; Linux startup; web open/drop/render; optional Web Live bridge.
