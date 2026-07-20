# Yabause live-tap integration

This lets Saturn Explorer read a **running** Yabause in realtime: Yabause runs a
tiny local-socket server that streams its VDP1/VDP2 VRAM, CRAM, and VDP2 registers
each frame; Saturn Explorer's **LiveDriver** connects and updates every panel live.

It also lets Saturn Explorer **pause and single-step** the running game (the
toolbar's Pause / Step Frame buttons) so you can freeze a frame and inspect it.

It's a small, isolated patch to Yabause — one new module plus a few one-line hook
calls. The memory export doesn't touch emulation logic; the optional pause/step
gate only decides *when* a frame runs, never *how*, so it can't affect game
compatibility.

## Recommended structure: a Yabause fork + `apply.py`

Keep Saturn Explorer and Yabause as **separate repos** (Yabause is GPLv2; don't
vendor its tree here). This directory holds the single source of truth for the
integration — `se_export.{c,h}` and the wire protocol `SeLiveProtocol.h` — plus
`apply.py`, which drops those into a Yabause checkout and inserts the hook calls for
you. (`SeLiveProtocol.h` is also compiled straight into Saturn Explorer's LiveDriver,
so client and server share one file and never drift.)

```sh
# 1. Fork Yabause once (github.com/Yabause/yabause -> your account) and clone it.
git clone https://github.com/<you>/yabause.git
cd yabause && git checkout -b saturn-explorer-live

# 2. From this repo, wire your fork in one command:
python3 /path/to/SaturnExplorer/Integration/Yabause/apply.py /path/to/yabause

# 3. Commit the result on your fork's branch, then build Yabause as usual.
git add -A && git commit -m "Add Saturn Explorer live tap"
```

`apply.py` is **idempotent** (safe to re-run after pulling upstream), inserts every
edit inside `/* --- SE_EXPORT --- */` markers, and supports:
- `--check` — report what it would change, touch nothing.
- `--revert` — remove all edits and the copied files.

When you update `se_export.*` here, re-run `apply.py` on your fork to sync. If a
future Yabause refactor moves an anchor, the script says exactly which hook to add
by hand — the manual steps below are that fallback.

## Manual integration (fallback)

### 1. Copy three files into `yabause/src`
- `se_export.h`
- `se_export.c`
- `SeLiveProtocol.h`  ← copied from this folder (`Integration/Yabause/`), the one
  copy the LiveDriver also compiles against, so the server and client never drift

Add `se_export.c` to Yabause's build (its `src/CMakeLists.txt` `yabause_SOURCES`
list, or your Makefile). On Windows link against `ws2_32` if it isn't already.

### 2. Wire in the hook calls
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
#include "sh2core.h"
    /* ... existing VBlankOUT body ... */
    sh2regs_struct se_msh2, se_ssh2;
    SH2GetRegisters(MSH2, &se_msh2);
    SH2GetRegisters(SSH2, &se_ssh2);
    SeExportSnapshot(Vdp1Ram, Vdp2Ram, Vdp2ColorRam, Vdp2Regs,
                     Vdp1Regs, LowWram, HighWram, VIDSoftGetVdp1FrameBuffer(),
                     &se_msh2, &se_ssh2);
