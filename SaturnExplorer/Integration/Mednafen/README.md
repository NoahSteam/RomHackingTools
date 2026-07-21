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

Every arg to `SeExportSnapshot` may be `NULL` (that section ships as length 0 and the
client no-ops it), so you can build the tiers incrementally and test each.

## Controller input (Tier 4, v7+) — TODO(mednafen) confirm

The Saturn Explorer **Controller panel** lets the user press a Saturn pad and drive the
running game. The panel sends an emulator-agnostic `SE_PAD_*` bitmask (SeLiveProtocol.h)
with an `INP` command; `se_export.c` forwards it to the input hook, and the glue's
`SeMdfnSetPad` calls the `SsDbgSetPad` accessor.

`apply.py` injects `SsDbgSetPad` into `ss.cpp` as a **no-op** so the patched build always
links out of the box (this is the one input site that is genuinely emulator-specific and
**has not been exercised in-repo** — the SE side is verified against the mock; only the
accessor body + the SE_PAD_*→SS-bit map need confirming on a real Mednafen). With the
injected no-op the panel highlights and sends but the game ignores the input. To actually
drive the pad, replace the injected stub's body with SMPC wiring:

```cpp
// In ss.cpp (or smpc.cpp — wherever the emulated pad's state is reachable). Latch the
// injected buttons and feed them into the SS controller port every frame, BYPASSING
// Mednafen's own host-input mapping (that's the whole point — a button in the panel is
// that Saturn button regardless of the emulator's key bindings). Map SE_PAD_* to
// Mednafen's SS gamepad bit order (verify against ss/input/gamepad.cpp), and OR/replace
// the pad's digital data for `port`, latched past the frontend's per-frame input refresh.
// Called from SeExportSetInputHook on each INP.
extern "C" void SsDbgSetPad(unsigned int port, unsigned int buttons) { /* SMPC wiring */ }
```

The mask is idempotent and latched, so re-sending each frame is safe.

## Tracepoints (Tier 4, v8+) — TODO(mednafen) confirm

Tracepoints are **non-halting** observation points: when execution reaches an address,
the glue captures the SH-2 register file and queues an event (the Saturn Explorer client
formats the message from those registers), then execution continues. The client installs
the set with a `TRC` command; `se_export.c` forwards it to `SeMdfnSetTracepoints`, and the
per-instruction check lives in `SeMednafenTraceHook(cpu, PC)` (both already in the glue).

The one thing to wire is the **per-instruction call**. Add, in the SS CPU dispatch that
runs under `--enable-debugger` (the same `ss/debug.inc` path the execution-breakpoint hook
uses — `DBG_CPUHook`/`DBG_SetCPUHook`), a call for each executed instruction:

```cpp
SeMednafenTraceHook(cpu, PC);   // cpu: 0 master / 1 slave; PC: the instruction address
```

`apply.py` forward-declares `SeMednafenTraceHook` but does **not** auto-inject this call
(the exact per-instruction site varies and a wrong anchor would break the build), so add
it by hand once. It only fires under a `--enable-debugger` build (the debugger provides
the per-instruction hook), exactly like execution breakpoints; without it, tracepoints
install and round-trip but never fire. This is **not exercised in-repo** — the SE side
(editor, format, `TRC`, the events block, client formatting) is mock-verified; only this
one call site needs confirming on a real Mednafen build. Note the per-instruction check is
a linear scan over the (few) installed tracepoints; that's the debugger-build cost, same
order as breakpoints.

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
