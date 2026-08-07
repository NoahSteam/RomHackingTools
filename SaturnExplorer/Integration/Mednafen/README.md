# Saturn Explorer live tap for standalone Mednafen

Targets **standard Mednafen** (the standalone emulator) — it hooks `Emulate()` in
`ss.cpp` and builds with autotools (`./configure --enable-debugger && make`). It is
**not** the Beetle Saturn libretro core, which wraps the same `ss/` code behind
`retro_run` and a different build; see [`../DISTRIBUTION.md`](../DISTRIBUTION.md)
("Mednafen vs. Beetle Saturn") for why that split matters.

**Status: patcher verified against a real Mednafen build; data path proven in-repo.**
Symbol names, storage layouts, and hook sites were read from the Mednafen `ss` source
([libretro-mirrors/mednafen-git `src/ss`](https://github.com/libretro-mirrors/mednafen-git/tree/master/src/ss)
— a git mirror of standard Mednafen's Mercurial source, not Beetle),
and `apply.py` was **run against a real Mednafen checkout**: all six code anchors
match, the build wiring lands in `src/ss/Makefile.am`, and the patched tree
**compiles against real Mednafen headers** — `vdp1.o`/`vdp2.o`/`ss.o` (the injected
accessors, against the actual `VDP1::VRAM`/`FB`/regs, `VDP2::VRAM`/`CRAM`/`RawRegs`,
`WorkRAML/H`, `CPU[]`/`SH7095::GSREG_*`, `CheatMemWrite`), plus `se_export.o` and the
wired `se_mednafen_glue.o`. The glue's ten `SsDbg*` externs each resolve to a
definition in the patched objects (nm-verified), so the tap links into real Mednafen.
Separately, the wire transforms round-trip end-to-end against a real Mednafen
savestate (§"Verify"). Tier-3 **execution breakpoints** are wired to Mednafen's
`ss/debug.inc` and link into a `--enable-debugger` build (§"Execution breakpoints");
they fire only in that build. Running a live game additionally needs a Saturn BIOS +
disc, which is the user's to supply.

Saturn Explorer already **loads Mednafen savestates** (`MDFNSVST`) statically —
VDP1/VDP2 VRAM, both register files, and CRAM (see
`Drivers/Savestate/src/SavestateDriver.cpp`). This folder is about the **live tap**:
attaching to a *running* Mednafen the way `Integration/Yabause/` attaches to Yabause.

## Quickstart

```sh
# Point the patcher at a Mednafen checkout (with src/ss/):
python3 /path/to/SaturnExplorer/Integration/Mednafen/apply.py /path/to/mednafen
#   --check       report what it would do, change nothing
#   --with-pause  also inject the pause/step frame gate (see the frame-gate caveat)
#   --revert      remove every edit + copied file
# Then build Mednafen as usual and launch Saturn Explorer with --live.
```

`apply.py` copies the portable server (`../Common/se_export.{c,h}` + `SeLiveProtocol.h`)
and the glue (`se_mednafen_glue.c`, with `SE_MEDNAFEN_WIRED` defined) into `src/ss/`,
appends the accessors (§"Accessors") to `vdp1.cpp`/`vdp2.cpp`/`ss.cpp`, injects one
per-frame call into `Emulate()`, and adds the two C sources to the build. Every edit is
fenced with `SE_EXPORT` markers so re-running is idempotent and `--revert` is exact.

## How little is actually new

The whole live machinery is emulator-agnostic and lives in
[`../Common/`](../Common): the socket server, protocol framing, double-buffering,
pause/step gate, breakpoint sync, and memory-write dispatch are all in `se_export.c`,
which takes **raw pointers + function-pointer hooks — no emulator types**. The
LiveDriver client and the wire protocol are unchanged and need **no edits** to talk
to a Mednafen server (the client already tolerates older protocol versions and
zero-length sections, so a partial Mednafen server degrades gracefully).

So a Mednafen tap = **reuse `../Common/se_export.{c,h}` verbatim** + the glue that maps
Mednafen's `ss` internals to `SeExportSnapshot(...)` + `apply.py` to inject it. The
glue is one screen of code; both are here.

## The Yabause → Mednafen symbol map

`SeExportSnapshot()` (see `../Common/se_export.h`) wants these, once per frame. The
Yabause column is what `Integration/Yabause/apply.py` passes; the Mednafen column is
the real symbol in `mednafen/src/ss/` (read from source — see the tree link above).
**Scope matters here:** almost every Mednafen symbol is *file-scope `static`*, so it
can't be reached by `extern` from the glue's translation unit — the patcher injects a
small accessor next to each (see §"Accessors").

| Snapshot arg | Yabause | Mednafen `ss` | Scope | Notes |
|---|---|---|---|---|
| VDP1 VRAM (512K) | `Vdp1Ram` | `VDP1::VRAM[0x40000]` u16 (vdp1.cpp) | file-scope | host u16 → **swap to big-endian** |
| VDP2 VRAM (512K) | `Vdp2Ram` | `VDP2::VRAM[262144]` u16 (vdp2.cpp) | **static** | host u16 → **swap to big-endian** |
| CRAM (4K) | `Vdp2ColorRam` | `VDP2::CRAM[2048]` u16 | **static** | host order — pass straight |
| VDP2 regs (288) | `Vdp2Regs` struct | `VDP2::RawRegs[0x100]` u16 *"For debugging"* | **static** | layout differs — see "VDP2 registers" |
| VDP1 regs (struct) | `Vdp1Regs` struct | `VDP1::{TVMR,FBCR,PTMR,EWDR,EWLR,EWRR,EDSR,LOPR}` | **mixed static** | accessor fills the 11-u16 `Vdp1` struct; `se_export` builds the hw-offset image. ENDR/COPR/MODR write-only/computed → 0 |
| Work RAM low (1M) | `LowWram` | `WorkRAML[0x80000]` u16 (ss.cpp) | **static** | host u16 (like VRAM) — pass raw, client `Bswap16`s |
| Work RAM high (1M) | `HighWram` | `WorkRAMH[0x80000]` u16 | **static** | host u16 — pass raw (see §Byte order) |
| VDP1 framebuffer (256K) | `VIDSoftGetVdp1FrameBuffer()` | `VDP1::FB[!FBDrawWhich]` (displayed bank of `FB[2][0x20000]`) | file-scope | RGB555 host order |
| Master SH-2 regs | `SH2GetRegisters(MSH2,…)` | `CPU[0]` (`SH7095`, ss.cpp) | file-scope | accessor packs 23 u32 via `CPU[0].GetRegister(GSREG_*)` |
| Slave SH-2 regs | `SH2GetRegisters(SSH2,…)` | `CPU[1]` | file-scope | " (cpu 1) |

All live in namespace `MDFN_IEN_SS` (VDP1 / VDP2 sub-namespaces for the VDP arrays).

Hook points (all in `ss.cpp`, all `static`):

| Purpose | Yabause | Mednafen `ss` |
|---|---|---|
| Init the server | end of `YabauseInit()` | end of `Load(GameFile*)` |
| Per-frame snapshot | `Vdp2VBlankOUT()` | `Emulate(EmulateSpecStruct*)`, after `espec->MasterCycles = …`, before `SMPC_UpdateOutput()` |
| Frame gate (pause/step) | top of `YabauseEmulate()` | top of `Emulate()` — **see caveat** |
| Deinit | `YabauseDeInit()` | `CloseGame(void)` |
| Exec breakpoints | `SH2AddCodeBreakpoint`, `SH2SetBreakpointCallBack` | `ss/debug.inc`: `DBG_AddBreakPoint(BPOINT_PC,…)` + `DBG_SetCPUCallback` (needs `--enable-debugger`) |
| Data breakpoints (read/write watchpoints) | `SH2AddMemoryBreakpoint` | `ss/debug.inc`: `DBG_AddBreakPoint(BPOINT_READ/WRITE,…)`, checked per-insn by `CheckRWBreakpoints` (needs `--enable-debugger`) |
| Memory poke | `MappedMemoryWriteByte` | `CheatMemWrite(A, V)` in ss.cpp — cache-correct bus byte write |

## Accessors (the Mednafen-specific integration wrinkle)

Yabause exposes its memory as extern globals, so its glue reads them directly.
Mednafen keeps VDP2 `VRAM`/`CRAM`/`RawRegs`, `WorkRAML/H`, and the individual VDP1
registers as **file-scope `static`** — invisible to a separate glue `.c`. Mednafen
already anticipates debug taps (`VDP2::PeekVRAM`, the `RawRegs // For debugging`
array), so the patcher follows that lead: it **appends a tiny `extern "C"` accessor
into each file** (a re-opened namespace block at end-of-file, where the static is
still visible), and the glue calls them (its `extern` declarations enable under
`SE_MEDNAFEN_WIRED`). `apply.py` injects these for you; the exact bodies:

```cpp
/* vdp2.cpp — namespace MDFN_IEN_SS::VDP2 */
extern "C" const unsigned short* SsDbgVdp2Vram(void){ return VRAM; }
extern "C" const unsigned short* SsDbgCram(void)    { return CRAM; }
extern "C" const unsigned short* SsDbgRawRegs(void) { return RawRegs; }
/* vdp1.cpp — namespace MDFN_IEN_SS::VDP1 */
extern "C" const unsigned short* SsDbgVdp1Vram(void){ return VRAM; }
extern "C" const unsigned short* SsDbgVdp1Fb(void)  { return FB[!FBDrawWhich]; }
extern "C" void SsDbgVdp1Regs(unsigned short o[11]){ o[0]=TVMR; o[1]=FBCR; o[2]=PTMR;
      o[3]=EWDR; o[4]=EWLR; o[5]=EWRR; o[7]=EDSR; o[8]=LOPR; /*ENDR/COPR/MODR=0*/ }
/* ss.cpp — namespace MDFN_IEN_SS */
extern "C" const unsigned short* SsDbgWramL(void){ return WorkRAML; }
extern "C" const unsigned short* SsDbgWramH(void){ return WorkRAMH; }
extern "C" void SsDbgSh2Regs(int c, unsigned int o[23]){         /* R0..R15, SR,GBR,VBR,MACH,MACL,PR,PC */
      SH7095& p = CPU[c?1:0];
      for (int i=0;i<16;++i) o[i]=p.GetRegister(SH7095::GSREG_R0+i,0,0);
      o[16]=p.GetRegister(SH7095::GSREG_SR,0,0); /*…*/ o[22]=p.GetRegister(SH7095::GSREG_PC_IF,0,0); }
extern "C" void SsDbgPokeByte(unsigned int a, unsigned char v){ CheatMemWrite(a, v); }
```

This is the bulk of what the Mednafen patcher does; the rest is the
five hook calls from the table above.

## Byte order — the one substantive difference from Yabause

Yabause stores VRAM **big-endian** natively, so its glue passes it straight.
**Mednafen stores VRAM host-order** (the savestate parser byte-swaps LE→BE — see
`CopyMednafenU16BE` in `SavestateDriver.cpp`). The wire wants VRAM **big-endian**, so
the Mednafen glue must **byte-swap VDP1/VDP2 VRAM to big-endian** before passing them
to `SeExportSnapshot`. Per section:

- **VDP1/VDP2 VRAM** — stored native `uint16` host-order (accessed big-endian via
  Mednafen's `ne16_*_be` helpers). Swap each 16-bit word to big-endian for the wire.
  (`SwapU16ToBE` in the glue.)
- **CRAM** — `uint16` host order; pass straight. The client normalizes it via RAMCTL
  (mode-aware).
- **Work RAM** — `WorkRAML/H` are `uint16` arrays (host order), the **same storage as
  VDP1/VDP2 VRAM**, just accessed via `ne16_rbo_be`. The wire carries work RAM
  host-order and the client `Bswap16`s it to big-endian (LiveDriver), so pass it raw —
  no glue swap. The in-repo mock proves this round-trips (a host word reads back as the
  right big-endian instruction). One residual: `WorkRAMH`'s comment notes it's
  "effectively 32-bit … 16-bit here because of fastmap" — the 16-bit path matches VRAM,
  but if a 32-bit value ever looks half-word-swapped on a real build, spot-check it.
- **VDP1 framebuffer** — `FB[!FBDrawWhich]`, native `uint16` RGB555 (host order like
  CRAM); pass straight.
- **SH-2 regs** — host-order u32 in `sh2regs_struct` field order; the client reads
  them as LE u32. The `SsDbgSh2Regs` accessor fills the 23-u32 array.
- **SCSP sound RAM (v13)** — the 512 KiB sound block (68000 program + PCM tone bank +
  sequences), surfaced as the **Sound RAM** Memory tab. Mednafen holds it as `uint16`
  host words (like VRAM), so the glue `SwapU16ToBE`s it to big-endian for the wire. The
  patcher injects `SsDbgSoundRam()` into **`sound.cpp`** (where the `static SS_SCSP SCSP`
  instance is in scope) returning `SCSP.GetRAMPtr()` — the 262144-word RAM buffer. It
  lives in `sound.cpp`, not `ss.cpp`, because that instance has internal linkage there.
  **Writes** also work — `SeExportSetSoundWriteHook(SeMdfnWriteSoundByte)` routes pokes
  through the existing bus writer at the sound-RAM base `0x25A00000`, so the Sound RAM
  tab and the music-swap prototype can edit a running game.
- **SCSP decoded voices (v14)** — the 32-slot **Sound (SCSP)** panel (per-voice format,
  pitch, envelope phase/level, pan, loop, live playback position) plus per-voice Play /
  Export. The "who's actually sounding" fields (`EnvLevel`/`EnvPhase`/`CurrentAddr`) exist
  only in the emulator's *decoded* `SS_SCSP::Slot[32]`, which is **private**, so the
  patcher adds a public `SeDbgReadSlots()` member to `scsp.h` and a `SsDbgScspSlots()`
  accessor to `sound.cpp` that calls it. The member serializes each voice into the fixed
  36-byte LE record in `SeLiveProtocol.h`, reading the raw DISDL/DIPAN/EFSDL/EFPAN from
  `SlotRegs[i][0x0B]` and sign-extending the 4-bit `Octave`.
- **CD-block status (v15)** — the **Disc Explorer**'s live drive readout: the FAD the CD
  drive is reading right now, resolved to a file on the loaded disc image. The drive state
  (`CurPosInfo.fad`/`.status`, `CurPlayStart`/`CurPlayEnd`) is file-static in **`cdb.cpp`**,
  so the patcher appends a `SsDbgCdStatus()` accessor there (no header edit needed) that
  serializes the 16-byte LE record in `SeLiveProtocol.h` and maps the Saturn CD status byte
  (`STATUS_*`) to Saturn Explorer's `SE_CD_*` enum. `STATUS_PLAY` — the drive's state while
  streaming sectors — maps to "reading", which is the case the Disc Explorer resolves.

## VDP2 registers (the one protocol wrinkle)

The wire's VDP2 section is the **raw 288-byte Yabause `Vdp2` struct**, which the
client rebuilds into a hardware-offset image via `sedrv::BuildVdp2RegImage`. Mednafen
keeps its VDP2 regs as a flat `RawRegs[0x100]` (indexed by `hw>>1`) — a different
layout. Two ways to bridge:

1. **Synthesize the Yabause struct from `RawRegs`** in the glue (keeps the client +
   protocol unchanged): `struct[offset[i]] = RawRegs[i]` (little-endian), using the same
   fixed 144-entry mapping table the client uses. The glue **embeds** that table
   (`kSeVdp2RegStructOffset`, kept in sync with `Drivers/Common/src/SaturnStateShared.cpp`)
   so it links standalone in a foreign tree — no manual paste. This is what
   `BuildYabauseVdp2Struct` does.
2. **(Cleaner, future) Generalize the wire** so the VDP2 section is a *pre-built
   hardware-offset big-endian image* — exactly what the VDP1-regs section already is.
   A one-time protocol bump: the server sends the finished image, the client uses it
   directly (drops its `BuildVdp2RegImage` call). Then Mednafen just byte-swaps
   `RawRegs` to BE (trivial) and no emulator-specific struct is on the wire. Recommended
   once the Mednafen path works, so no core carries another core's struct layout.

## Suggested phasing

- **Tier 1 — read-only view** (VDP Output, VRAM Map, Command List, Texture/Palette,
  Registers, Framebuffer): only VRAM + registers + CRAM + framebuffer per frame. No
  SH-2, work RAM, breakpoints, or writes. Matches what Mednafen savestates already
  surface — the natural first target.
- **Tier 2 — memory** (Watch + Hex read): add `WorkRAML/H` (+ verify byte order); add
  the memory-poke hook for Hex edits.
- **Tier 3 — CPU/debugger** (SH-2 Assembly + breakpoints): SH-2 register extraction,
  the memory poke, and execution breakpoints are all wired (see below). Breakpoints
  need a `--enable-debugger` build; everything else works in any build.

## Execution breakpoints (Tier 3)

`apply.py` injects `SsDbgAddExecBp` / `SsDbgClearBps` into `ss.cpp` (under
`#ifdef WANT_DEBUGGER`, so a non-debugger build still compiles — they're no-ops
there). `SsDbgAddExecBp` calls the `ss/debug.inc` debugger:
`DBG_AddBreakPoint(BPOINT_PC, addr, addr, true)` plus a one-time
`DBG_SetCPUCallback(hook, false)`. Adding a PC breakpoint makes `DBG_NeedCPUHooks()`
return true, so ss.cpp's per-frame run-loop dispatcher
(`rltab[…][DBG_NeedCPUHooks()]`) switches to the per-instruction `DBG_CPUHandler`
path on its own — **no explicit debug-mode toggle needed**. On a hit the injected
hook reports the halted PC and the CPU that hit it (`DBG.ActiveCPU`) via
`SeExportNotifyStop`, then blocks on `SeExportGateFrame` right at the instruction
until Saturn Explorer resumes — an **instruction-exact halt**, like the Yabause tap.
(Blocking mid-frame freezes Mednafen's frame pump while halted, so audio underruns
for the duration — the same trade as the frame-gate caveat below, expected while
you're stopped at a breakpoint.) The glue registers this through the existing
`SeExportSetBreakpointHooks` bridge, so
Saturn Explorer's gutter breakpoints and "Run to Here" drive it over the protocol
unchanged.

Build Mednafen with `./configure --enable-debugger` for breakpoints to fire; without
it the breakpoint set still round-trips over the wire but doesn't halt. (SS PC
breakpoints are shared across both SH-2s, so a breakpoint fires for whichever core
reaches the address; the hit report still names the exact CPU.)

### Data breakpoints / watchpoints (read/write)

The wire's breakpoint descriptors carry a `kind` (0 exec, 1 read, 2 write, 3
read/write) and a `size` (1/2/4 bytes, the Saturn's 8/16/32-bit accesses). For a
non-exec kind, `apply.py`'s `SsDbgAddMemBp` installs a data watchpoint over
`[addr, addr+size)`: `DBG_AddBreakPoint(BPOINT_READ …)` and/or `BPOINT_WRITE`. The
ss debugger evaluates these ranges every instruction inside `DBG_CPUHandler`
(`CheckRWBreakpoints`); a matching access sets `FoundBPoint`, which drives the same
`SeSsBpHook` with `bpoint=true`, so a watchpoint hit halts (and reports the
accessing PC) exactly like a PC breakpoint. `SsDbgClearBps` flushes all three lists
(`BPOINT_PC`/`READ`/`WRITE`). The glue registers this through
`SeExportSetMemBreakpointHook`. Front end: right-click a byte in the **Memory**
panel → **Add breakpoint** → Read / Write / Read or Write → Byte / Short / Long.
Same `--enable-debugger` requirement as PC breakpoints.

The debugger matches the **raw effective address** the instruction computes, and the
SH-2 reaches the same RAM cell through several cache-region images (bits 31..29:
cached `0x0xxxxxxx`, cache-through `0x2xxxxxxx`, …). Games mix cached and uncached
accesses freely, so `SsDbgAddMemBp` installs the watchpoint in **both** the cached and
cache-through images of the address; a hit through either form halts. `SeMdfnAddMemBp`
also logs a `watchpoint: addr=… size=… RW` line to the SE Log on each breakpoint-set
sync — a useful check that the emulator received it (a stale, non-mem-bp build logs
nothing). Note this is a **brand-new emulator-side accessor**: re-run `apply.py` and
rebuild Mednafen for memory breakpoints to take effect, even if PC breakpoints already
work in your current binary.

### Instruction stepping (Step Into / Over / Out, v12+)

Once halted, the `IST` verb single-steps the halted CPU N instructions then halts again
with `SE_LIVE_STOP_STEP`. Server-side this reuses the same `SeSsBpHook` callback: on an
`IST` request the hook re-arms itself **continuous** (`DBG_SetCPUCallback(hook, true)`)
right after the halt gate releases (`SeExportInsnStepBegin`), counts instructions on the
stepped CPU (`SeExportInsnStepTick`), and halts at the budget's end — so no new call site
is needed. **Step Into** is one `IST`. **Step Over** and **Step Out** are built entirely
client-side: the client installs a transient one-shot breakpoint (kept out of the user's
breakpoint set, auto-retired when hit) at the return site — `PC+4` for a `bsr`/`bsrf`/`jsr`
call, or the current frame's return address for step-out — and resumes at native speed.
Instruction stepping needs the same `--enable-debugger` build as breakpoints and is
initiated from a breakpoint/step halt (the CPU must be in `SeSsBpHook`'s gate).

Every arg to `SeExportSnapshot` may be `NULL` (that section ships as length 0 and the
client no-ops it), so you can build the tiers incrementally and test each.

## Controller input (Tier 4, v7+)

The Saturn Explorer **Controller panel** lets the user press a Saturn pad and drive the
running game. The panel sends an emulator-agnostic `SE_PAD_*` bitmask (SeLiveProtocol.h)
with an `INP` command; `se_export.c` forwards it to the input hook, and the glue's
`SeMdfnSetPad` calls the `SsDbgSetPad` accessor.

`apply.py` wires `SsDbgSetPad` into `smpc.cpp`. Incoming masks are translated to
Mednafen's digital-pad bit order (`bit0 UP … 4 START, 5 A … 12 R`, from
`input/gamepad.cpp`), handed from the server thread through atomics, and overlaid after
the frontend's per-frame host-input refresh. This means Saturn Explorer buttons and
ordinary Mednafen input can coexist. Disconnecting the live client clears both injected
ports so a held button cannot remain stuck. The mask is idempotent and latched, so
re-sending each frame is safe.

**Which pad device.** The injection overlays whichever device the port is set to in
Mednafen's Input config, for the two supported types: the **Digital Control Pad**
(2-byte data) and the **3D Control Pad** (10-byte data — digital buttons share the
gamepad bit layout; L/R are driven as full analog when injected; mode + stick are left
to the host). If a port is set to a device the injection doesn't handle (mouse, mission
stick, gun, …), Saturn Explorer input for that port is ignored. So if presses don't
reach the game, first check the port's device is a Control Pad, and make sure you
**rebuilt Mednafen** (`update.bat`) after any `apply.py` change — the injection lives in
the compiled `smpc.cpp`, not in an already-built binary.

**Verifying what the pad sees.** `apply.py` also injects `SsDbgQueryInput(port, out[3])`
into `smpc.cpp` — it reports, per port, `out[0]` device kind (0 none / 1 gamepad / 2 3D
pad / 3 other), `out[1]` the host-input bits, and `out[2]` the Saturn Explorer-injected
bits. This is the readback hook a client can poll to confirm its injection is actually
reaching the emulated pad (device is a Control Pad, injected bits non-zero) rather than
being dropped by a device-type mismatch.

**Logging the controller configuration on connect.** `apply.py` injects a companion
accessor `SsDbgPortDeviceName(unsigned port)` into `smpc.cpp` that returns a short
human-readable name for the device bound to a port (`"Digital Control Pad"`, `"3D Control
Pad"`, `"Mouse"`, `"Arcade Racer (Wheel)"`, `"Mission Stick"`, `"Twin Mission Stick"`,
`"Virtua Gun / Stunner"`, `"Keyboard"`, `"JP Keyboard"`, `"None (disconnected)"`). The
glue's `SeMdfnPortDeviceName` forwards to it and registers it with
`SeExportSetPortInfoHook`; `se_export.c` then logs `port 1: …` / `port 2: …` to the Log
window once each time a client connects (it rides the existing v11 log channel, so no
wire-protocol change). This makes it obvious at a glance whether the running game is on a
device SE can inject into — only the two Control Pad types are wired for injection.

## Matching Mednafen's keyboard bindings (v10+)

The Controller panel drives the pad from host keyboard keys, and those keys should match
whatever the user already configured in Mednafen (e.g. WASD for the d-pad) — with **no
config-file upload**. Instead of parsing `mednafen.cfg`, Saturn Explorer queries the
emulator's *live* bindings over the protocol.

`apply.py` injects `SsDbgQueryKeyMap(unsigned port, int out[13])` into
`src/drivers/input.cpp` — the SDL frontend layer, where the `PIDC[]` binding cache lives
(the `ss` core only ever sees a resolved pad mask, so this can't live with the other
`SsDbg*` core accessors; like the window-title mark it's appended to a drivers-layer
file, best-effort on a non-SDL build). It walks the port's active `ButtConfig` list,
matching each Saturn button by its device-agnostic IDII token (`"up"`, `"a"`, `"ls"`=L,
`"rs"`=R, …) so it serves the gamepad and 3D Control Pad alike, and returns the USB-HID
scancode of each button's keyboard binding (`ButtonNum & 0x0FFF`), or −1 where none.

The glue's `SeMdfnGetKeyMap` forwards to it; `se_export.c` appends a **v10 keyboard-map
trailing block** to every snapshot (2 ports × 13 int32 scancodes, ascending `SE_PAD_*`
order). The LiveDriver parses it and exposes `se_live_poll_keymap`; the Controller panel
re-adopts the mapping only when it changes, so a manual rebind survives between changes.
Mednafen and SDL/ImGui share USB-HID scancodes, so the value maps straight across.

## Tracepoints (Tier 4, v8+) — TODO(mednafen) confirm

Tracepoints are **non-halting** observation points: when execution reaches an address,
the glue captures the SH-2 register file and queues an event (the Saturn Explorer client
formats the message from those registers), then execution continues. The client installs
the set with a `TRC` command; `se_export.c` forwards it to `SeMdfnSetTracepoints`, and the
per-instruction check lives in `SeMednafenTraceHook(cpu, PC)` (both already in the glue).

The **per-instruction call is wired automatically** through the same SS debugger callback
the execution-breakpoint hook uses. `apply.py` makes the injected `SeSsBpHook` call
`SeMednafenTraceHook((int)DBG.ActiveCPU, PC)` on every invocation (before the breakpoint
halt gate, so a tracepoint on the very PC a breakpoint also stops on still fires as the PC
is reached). To make that callback run *every* instruction — not just when the debugger
finds a PC breakpoint — the glue calls the injected `SsDbgSetTraceActive(1)` from
`SeMdfnSetTracepoints` whenever at least one enabled tracepoint is present; that installs
the callback in **continuous** mode (`DBG_SetCPUCallback(SeSsBpHook, true)`), which also
makes `DBG_NeedCPUHooks()` true so the SS run loop switches to the per-instruction
dispatcher on its own. Emptying the tracepoint set (and having no breakpoints) removes the
callback again, returning the emulator to full speed.

This only works under a `--enable-debugger` (`WANT_DEBUGGER`) build, exactly like
execution breakpoints; without it, `SsDbgSetTraceActive` compiles to a no-op and
tracepoints install and round-trip but never fire. The per-instruction check is a linear
scan over the (few, ≤64) installed tracepoints — the debugger-build cost, same order as
breakpoints. The SE side (editor, format, `TRC`, the events block, client formatting) is
mock-verified in-repo; the live firing needs a real `--enable-debugger` Mednafen.

## Call stack (Tier 4, v9+) — TODO(mednafen) confirm

The **Call Stack** panel shows a dependable (● Confirmed) stack when the emulator records
control flow. The same per-instruction hook (`SeMednafenTraceHook`, above) also maintains
a shadow stack: `SeMdfnTrackFlow` reads the opcode at PC (`SsDbgReadOpcode`, injected by
`apply.py`) and mirrors the SH-2's own calls/returns into se_export —
`SeExportPushFrame` on `bsr`/`bsrf`/`jsr`, `SeExportPopFrame` on `rts`/`rte`. The server
serializes each CPU's stack into the v9 reply block; the client marks these frames
Confirmed and prefers them over its heuristic reconstruction.

Two things are emulator-specific and **not exercised in-repo** (the wire — se_export
serialize + LiveDriver read + client merge — is verified end-to-end with the real code):

- `SsDbgReadOpcode(addr)` reads the 16-bit big-endian instruction via `CheatMemRead`
  (the read pair of the poke's `CheatMemWrite`). Confirm your fork has it, or point it at
  the equivalent debug reader. A fork that already has the executing opcode in the CPU
  dispatch can pass it to the hook and skip the per-instruction read.
- The call/return decode assumes standard SH-2 encodings and returns one instruction past
  the delay slot (PC+4). Interrupt/exception entry is not tracked (only `rte` pops), so an
  interrupt-heavy path can drift; `SeExportPopFrame` guards against underflow and
  `SeExportResetCallStack` is available to re-baseline.

## Window-title mark

`apply.py` also appends `(SaturnExplorer Enabled. <SE ver> / Mednafen <MEDNAFEN_VERSION>)`
to the SDL window title so a tapped build is obvious — injected right after Mednafen's
own `SDL_SetWindowTitle` in `src/drivers/video.cpp`, using the shared
`SeExportTitleSuffix()` helper. It's best-effort: a non-SDL / libretro build won't have
that file, and the patcher skips it gracefully.

## Rewind: resume from a scrubbed-back frame (v16) — opt-in, `-DSE_MDFN_REWIND`

Saturn Explorer's scrub-back can do more than replay recorded pictures: with this wired, you
can **pause, scrub back to an earlier frame, edit Work RAM, and press Play to have the game
jump back to that frame and re-simulate forward with the edit in effect** (later frames are
discarded and re-recorded). That needs the emulator's *full* internal state (CPU pipeline,
bus, timers, DMA, SCU, CD block, SCSP), which only a **savestate** captures — SE's own region
snapshot isn't enough. So the tap adds two accessors that save/load a full state image to/from
a memory buffer:

- `SsDbgSaveState(buf, cap) -> size_t` — write the current savestate into `buf` (probe the size
  with `buf == NULL`); returns 0 on failure.
- `SsDbgLoadState(buf, len) -> int` — restore a savestate from `buf`; returns 0 on success.

`se_export.c` calls `SsDbgSaveState` **once per frame on the emulate thread** and hands the raw
image to a **worker thread** that XOR-diffs it against a keyframe and RLE-compresses it (so only
tiny deltas are kept, not a multi-MB state per frame); the compact blocks stream to the client a
few frames behind. On Play-from-scrub the client reconstructs frame N's full state, ships it back
with the `LST` verb, and `SsDbgLoadState` restores it at the frame gate — so **rewind requires
`--with-pause`** (the gate is where the load is applied).

**Shipped dormant.** The exact Mednafen memory-savestate API — `MDFNSS_SaveSM` / `MDFNSS_LoadSM`
against a `Mednafen::MemoryStream`, and whether those live in the global or `Mednafen::` namespace
— varies by fork, so `apply.py` injects `SsDbgSaveState`/`SsDbgLoadState` as a **compiling stub**
by default (save returns 0 → the ring stays empty → the feature is simply off, and the whole
protocol/client/UI degrade gracefully). To enable it:

1. Confirm the `MDFNSS_*` signatures on your checkout (see `src/state.h` / `src/MemoryStream.h`).
   Adjust the enabled branch in `apply.py`'s `SAVESTATE_ACCESSORS` block if the names/namespace
   differ.
2. Build with **`-DSE_MDFN_REWIND=1`** and **`--with-pause`**. For example, add
   `CPPFLAGS='-DSE_MDFN_REWIND=1'` to `./configure`.
3. Scrub back, edit Work RAM, press Play — the picture should jump back and re-simulate forward
   with your edit, and the scrub bar re-fills from that frame. A load-screen scene change should
   trigger a fresh keyframe; because the diff is off-thread, no frame-rate hitch should appear.

The tuning constants (keyframe threshold, max keyframe interval, raw-queue depth, client state
budget) live in `se_export.c` / `FrameRecorder.*` and can be measured/adjusted on-emulator.

## Saturn-only build (skip the other console cores)

Mednafen builds **every** console core by default (PSX, SNES, MD, NES, PCE, PC-FX, …),
and those cores are the bulk of the compile. This tap only touches `src/ss/`, so you can
`--disable-*` every core except Saturn and lose nothing — the Saturn core (`ss`) and the
shared mednafen core still build. Expect a large cut (PSX + SNES + MD alone dominate).

- **Via `install.bat` / `install.py`:** pass `--mednafen-saturn-only`. The installer adds a
  `--disable-<core>` for each other console to its `./configure` line
  (`MEDNAFEN_OTHER_CORES` in `install.py`):

  ```sh
  install.bat --mednafen-saturn-only          # Windows one-command install
  python Integration/install.py --mednafen-saturn-only
  ```

- **By hand** (keep `--enable-ss`, the default, and `--enable-debugger` for our hooks):

  ```sh
  ./configure --enable-debugger \
    --disable-apple2 --disable-demo   --disable-gb    --disable-gba \
    --disable-lynx   --disable-nes    --disable-ngp   --disable-pce \
    --disable-pce-fast --disable-pcfx --disable-psx   --disable-sms \
    --disable-snes   --disable-snes-faust --disable-md \
    --disable-ssfplay --disable-vb    --disable-wswan
  make -j$(nproc)
  ```

  `--disable-ssfplay` drops the Saturn **Sound-Format player**, a separate module from
  `ss`. The authoritative flag list is `./configure --help` on your checkout (a newer tree
  may have added a core); an extra or renamed `--disable-*` is harmless — autoconf ignores
  an unrecognized one with a warning rather than failing.

**Do you need `make distclean` to switch, or to re-run the installer?** No, not for
correctness. `install.py` re-runs `./configure` on **every** invocation (only the autotools
bootstrap is guarded), and it doesn't use `config.cache`, so a changed flag set is picked
up automatically — the Makefiles regenerate and `make` rebuilds only what changed.
Switching an already-built tree to Saturn-only leaves the other cores' stale `.o` files on
disk; that's wasted **disk**, not compile time, and the compile-time win still applies on
the next `make`. Run `make distclean` (or delete `_emu/mednafen`) only to reclaim that disk
or to force a guaranteed-clean rebuild.

## Build / patch workflow + caveats

- **You need Mednafen's `ss` source and a local build.** Mednafen discourages
  redistributing modified binaries; patching a **local** build for your own
  reverse-engineering is fine under its license, but don't ship a patched "Mednafen".
  (Same personal-patch model as the Yabause tap.) See
  [`../DISTRIBUTION.md`](../DISTRIBUTION.md) for the full fork-and-pin + prebuilt-binary
  guidance (and why the Mednafen-vs-Beetle build split matters).
- Add `se_export.c` to Mednafen's `ss` build, `#include "se_export.h"` at the hook
  sites, and wire the five calls (init / per-frame snapshot / frame gate / deinit /
  bridge registration) — see `se_mednafen_glue.c` for the exact bodies.
- **Frame-gate caveat:** Mednafen's `Emulate()` is driven by its frontend and is
  coupled to audio/timing. Blocking in the gate (`while (!SeExportGateFrame()) {}`)
  the way Yabause does may stall Mednafen's audio. For Tier 1 you can skip the gate
  entirely (live view only, no pause/step); add pause carefully at Tier 2/3.

## Verify without a Mednafen rebuild — done ✓

The glue's snapshot path is fenced under `SE_MEDNAFEN_WIRED` (undefined = stub, so the
file builds anywhere; defined = the real path runs against injected accessors). A
headless mock (`mdfn_live_e2e`) compiles `se_mednafen_glue.c` with that macro, feeds
its accessors, and drives the **real `se_export` server ↔ real LiveDriver** over the
socket. Results:

- **VDP1 VRAM from a real Mednafen savestate round-trips identically to the file
  loader** — the authentic byte-order proof on real data (host-order → glue swap → BE).
- VDP2 VRAM host→BE swap; CRAM + work RAM host→BE normalization (a host word reads
  back as the right big-endian instruction); the `RawRegs`→Yabause-struct→client-image
  rebuild (including the byte-swapped hw-0x78/0x7A pairs); VDP1 regs via the 11-u16
  struct; SH-2 master/slave PCs; and a `WRM` poke reaching the write hook — **all exact.**

So the full data path is proven in-repo, and the patched tap (including the Tier-3
breakpoint accessors, under `--enable-debugger`) compiles + links into a complete real
Mednafen binary. What a live game still needs is a Saturn BIOS + disc to actually run.