```

`Vdp1Ram`/`Vdp2Ram`/`Vdp2ColorRam` are Yabause's VDP RAM globals; `Vdp2Regs` and
`Vdp1Regs` are its register structs; `LowWram`/`HighWram` are the 1 MiB work-RAM
globals (declared in `memory.h`); `se_msh2`/`se_ssh2` are the master/slave SH-2
register files (`sh2core.h`), feeding the disassembler / Assembly panel. These
names are stable across the Yabause family; if your fork renamed them, pass the
equivalents. Any argument may be NULL to omit that section.

### 2c. (Optional) SH-2 execution breakpoints
To let the Assembly panel's gutter breakpoints and "Run to Here" halt the running
game, register the breakpoint bridge after `SeExportInit()` in `YabauseInit()` and
add three tiny file-scope helpers (all inserted for you by `apply.py`):

```c
static void SeExpBpHit(void *ctx, u32 addr, void *ud) {
    (void)ud;
    SeExportNotifyStop(ctx == (void *)SSH2 ? 1 : 0, (unsigned int)addr);
}
static void SeExpAddExecBp(int cpu, unsigned int addr) {
    SH2AddCodeBreakpoint(cpu ? SSH2 : MSH2, (u32)addr);
}
static void SeExpClearBps(void) {
    SH2ClearCodeBreakpoints(MSH2); SH2ClearCodeBreakpoints(SSH2);
}
/* ... in YabauseInit(), right after SeExportInit(): */
SH2SetBreakpointCallBack(MSH2, SeExpBpHit, NULL);
SH2SetBreakpointCallBack(SSH2, SeExpBpHit, NULL);
SeExportSetBreakpointHooks(SeExpAddExecBp, SeExpClearBps);
```

**Caveat:** Yabause only honours code breakpoints in the **debug SH-2 interpreter**
(`SH2InterpreterExec` with breakpoint checking), not the fast interpreter or a
dynarec core. Select the debug/interpreter CPU core for breakpoints to fire; live
viewing and pause/step work with any core. Memory (read/write) breakpoints
round-trip over the protocol but aren't installed yet — they'd need
`SH2AddMemoryBreakpoint` wiring.

### 2d. (Optional) Hex Editor writes
To let the Hex Editor poke work RAM in the running game, wire the write hook to
Yabause's byte writer (also inserted by `apply.py`):

```c
static void SeExpWriteByte(unsigned int addr, unsigned char val) {
    MappedMemoryWriteByteNocache(MSH2, (u32)addr, (u8)val);
}
/* ... in YabauseInit(), after SeExportInit(): */
SeExportSetMemWriteHook(SeExpWriteByte);
```

Writing byte-by-byte at Saturn addresses keeps big-endian order without a manual
swap. Note the writer is `MappedMemoryWriteByteNocache(SH2_struct*, addr, val)` —
in this Yabause `MappedMemoryWriteByte` is only a function-pointer field on
`SH2_struct`, not a callable 2-arg function (matches how `yabause.c` already does
memory writes). `Nocache` writes straight to memory (bypassing the SH-2 cache), the
conventional debugger poke; the master SH-2 (`MSH2`) is a fine target since both
cores share the bus. Without this hook, Hex Editor edits still apply to Saturn
Explorer's own view of the frame but aren't pushed to the emulator.

### 2b. (Optional) Pause / single-step gate
To enable the Pause and Step Frame buttons, gate each emulated frame on
`SeExportGateFrame()` in Yabause's run loop. It returns 1 when the frame should
run and 0 while the debugger is holding it paused (and releases exactly one frame
per call when single-stepping). It sleeps ~2 ms internally when paused, so just
spin on it — no host sleep needed. In `yabause/src/yabause.c`, at the top of
`YabauseEmulate()`:

```c
#include "se_export.h"

