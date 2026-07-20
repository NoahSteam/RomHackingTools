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
vendor its tree here). This directory holds the **Yabause-specific** patcher —
`apply.py` — plus this README. The portable pieces it installs (`se_export.{c,h}`
and the wire protocol `SeLiveProtocol.h`) live one level up in
[`../Common/`](../Common), shared with every other emulator's patcher;
`apply.py` copies them from there into your Yabause checkout and inserts the hook
calls. (`SeLiveProtocol.h` is also compiled straight into Saturn Explorer's
LiveDriver, so client and server share one file and never drift.)

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
All three come from [`../Common/`](../Common) (the portable, shared source of truth):
- `se_export.h`
- `se_export.c`
- `SeLiveProtocol.h`  ← the one copy the LiveDriver also compiles against, so the
  server and client never drift

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
(protocol v6): `Integration/Common/SeLiveProtocol.h`.

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

1. Build the web frontend with live mode on:
   `emcmake cmake -B build-web -DSE_WEB_LIVE=ON && cmake --build build-web`. This is
   the `SE_WEB_LIVE` CMake option — it links the `SaturnExplorerLiveDriver`, defines
   `SE_ENABLE_LIVE`, and adds `-pthread -sPROXY_POSIX_SOCKETS -sWEBSOCKET_URL=…` for
   you. (The default web build leaves it **off** and stays viewer-only.)
2. Serve the page from a **cross-origin-isolated** host — pthreads need
   SharedArrayBuffer, which requires the `Cross-Origin-Opener-Policy: same-origin` +
   `Cross-Origin-Embedder-Policy: require-corp` headers. **GitHub Pages can't set
   those**, so the live build needs your own server (or a local dev server that adds
   them); the Pages deploy stays viewer-only.
3. Run a WebSocket-to-TCP relay pointing at the export port, e.g.
   `websockify 8846 127.0.0.1:6845` (or Emscripten's `websocket_to_posix_proxy`).
4. Open the page and **File → Connect to emulator (live)** — the browser build
   defaults to `tcp:127.0.0.1:6845` (`SE_LIVE_DEFAULT_TCP_ENDPOINT`), tunneled over
   the relay.

The TCP transport itself is exercised natively in the mock-server test; only the
browser tunnel needs your relay.

## Notes / limits
- **Transport:** Unix domain socket `/tmp/saturn_explorer.sock` (Linux/macOS) or
  named pipe `\\.\pipe\SaturnExplorer` (Windows); plus a local TCP listener on
  `127.0.0.1:6845` (POSIX) for the web bridge. Local only.
- **Pause / single-step** need the §2b gate; live viewing works without it.
- **Disc access** isn't exported yet; an easy follow-on.
- The snapshot is taken at vblank under a lock and double-buffered, so the client
  always reads a whole, consistent frame.

## Window-title mark
`apply.py` also appends `(SaturnExplorer Enabled. <SE ver> / Yabause <VERSION>)` to the
window title so a tapped build is obvious. This lives in the **Qt port**
(`src/qt/main.cpp`, right after Yabause sets the title from its app name), using the
shared `SeExportTitleSuffix()` helper in `se_export.c`.

**On Windows this is already your frontend:** modern Yabause has no separate native
Win32 GUI — the official Windows binaries are the Qt build — so the standard `apply.py`
run marks the title with no extra steps. The mark is skipped gracefully only on ports
that genuinely lack `qt/main.cpp` (GTK, Cocoa, libretro, etc.); for one of those, add
the one line wherever that port sets its window caption:
```c
extern "C" const char* SeExportTitleSuffix(const char*, const char*);  /* at file scope */
/* right after the port sets its title, append: */  SeExportTitleSuffix("Yabause", VERSION)
```

## Distribution & building
Shipping a patched emulator to users? See [`../DISTRIBUTION.md`](../DISTRIBUTION.md) for
the fork-and-pin guidance (keep `apply.py` anchored to a known upstream commit) and the
prebuilt-binary rules — Yabause is fine to distribute with source (GPLv2), and every
build still needs a user-supplied Saturn BIOS.
