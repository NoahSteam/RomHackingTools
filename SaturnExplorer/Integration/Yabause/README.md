# Yabause live-tap integration

This lets Saturn Explorer read a **running** Yabause in realtime: Yabause runs a
tiny local-socket server that streams its VDP1/VDP2 VRAM, CRAM, and VDP2 registers
each frame; Saturn Explorer's **LiveDriver** connects and updates every panel live.

It's a small, isolated patch to Yabause — one new module plus three one-line hook
calls. It does not touch emulation logic, so it can't affect game compatibility.

## 1. Copy three files into `yabause/src`
- `se_export.h`
- `se_export.c`
- `SeLiveProtocol.h`  ← from this repo's `Drivers/Common/src/SeLiveProtocol.h`
  (kept single-source there so the server and the client never drift)

Add `se_export.c` to Yabause's build (its `src/CMakeLists.txt` `yabause_SOURCES`
list, or your Makefile). On Windows link against `ws2_32` if it isn't already.

## 2. Wire in three calls
In `yabause/src/yabause.c`:

```c
#include "se_export.h"

int YabauseInit(yabauseinit_struct *init) {
    /* ... existing init ... */
    SeExportInit();                 /* add at the very end, before `return 0;` */
    return 0;
}

void YabauseDeInit(void) {
    SeExportDeinit();               /* add near the top */
    /* ... existing deinit ... */
}
```

In `yabause/src/vdp2.c`, at the end of `Vdp2VBlankOUT(void)` (fires once per
frame, after the frame is drawn):

```c
#include "se_export.h"
    /* ... existing VBlankOUT body ... */
    SeExportSnapshot(Vdp1Ram, Vdp2Ram, Vdp2ColorRam, Vdp2Regs,
                     Vdp1Regs, LowWram, HighWram);
```

`Vdp1Ram`/`Vdp2Ram`/`Vdp2ColorRam` are Yabause's VDP RAM globals; `Vdp2Regs` and
`Vdp1Regs` are its register structs; `LowWram`/`HighWram` are the 1 MiB work-RAM
globals (declared in `memory.h`). These names are stable across the Yabause family;
if your fork renamed them, pass the equivalents. Any argument may be NULL to omit
that section. That's the whole patch.

## 3. Build & run
1. Build Yabause as usual (the port you use for Sakura Wars, etc.).
2. Launch it and load the game.
3. Launch Saturn Explorer and click **Connect to Yabause** (or start it with
   `--live` to auto-connect). Everything — VDP Output, VRAM Map, command list,
   registers, textures — now tracks the running game.

## What flows over the wire
Per request, the server sends: VDP1 VRAM (512 KiB), VDP2 VRAM (512 KiB), CRAM
(4 KiB), the 288-byte VDP2 register struct, the VDP1 register image, and low + high
work RAM (1 MiB each) — the exact bytes Saturn Explorer's savestate loader already
understands (VRAM big-endian, CRAM host-endian, VDP2 the raw struct). Wire format:
`Drivers/Common/src/SeLiveProtocol.h`.

## Notes / limits
- **Transport:** Unix domain socket `/tmp/saturn_explorer.sock` (Linux/macOS) or
  named pipe `\\.\pipe\SaturnExplorer` (Windows). Local only.
- **Disc / frame-step** aren't exported yet; easy follow-ons.
- The snapshot is taken at vblank under a lock and double-buffered, so the client
  always reads a whole, consistent frame.