int YabauseEmulate(void) {
    int oneframeexec = 0;
    while (!SeExportGateFrame()) { }   /* held here while paused */
    /* ... existing frame emulation ... */
}
```

Skip this block if you only want live viewing; without it the emulator always
free-runs and the Pause / Step Frame buttons stay disabled. That's the whole patch.

## 3. Build & run
1. Build Yabause as usual (the port you use for Sakura Wars, etc.).
2. Launch it and load the game.
3. Launch Saturn Explorer and click **Connect to Yabause** (or start it with
   `--live` to auto-connect). Everything — VDP Output, VRAM Map, command list,
   registers, textures — now tracks the running game.

## What flows over the wire
Each request is an 8-byte command (a verb — `GET`/`PAU`/`RUN`/`STP`/`BKP` — plus a
little-endian argument). Every command replies with a full snapshot: VDP1 VRAM
(512 KiB), VDP2 VRAM (512 KiB), CRAM (4 KiB), the 288-byte VDP2 register struct,
the VDP1 register image, low + high work RAM (1 MiB each), the VDP1 frame buffer
(256 KiB, the drawn output), a control block, and the SH-2 state (master + slave
`sh2regs_struct`) — the exact bytes Saturn Explorer's savestate loader already
understands (VRAM big-endian, CRAM host-endian, VDP2 the raw struct, work RAM
host-endian so the client byte-swaps each 16-bit word to big-endian). The VDP1
frame buffer is **host-endian** like CRAM: VIDSoft writes native `u16` pixels, so on
a little-endian host the bytes are little-endian (the FB viewer decodes
little-endian by default). The control block carries the paused flag, frame
counter, and a stop event (reason / CPU / PC) so a breakpoint halt jumps the
Assembly panel to the halted PC. The pause/step verbs just set the state
`SeExportGateFrame()` reads; `BKP` syncs the whole breakpoint set (its arg is the
descriptor count, followed by that many 12-byte descriptors); `WRM` pokes work RAM
(arg = byte count N, payload = address + N big-endian bytes). Wire format
(protocol v6): `Integration/Yabause/SeLiveProtocol.h`.

**VDP1 frame buffer source.** The *global* `Vdp1FrameBuffer` in Yabause is only a
fallback — during play every real frame-buffer access is routed through the active
video core, so the global stays blank. VIDSoft keeps the pixels in its own
`vdp1frontframebuffer` / `vdp1backframebuffer` (two 256 KiB RGB555 banks that swap
each frame). `apply.py` therefore adds a small `VIDSoftGetVdp1FrameBuffer()`
accessor to `vidsoft.c` (returning the displayed *front* bank) and passes that to
`SeExportSnapshot`, grabbed at `Vdp2VBlankOUT` (post-swap, so no mid-draw tearing).
Caveat: this is the **software** renderer. VIDOGL keeps the frame buffer on the GPU
and would need a read-back — if you run the OpenGL core, the FB section will be
empty until that's added.

## Web (browser) live viewing
The browser can't open a local socket, so the web build reaches Yabause over a
**WebSocket → TCP bridge**. `SeExportInit` also opens a TCP listener on
`127.0.0.1:6845` (POSIX builds) serving the same protocol; the LiveDriver accepts
a `tcp:host:port` endpoint, and under Emscripten its socket calls are tunneled to
a WebSocket proxy. To wire it up:

1. Run a WebSocket-to-TCP relay pointing at the export port, e.g.
   `websockify 8846 127.0.0.1:6845` (or Emscripten's `websocket_to_posix_proxy`).
2. Build a web frontend that includes the LiveDriver (it's excluded from the
   default web target) and enables Emscripten's socket proxying — add the
   `SaturnExplorerLiveDriver` source + `SE_ENABLE_LIVE`, and link with
   `-sPROXY_POSIX_SOCKETS -pthread -sWEBSOCKET_URL=ws://localhost:8846`
   (pthreads need a COOP+COEP cross-origin-isolated host for SharedArrayBuffer).
3. In the page, connect to `tcp:127.0.0.1:6845`.

The default single-file web build stays viewer-only (no live driver, no pthread),
so this is opt-in and doesn't destabilize it. The TCP transport itself is
exercised natively in the mock-server test; only the browser tunnel needs your
relay.

## Notes / limits
- **Transport:** Unix domain socket `/tmp/saturn_explorer.sock` (Linux/macOS) or
  named pipe `\\.\pipe\SaturnExplorer` (Windows); plus a local TCP listener on
  `127.0.0.1:6845` (POSIX) for the web bridge. Local only.
- **Pause / single-step** need the §2b gate; live viewing works without it.
- **Disc access** isn't exported yet; an easy follow-on.
- The snapshot is taken at vblank under a lock and double-buffered, so the client
  always reads a whole, consistent frame.
