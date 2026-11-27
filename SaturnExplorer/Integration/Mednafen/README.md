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
attaching to a *running* Mednafen. Mednafen is the only supported live emulator; the
Yabause comparisons below are background from the original (now unsupported) Yabause tap.

## Quickstart

```sh
# Point the patcher at a Mednafen checkout (with src/ss/):
python3 /path/to/SaturnExplorer/Integration/Mednafen/apply.py /path/to/mednafen
#   --check       report what it would do, change nothing
#   --with-pause  also inject the pause/step frame gate (see the frame-gate caveat)
#   --revert      remove every edit + copied file
# Then build Mednafen as usual and launch Saturn Explorer with --live.
```

Three build choices decide what the tap can do, and the emulator reports each one to Saturn
Explorer (protocol v24, the `SE_LIVE_CAP_*` bits), which offers only the controls that will work and
lists the rest in its Log on connect:

| Build choice | Without it |
|---|---|
| `apply.py --with-pause` | no pause, frame step, rewind load or emulator-slot load |
| `-DSE_MDFN_REWIND=1` in `CPPFLAGS` | no rewind, Play From Here or save states (the savestate accessors are stubs) |
| the debugger (`./configure` default; off with `--disable-debugger`) | no breakpoints, watchpoints, instruction steps or tracepoints |

`Integration/install.py` sets all three.

Saving to one of Mednafen's own numbered slots (Run → Save Emulator State, protocol v25, `ESV`)
needs none of them: the emulator writes the slot F5 would, by path, at its next frame boundary --
the end of the running frame, or the frame gate while paused -- and keeps running. It is the
counterpart of the emulator-slot load (`ELS`), which does need the gate, and like any load leaves
the emulator running.

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
| CRAM (4K) | `Vdp2ColorRam` | `VDP2::CRAM[2048]` u16 | **static** | bus view via `SsDbgCramWire` — see below |
| VDP2 regs (288) | `Vdp2Regs` struct | `VDP2::RawRegs[0x100]` u16 *"For debugging"* | **static** | layout differs — see "VDP2 registers" |
| VDP1 regs (struct) | `Vdp1Regs` struct | `VDP1::{TVMR,FBCR,PTMR,EWDR,EWLR,EWRR,EDSR,LOPR}` | **mixed static** | accessor fills the 11-u16 `Vdp1` struct; `se_export` builds the hw-offset image. ENDR/COPR/MODR write-only/computed → 0 |
| Work RAM low (1M) | `LowWram` | `WorkRAML[0x80000]` u16 (ss.cpp) | **static** | host u16 (like VRAM) — pass raw, client `Bswap16`s |
| Work RAM high (1M) | `HighWram` | `WorkRAMH[0x80000]` u16 | **static** | host u16 — pass raw (see §Byte order) |
| VDP1 framebuffer (256K) | `VIDSoftGetVdp1FrameBuffer()` | `VDP1::FB[!FBDrawWhich]` (displayed bank of `FB[2][0x20000]`) | file-scope | RGB555 host u16 → **swap to big-endian** |
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

`CheatMemWrite` only reaches ranges in `SH7095_FastMap` (BIOS, work RAM, VDP1/VDP2 VRAM, sound
RAM). **CRAM (`0x05F00000`) and the VDP1 frame buffer (`0x05C80000`) are not in it**, so a poke
there is accepted and dropped. `apply.py` therefore also injects `SsDbgPokeCramByte` (vdp2.cpp:
a byte of the CRAM bus window is merged into its bus word and written through `VDP2::Write16_DB`, so
the renderer's CRAM copy follows) and `SsDbgPokeVdp1FbByte` (vdp1.cpp: the displayed bank, the one
`SsDbgVdp1Fb` reads). The glue's `SeMdfnWriteVdpByte` calls them and is registered with
`SeExportSetVdpWriteHook`, which sets the `SE_LIVE_CAP_VDP_POKE` bit in the control block; the
client offers a CRAM / frame-buffer edit to a running emulator only when that bit is set.
VDP2 VRAM *is* in the fast map, but the VDP2 renderer keeps a second copy of it that only
`VDP2REND_Write16_DB` feeds, so `SsDbgPokeByte` also hands a VDP2 VRAM poke to `VDP2::PokeVRAM`;
without that the snapshot showed the edit while the game kept drawing the old tiles. Likewise a VDP1
VRAM poke is written into the draw-end latch (`SsDbgVdp1LatchPokeByte`), which is what the snapshot
shows: otherwise an edit made at a breakpoint vanished from the view until the next draw-end.
`Integration/tests/check_vdp_poke.py` compiles these against stubs of the Mednafen statics and checks
each poke round-trips through the capture and the client's normalization.

Pokes (`WRM`, `WRS`) are queued by the server thread and applied by the **emulate thread** at the
frame gate (also while a breakpoint holds the CPU), never while the cores are running: the hooks
above are called from `SeApplyPendingPokes` in `se_export.c`. The control block reports how many
were applied and how many could not be (`pokes_applied` / `pokes_dropped`), which the client
turns into an "edit not applied" message. A state load (`LST`/`ELS`) discards pokes still queued
on the client for the state it replaces.

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
- **CRAM** — not the raw array. In RGB888 mode `VDP2::CRAM[]` holds every entry's high
  word in its first half and every low word in its second (`RW()` in vdp2.cpp), which is not
  the layout the CPU sees at `0x05F00000`, where pokes address it. `SsDbgCramWire` exports the
  bus view in the little-endian shape the client's RAMCTL-aware normalization undoes (4-byte
  entries in CRMD 2, 2-byte words otherwise), and `SsDbgPokeCramByte` reads-modifies-writes the
  same bus word, so a poke and the next capture name the same byte in every mode.
- **Work RAM** — `WorkRAML/H` are `uint16` arrays (host order), the **same storage as
  VDP1/VDP2 VRAM**, just accessed via `ne16_rbo_be`. The wire carries work RAM
  host-order and the client `Bswap16`s it to big-endian (LiveDriver), so pass it raw —
  no glue swap. The in-repo mock proves this round-trips (a host word reads back as the
  right big-endian instruction). One residual: `WorkRAMH`'s comment notes it's
  "effectively 32-bit … 16-bit here because of fastmap" — the 16-bit path matches VRAM,
  but if a 32-bit value ever looks half-word-swapped on a real build, spot-check it.
- **VDP1 framebuffer** — `FB[!FBDrawWhich]`, native `uint16` RGB555 host order like VRAM;
  `SwapU16ToBE` it, since `SsDbgPokeVdp1FbByte` addresses it big-endian.
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

  The record deliberately does **not** carry `SSCTL` (the voice's sample source: memory PCM,
  noise, or zero) or `SBCTL` (sample-bit inversion). Play/Export therefore read the voice's
  bytes out of sound RAM as PCM, which is the sample as stored — right for tone and music
  playback, and not what the hardware does for a noise/zero source or an SBCTL-mangled voice.
  Both are rare and effect-only. Adding them needs a protocol version and another two bytes in
  the record, and until then the panel states the limit on the buttons that produce the sound
  rather than implying a bit-accurate export (review finding MEDIA-01).
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
hook reports the halted PC and the CPU that hit it (`SeSsExecCpu`, see "Both SH-2s"
below) via `SeExportNotifyStop`, publishes the snapshot taken at the instruction, then blocks on
`SeExportGateHalt` right there until Saturn Explorer resumes — an **instruction-exact halt**. The
server reports the stop only together with that snapshot, so the first reply that shows the halt
also carries the registers and memory at it (before, about one halt in five was first reported with
the previous moment's registers, and the client evaluates conditional breakpoints on that report).
A publish from inside a halt keeps the current frame number: halts and steps are moments inside a
frame, not frames.
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

### Both SH-2s (`debug.inc`)

Stock `DBG_CPUHandler(which, …)` **returns at its first line when `which != DBG.ActiveCPU`**.
`ActiveCPU` is the debugger UI's "current CPU" — the master unless that UI changed it, which
nothing in this integration does — so with a breakpoint, tracepoint or step armed only the
master ever reached our callback: slave breakpoints and tracepoints never fired, and the shadow
call stack never saw a slave call. `apply.py` (`process_debug`) inserts a scope guard at the top
of `DBG_CPUHandler`'s *definition* (anchored on the signature through its opening brace, not on
the filter line, whose spelling is the fork's) that, while `SeSsBothCpus` is set — exactly while
our callback is installed — makes the executing core the "active" one for the call and records it
in `SeSsExecCpu`, restoring the previous value on every exit path. Everything downstream of the
filter then sees the invariant it was written under (`which == ActiveCPU`), and `SeSsBpHook` takes
the CPU from `SeSsExecCpu`, never from `DBG.ActiveCPU`. User breakpoints stay shared. The stock
filter line is left alone, so `--revert` only has to remove our block.

The patch is verified here against a mock `debug.inc` (it applies, is idempotent, reverts to the
original byte for byte, and the guarded handler runs for both cores) — **not** against the real
file. If your fork spells the definition differently, `apply.py` reports `ANCHOR MISS` for
`SeSsActiveCpuScope<decltype`; add the one line it prints by hand. Without it, slave
breakpoints silently do nothing.

### The device sync in the continuous hook (`SeSsHookNeedsSync`)

Upstream's `DBG_CPUHandler` does `ForceEventUpdates(DBG.eff_ts)` before **every** call of the CPU hook
while the hook is continuous: every device (CDB, SMPC, SCSP, VDP1/VDP2, SCU DMA and DSP, both SH-2 DMACs,
FRT/WDT) is run up to the current timestamp and rescheduled, once per instruction of both SH-2s. A
debugger halting at that instruction needs it. Function attribution does not, and measured on a real
title (Panzer Dragoon II, ~9,900 sampled frames) that sync was 69 % of the emulation thread's time with
attribution on (127 ms of CPU per frame against 13.5 ms with it off). With the guard below the same
scene measured 31.6 ms against 12.2 ms, with no sample in `ForceEventUpdates` under the handler.

`process_debug` therefore replaces that one line (upstream text kept inside the fence, so `--revert` is
byte exact) by `if(DBG.FoundBPoint || SeSsHookNeedsSync || DBG.CPUHook != SeSsOwnCpuHook) ForceEventUpdates(DBG.eff_ts);`
(`SeSsOwnCpuHook` is the callback `SeSyncCpuHook` installed, so a callback the native debugger installs gets
the upstream sync).
`SeSsHookNeedsSync` is defined in the `ss.cpp` block (1 until first set: upstream's behaviour) and
maintained by `SeSyncCpuHook` next to the callback it describes: **set while a tracepoint is armed or an
instruction step is in progress, clear for profiling alone**. A PC or data breakpoint found on this very
instruction (`DBG.FoundBPoint`) always syncs. So the sync still happens wherever the callback may read
device state or halt; it is skipped only for an instruction whose hook does the profile and the shadow
call stack. Because the flag is written in `SeSyncCpuHook`, it obeys the same deferral
(`SeSsInHandler` / `sSeSyncPending`) as the callback itself: the two change together, at the end of the
handler call that postponed them. Why that is safe, and what the profile-only path reads, is in
`Docs/Codebase/Systems/Profiler.md` ("Cost of attribution"). `check_component.py` (f2_sh2) counts the
real `ForceEventUpdates`'s device calls in each mode, and `--mutate sync-guard-removed|sync-never|
sync-no-breakpoint|sync-flag-only|sync-flag-profile` must make it fail.

### The native debugger and our callback (`SeSsNativeSetCPUCallback`)

Mednafen's own debugger (Alt+D) installs and removes its CPU callback through `DebuggerInfo.SetCPUCallback`
(`drivers/debugger.cpp`, `UpdateCoreHooks`: NULL on every breakpoint edit, window change and end of stepping
while it needs none). Upstream points that table entry at `DBG_SetCPUCallback`, so the first time the debugger
was opened it replaced `SeSsBpHook` and a later NULL removed it for good: nothing reinstalled it until an SE
install event ran `SeSyncCpuHook`. Function attribution then froze (records kept arriving with mode 1 and rows,
the whole busy total on one function). `process_debug` now replaces the table entry with
`SeSsNativeSetCPUCallback` (fenced, upstream entry kept under the fence; the extern lives in the
`SeSsActiveCpuScope` helper block, so a tree patched by the previous `apply.py` is upgraded in place):

- a callback the debugger asks for is installed as upstream does, and **it keeps it**: `SeSyncCpuHook` no longer
  writes `DBG.CPUHook` while the debugger holds one (before, any SE install edit during native stepping took the
  callback from it), and `SeSsBothCpus` is off so the handler behaves as upstream for it. Our breakpoints and
  tracepoints do not fire for the other SH-2 meanwhile; that is the price of the debugger taking precedence;
- NULL while it held a callback: our mode is applied again by `SeSyncCpuHook` (deferred to `SeSsHandlerExit` when
  asked from inside `DBG_CPUHandler`, like every edit); NULL while it held nothing changes nothing;
- `SeSyncCpuHook` tells the glue (`SeMednafenForeignHook`) whether we want a callback and it is not ours; the glue
  withdraws ownership and drops the sampling baseline, resets both shadow stacks when ours returns, and publishes
  every frame touched by the gap as not attributed (`Docs/Codebase/Systems/Profiler.md`, "Cost of attribution").
- a callback taken and released inside one `DBG_CPUHandler` call (a one-shot) is still a gap: the takeover is
  recorded when it happens (`sSeNativeLost`) and the next sync that actually runs reports it to the glue before the
  release, so the frame is not attributed and the stack restarts; nothing is marked if we wanted no callback then.

`check_component.py` (f2_sh2) drives the real table entry; `--mutate native-table-upstream|native-null-not-reapplied|
native-not-installed|native-stolen-by-edit|native-both-cpus|native-not-noted|native-in-handler|native-null-always-drops|native-transient-lost|native-transient-undelivered|native-transient-unconditional|native-transient-kept`
must each make it fail. Verified on the pinned source and in the harness; not yet on the real emulator.

### Installs run on the emulate thread

Breakpoint, watchpoint and tracepoint sets arrive on a server thread, but what they install —
the debugger's breakpoint lists, the per-instruction callback and its arming, the glue's tracepoint
table — is read by the emulate thread on every instruction. Rewriting it from the server thread
let the CPU scan half of one tracepoint set and half of the next (a descriptor's id paired with
another's CPU and address). `se_export.c` therefore only **publishes** each set whole into a
mailbox under its state lock; the emulate thread takes it at its next frame gate, frame snapshot or
halt-gate spin (`SeApplyPendingInstalls`) and runs the install hooks itself, so the glue's tables are
single-threaded. The glue calls `SeExportApplyInstalls()` even on frames with nothing attached, so a
client that leaves has its breakpoints dropped promptly; and a halt that nobody is attached to
release is let go by the gate, so a hit that lands before the drop cannot freeze the game.

`SeExportGateHalt` is the halt's own gate: it applies these installs and honours a resume or an
instruction step, but never a state load. A rewind (`LST`) or emulator-slot load (`ELS`) that arrives
while halted releases the halt instead, and the next top-of-frame `SeExportGateFrame` applies it.
Applied inside the halt, mid-instruction, the load resumed a different machine from the one saved
(on the real fork the master SH-2's pipeline and the SCSP timing came out different).

What the debugger holds is released when the **last** client leaves: breakpoints, tracepoints, a
pause or halt, a held pad. The local socket and the TCP port can both be attached, and one
leaving must not disarm the other's breakpoints or resume its halt; a leftover tracepoint would keep
the per-instruction hook on (on a test host, 60 fps fell to 8.5) and stream its events to the next
client.

A resume is ordered after the installs sent before it: the server thread publishes a set, then
clears the pause. The gate checks the mailbox first and the pause second, so a resume landing between
the two used to release the CPU with its temporary breakpoint (Step Over's return address) still
pending. The gate now looks at the mailbox again once it has seen the CPU released (every way out of
it: RUN, STP, IST, no client), before returning to the emulator.

### Shadow call stack timing

The per-instruction hook runs **before** the instruction does, and on the SH-2 a call, `rts` and
`rte` have a delay slot — the transfer happens after the slot runs. Applying them at their own hook
drew the wrong stack for as long as they were in flight: a breakpoint on B's `rts` found B already
popped (so Step Out targeted B's caller's return), and a breakpoint on a `bsr` found its callee
already pushed. The glue now holds a flow instruction pending per CPU and applies it at the first hook
after its delay slot (`SeMdfnFlowAdvance` / `SeMdfnFlowDefer`): a repeat of the instruction or its
slot (the bus-stalled CPU presents the same PC again) changes nothing; `trapa`, with no slot,
completes at the next instruction; a flow whose slot the hook never saw while every instruction is
presented (an interrupt ran first) is applied at the next PC; and a savestate load drops whatever was
in flight. Note the recording only happens while the callback is continuous — a tracepoint armed or an
instruction step in progress — so in a breakpoint-only session there are no recorded frames and Step
Out says so.

When the CPU is released into a run that is **not** watched (Step Over / Step Out / Continue with no
tracepoint armed), the glue is told (`SeMednafenHookMode(0)`, from `SeSyncCpuHook`) and settles what
it holds, because the presentations that would complete it will not come: a held `rts`/`rte` is
applied (it is certain to happen), a held call or `trapa` is dropped (its callee may have run and
returned unseen, and applying it at the next breakpoint would record it as still running). The cost: a
breakpoint inside a callee entered during an unwatched run finds no frame for it. Edits made while
halted (a breakpoint set changed, applied while the halted snapshot is published) do not count as a release, so a Step Into from a halt on a `bsr`
still keeps the call.

### Stepping

`IST` (instruction step) runs the CPU that halted. The per-instruction hook counts **retired**
instructions, not hook calls: the SH-2 can be held off the bus by a long SCU DMA, presenting the
same PC repeatedly without retiring. A repeated PC therefore spends no budget — except for a taken
branch **to itself** (`bt .` / `bf .`, displacement −2), which retires and leaves the PC where it
was; the glue decodes the instruction (`SeMednafenSelfBranchTaken`) and says so. An SCU-DMA
watchpoint halts *between* instructions (`SeExportNotifyDmaStop`): the instruction at the halt PC has
not run, so its first presentation after a step is not a retirement. That halt is reported as its own stop reason, `SE_LIVE_STOP_DMA_WATCH`: no instruction made the
access, so the client does not infer the cause from the instruction (or an execution breakpoint) at the PC. One case remains that a PC and
an opcode cannot settle — a taken self-branch that is *also* stalled on the bus; it would need a
retire counter from the CPU core.

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
(2-byte data) and the **3D Control Pad** (10-byte data — its digital bit layout differs
from the Digital Pad's, so each is translated separately; L/R are driven as full analog
when injected; mode + stick are left to the host). If a port is set to a device the injection doesn't handle (mouse, mission
stick, gun, …), Saturn Explorer input for that port is ignored. So if presses don't
reach the game, first check the port's device is a Control Pad, and make sure you
**rebuilt Mednafen** (`update.bat`) after any `apply.py` change — the injection lives in
the compiled `smpc.cpp`, not in an already-built binary.

**Verifying what the pad sees.** `apply.py` also injects `SsDbgQueryInput(port, out[3])`
into `smpc.cpp` — it reports, per port, `out[0]` device kind (0 none / 1 gamepad / 2 3D
pad / 3 other), `out[1]` the host-input bits, and `out[2]` the Saturn Explorer-injected
bits (the protocol's `SE_PAD_*` mask). This is the readback hook a client can poll to confirm its injection is actually
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
`SeMednafenTraceHook(SeSsExecCpu, PC)` on every invocation (before the breakpoint
halt gate, so a tracepoint on the very PC a breakpoint also stops on still fires as the PC
is reached). To make that callback run *every* instruction — not just when the debugger
finds a PC breakpoint — the glue calls the injected `SsDbgSetTraceActive(1)` from
`SeMdfnSetTracepoints` whenever at least one enabled tracepoint is present; that installs
the callback in **continuous** mode (`DBG_SetCPUCallback(SeSsBpHook, true)`), which also
makes `DBG_NeedCPUHooks()` true so the SS run loop switches to the per-instruction
dispatcher on its own. Emptying the tracepoint set (and having no breakpoints) removes the
callback again, returning the emulator to full speed.

**Repeat policy (v21).** The descriptor's flags word carries *once* / *every N* / *guarded* bits.
The glue applies them where the instruction runs (`SeMdfnTpFires`): an unconditional tracepoint
fires on its first execution (*once*) or every Nth, so a hot tracepoint does not queue events to be
thrown away; a *guarded* one (the client has a condition we cannot evaluate) forwards every
execution. Counts are kept by the CPU thread and carried across a re-sent set for every tracepoint
whose descriptor did not change.

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
`SeExportPushFrame` on `bsr`/`bsrf`/`jsr`, `SeExportPopFrame` on `rts`,
`SeExportPushExceptionFrame` on `trapa` and `SeExportPopExceptionFrame` on `rte`. The
server serializes each CPU's stack into the v9 reply block; the client marks these frames
Confirmed and prefers them over its heuristic reconstruction.

**Returns are paired with the entry that created the frame** — `rts` unwinds a call, `rte`
unwinds an exception — so an interrupt handler's `rte` can no longer delete an application
frame. Before that, a Saturn's VBlank/HBlank/timer/SCSP interrupts drained the recorded
stack to empty within a frame or two. `se_export.h` documents the rule and the SH-2
semantics behind it; `Integration/tests/ShadowCallStackTests.c` covers it in-repo by
driving the tracker over a synthetic instruction stream.

Two things are emulator-specific and **not exercised in-repo** (the wire — se_export
serialize + LiveDriver read + client merge — is verified end-to-end with the real code):

- `SsDbgReadOpcode(addr)` reads the 16-bit big-endian instruction via `CheatMemRead`
  (the read pair of the poke's `CheatMemWrite`). Confirm your fork has it, or point it at
  the equivalent debug reader. A fork that already has the executing opcode in the CPU
  dispatch can pass it to the hook and skip the per-instruction read.
- The call/return decode assumes standard SH-2 encodings and returns one instruction past
  the delay slot (PC+4; `trapa`, which has no delay slot, returns to PC+2). Asynchronous
  interrupt entry is still not *visible* — the stack no longer drifts because of it (see
  above), but a handler's frame is only shown when the entry was a `trapa`.

## Window-title mark

`apply.py` also appends `(SaturnExplorer Enabled. <SE ver> / Mednafen <MEDNAFEN_VERSION>)`
to the SDL window title so a tapped build is obvious — injected right after Mednafen's
own `SDL_SetWindowTitle` in `src/drivers/video.cpp`, using the shared
`SeExportTitleSuffix()` helper. It's best-effort: a non-SDL / libretro build won't have
that file, and the patcher skips it gracefully.

## Rewind: resume from a scrubbed-back frame (v16) — `-DSE_MDFN_REWIND`

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
`--with-pause`** (the gate is where the load is applied). A load sent while a breakpoint holds the
CPU waits for that gate too: the halt is released, the frame finishes, and the load is applied
before the next one starts.

### Where in the frame the state is taken

Not where the frame's pictures are published. `SeMednafenFrameHook` sits inside Mednafen's
`MidSync()`, part-way through the frame's run loop (that is where `FRAME_ANCHOR` first matches), and
that is right for the snapshot. It is wrong for a savestate: a load is applied at the **top** of the
next `Emulate()`, so the state to save is the one at the **end** of the frame, after
`SMPC_EndFrame`, `RebaseTS` and the timestamp adjustments. Saved mid-frame and loaded at the top of
the next, the rest of that frame never runs, and the game resumes subtly off its original course.

So `SeExportSnapshot` only *notes* that a state is wanted, and the second injected call,
`SeMednafenEndFrameHook()` (the last statement of `Emulate()`, placed by `END_FRAME_ANCHOR`), takes
it through `SeExportEndFrame()`. A glue that never calls `SeExportEndFrame` records pictures but no
savestates, silently. Also: a client that attaches mid-run is sent a keyframe first, so what it
records is resumable from the start instead of after the next one (up to 300 frames away).

This was found by running a real game: replay a restored frame and compare the memory of the frames
that follow with the first time round. Mid-frame, only 3 of 76 matched; at the end of the frame,
nearly all do. `se-rewind-live-check` does exactly that against a running emulator (see
`Docs/PlayFromHereVerification.md`).

### What it costs, and how to turn it off

A full savestate per frame is the most expensive thing the tap asks of the emulate thread: several
MB serialized through Mednafen's `SFORMAT` descriptors every frame — and the worker then diffs and
compresses the same bytes on another core. On a slower host that is visible as a lower frame rate,
and it is the first thing to suspect when a patched build runs slower than an unpatched one after
a resync.

Three things bound it:

- **The save buffer is reused.** `SsDbgSaveState` keeps one `MemoryStream` for the life of the
  process and `truncate(0)`s it per frame. That only assigns `data_buffer_size`, leaving
  `data_buffer_alloced` intact, so `grow_if_necessary` stops reallocating after the first save.
  Constructing a fresh stream each frame instead grew it from nothing to the full state size
  through repeated `realloc` and then freed it — a multi-MB allocate-grow-free cycle per frame,
  which is far more expensive on Windows (large blocks go to `VirtualAlloc`/`VirtualFree`) than
  on macOS, and is the likeliest reason the same build felt fine there.

- **Nothing attached, nothing captured.** `SeExportSnapshot` returns immediately while no client
  is connected (`SeExportHasClient`), so the whole per-frame capture — ring copy *and* savestate —
  costs nothing until Saturn Explorer actually attaches. The Mednafen glue checks the same flag
  before it byte-swaps VRAM into its scratch buffers. `SaturnExplorerExportAttachTests` pins this.
- **The user's Settings switch.** Rewind is always built in -- there is no build flag for it --
  and Saturn Explorer's Settings dialog turns it on and off. The client sends the setting to the
  emulator with the `REW` verb (v18) on connect and on every change, so a disabled rewind costs
  nothing per frame rather than being captured and discarded. The server starts each connection
  capturing, so a pre-v18 client keeps the old always-on behavior.

### ⚠ This depends on Mednafen internals, and fails silently

`install.py` defines `SE_MDFN_REWIND=1` (`MEDNAFEN_DEFINES`), so the real `MDFNSS_*` path is what
builds against the pinned fork. `apply.py` keeps a `#else` **compiling stub** beside it, because
the memory-savestate API is not a stable interface and varies by fork: whether `MDFNSS_SaveSM` /
`MDFNSS_LoadSM` / `MemoryStream` sit in the global or `Mednafen::` namespace, what the
`MemoryStream` constructor arguments mean, and what the accessors are called.

**The failure is quiet either way, which is the thing to watch for.** If the real branch stops
compiling you fall back to the stub, `SsDbgSaveState` returns 0, the savestate ring stays empty,
and rewind and save state simply do nothing — no error, no log line. If it compiles but is
*subtly* wrong, you get the same silence. That already happened once: the original enabled branch
built a `MemoryStream(len, 0)`, where the second argument means "the first argument is only a
capacity hint", so the stream's size stayed 0 and `MDFNSS_LoadSM` read an empty stream. Saving
worked, loading always failed, and nothing said so. It needs `MemoryStream(len, -1)` — size set,
zero-fill skipped.

**So after changing `--mednafen-rev`, bumping the fork, or editing `SAVESTATE_ACCESSORS`,
verify the real path is actually live rather than assuming it:**

```
nm _emu/mednafen/src/mednafen | grep SsDbgSaveState        # symbol exists either way
otool -tV _emu/mednafen/src/mednafen | grep -A15 '^_SsDbgSaveState:'   # macOS
objdump -d _emu/mednafen/src/mednafen | grep -A15 '<SsDbgSaveState>:'  # Linux
```

A live build calls `MDFNSS_SaveSM`; the stub returns 0 in a few instructions and calls nothing.
Check `SsDbgLoadState` the same way — it must call `MDFNSS_LoadSM` **and** the two-argument
`MemoryStream(unsigned long long, int)` constructor. Then confirm end to end: scrub back, edit
Work RAM, press Play — the picture should jump back and re-simulate forward with the edit in
effect, and the scrub bar should re-fill from that frame. A scene change should trigger a fresh
keyframe, and because the diff is off-thread there should be no frame-rate hitch.

Rewind also needs **`--with-pause`** (the load is applied at the frame gate).

One build-system trap: `make` tracks source mtimes, not compiler flags, and `update.sh` skips
`./configure` entirely when `config.status` exists — so changing `MEDNAFEN_DEFINES` on an
existing checkout would otherwise have no effect at all, and the feature would look broken rather
than un-rebuilt. `install.py` records the exact configure invocation it used in
`_emu/mednafen/.se-configure` and forces a reconfigure whenever this run's would differ, so a
changed define, `--disable-*` set or Homebrew prefix all re-trigger it with nothing to remember.

The tuning constants (keyframe threshold, max keyframe interval, raw-queue depth, client state
budget) live in `se_export.c` / `FrameRecorder.*` and can be measured/adjusted on-emulator.

## Profiler counters (v27)

`apply.py` (`process_profiler`) inserts counter increments into `sh7095.inc` (external-bus
requests), `sh7095_ops.inc` (SH-2 sleep / standby / bus-stall cycles), `scu.inc` (SCU DMA
transfers and bytes, DSP cycles), `vdp1.cpp` (commands, starts, unfinished draws, command-work
cycles, budget discarded by a DMA), `sound.cpp` and the shared 68000 core `hw_cpu/m68k` (68000
total / idle / executed cycles), `ss.cpp` (cache emulation mode) plus the frame length at the end of
`Emulate()`. They add to one array, `SeSsPerfC`, defined in the glue and indexed by
`SePerfCounters.h` (copied into the tree with the other shared files). The glue hands the array to
the server at the end of every frame (`SeMdfnPerfEndFrame`), sets the `SE_PERF_VALID_*` bit for each
v27 counter the hooks produce, and zeroes it. The increments are unconditional and cheap; every hook
is an insertion after an anchor, so `--revert` removes them cleanly (appended blocks keep their
separating blank line inside the fence, so the file comes back byte for byte). The one hook that
REPLACES a line, the `UpdateInput` call in `smpc.cpp`, keeps the upstream line inside its fence under
`#if 0 /* SE_ORIGINAL */`, and `--revert` puts that text back; a tree patched by an older `apply.py`,
which dropped the line, is recognised, migrated on re-apply and restored on revert. `check_migration.py`
asserts that the whole tree is byte-identical to the pristine one after `--revert`.
An `ANCHOR MISS` here costs one figure in the
Profiler panel (it reads zero) and is reported like any other miss. What each counter means is in
`Docs/Codebase/Systems/Profiler.md`.

**Frame lifecycle (F2 + F6) and function ownership (F9).** Hooks added for them: `sh7095.inc`
`SH7095::SetActive` (the slave's power edges, `SE_SLAVE_ON_EDGE` / `SE_SLAVE_OFF_EDGE`) and the top of
`SH7095::Reset` (`SE_CPU_RESET_HOOK`, after the early return for a switched-off CPU); `sh7095_ops.inc` one
`SeMednafenExceptionEntry` call after each `Exception(EXCEPTION_INT / NMI / CPUADDR / DMAADDR / ILLINSTR /
ILLSLOT, ...)` statement (`SE_OWN_EXC_*`; TRAPA is deliberately not hooked, its flow is observed); `ss.cpp`
`SeMednafenFrameClose(end_ts, ...)` in `Emulate()` after `SMPC_EndFrame` and **before** the rebase
(`SE_FRAME_CLOSE_HOOK`), and `SeMednafenStateLoaded` in the `StateAction` load path beside the timeline
notification (neither `PostStateLoad` nor a load calls `SetActive`). The slave on-time (counter 5) is
integrated over those edges by the glue; the old all-or-nothing line in the end-of-frame fence is gone.

**Re-applying over a tree an older `apply.py` patched.** `upsert` leaves a fence whose key is
already present alone, so a hook this version drops or renames has to be deleted explicitly:
`RETIRED_FENCES` in `apply.py` lists them and `process_retired` removes them before anything else
is applied. Currently the v26 elapsed-VDP1 hook (`SE_PERF_VDP1_DRAW_CYCLES`, which no longer
compiles: index 6 is retired and is never aliased), the v26 per-slice 68000 hooks (`se_perf_t0`) and the
all-or-nothing slave on-time line of the end-of-frame fence (`SSH2_ON_CYCLES += end_ts`; the same fence is
re-inserted without it).
Any future rename needs a new entry there. Checks:

* `Integration/tests/check_apply_hooks.py` (ctest `SaturnExplorerMednafenProfilerHooks`, no
  Mednafen source needed): the retirement on synthetic fences, exact revert (also of the replaced `smpc.cpp` line, and of the block an older `apply.py` wrote), the F4 debit macro
  compiled around a copy of Mednafen's, and the F8 address table compiled and checked at both ends of
  every range.
* `Integration/tests/check_apply_newlines.py` (ctest `SaturnExplorerMednafenApplyNewlines`, no checkout): apply and
  `--revert` on a synthetic LF tree and its CRLF copy are byte-exact, an LF tree gains no CR, a CRLF tree stays
  pure CRLF; Windows' text-mode newline translation is simulated by wrapping `open`. `apply.py` reads and
  writes patched files as bytes (`read_src` / `write_src`), folding a pure-CRLF file to LF only in memory.
  A file mixing LF and CRLF is patched as-is (its anchors may then miss).
* `Integration/tests/check_scratch_includes.py` (ctest `SaturnExplorerMednafenScratchIncludes`, no checkout): the
  scratch tree of `check_migration.py` / `check_component.py` resolves `<mednafen/...>` to the PATCHED `work/src`
  both with symlinks and with `os.symlink` forced to fail (then `include/mednafen` is a mirror that
  `include_flags()` re-syncs before every compile). Checks compile with `check_migration.include_flags(work)`.
* `Integration/Mednafen/check_migration.py <checkout> <scratch> <old apply.py>...` (needs the
  checkout; `git show 4b6cd9e:SaturnExplorer/Integration/Mednafen/apply.py > old.py`): patches
  pristine copies with each old `apply.py`, then with this one, and asserts the retired text is gone,
  every hook is present exactly once, a third apply changes nothing, `vdp1.cpp` / `sound.cpp` /
  `m68k.cpp` / `ss.cpp` still parse, and `--revert` restores the WHOLE tree byte for byte (a snapshot of
  every file, so an unlisted file is caught too), with and without `--with-pause`; applying again
  gives the migrated tree.
* `Integration/Mednafen/check_component.py <checkout> <scratch>` (needs the checkout): builds the
  *real* patched `vdp1.cpp` (F4: `VDP1::Update` / `DoDrawing` over a tiny VRAM command list), the
  patched 68000 core and `sound.cpp` (F5: real STOP / interrupt wake / external halt, and a real
  faulting sound-bus read whose `MDFN_longjmp` lands in `SOUND_Update`) into small harnesses with
  only the surrounding devices stubbed, and checks the SCU / CS0 decode windows and the DMA / DSP hook
  placement in `scu.inc`. F2 / F9: the **real** `SH7095::SetActive` / `SH7095::Reset` (whole patched `ss.cpp`
  included, hidden-visibility data stubbed) calling the real glue, plus where the frame-close, state-load
  and exception hooks sit. `--drop KEY` removes one hook and must make it fail (`SE_SLAVE_ON_EDGE`,
  `SE_SLAVE_OFF_EDGE`, `SE_CPU_RESET_HOOK` among them). F8: `component/perf_bus_harness.cpp` drives the
  real patched SH-2 core (cache fills in both instruction-cache modes, CCR.CE / ID / OD, purge and refill,
  uncached mirror, the on-chip DMAC for each unit size, burst and cycle-steal, cross-device and the
  dummy / unmapped buckets) and asserts the exact request counts of `component/BUS_EXPECTED.md`;
  `--only f8_bus` runs just it (also registered as ctest `SaturnExplorerMednafenBusComponent`, skipped when
  there is no checkout: exit status 77) and `--mutate bus-burst|bus-dma-read|bus-dma-write` must make it fail.
  The `debug.inc` device-sync guard is checked in the f2_sh2 harness (`--mutate sync-guard-removed|sync-never|
  sync-no-breakpoint|sync-flag-only|sync-flag-profile`).

Function attribution is separate and opt-in: the client's `PRF` verb makes `SeSyncCpuHook` keep
the continuous per-instruction callback installed (`SsDbgSetProfileActive`), and
`SeMdfnProfSampleAt` charges each instruction's cycles (`SsDbgCpuTimestamp` deltas, less the
sleep/stall cycles counted meanwhile) to the innermost shadow-call-stack frame while its ownership is
known, else to a raw-PC bucket (F9). The mode only changes between frames (the server applies the request
at the top-of-frame gate or the end-of-frame hook, never at a halt or snapshot) and is published with
the record. It therefore needs
`--enable-debugger`, runs the debug run loop (several times slower than the fast one; the device sync
upstream's handler does per instruction is skipped for profiling alone, see "The device sync in the
continuous hook"), and is switched off by the server when the client disconnects.

**Not yet verified against a running game**: the patched 1.32.1 tree compiles and links with
these hooks, and the wire path is tested end to end with the real server and driver, but the
counter magnitudes have not been compared with a known workload. See
`Docs/Codebase/Systems/Profiler.md`.

## Hardware event log

Design: `Docs/DebuggerFeaturesPlan.md` Part 1. The emulator reports what the hardware does (exceptions,
interrupts, DMA, VDP1 draws, VBlank, CD and SMPC commands) as 32-byte records (`Common/SeHwEvents.h`) into the
exporter's ring; the front end shows them in the Log. Everything is opt-in per connection (the `HWE` verb), so a
client that never asks costs the emulator one load and one branch per hook site.

**Zero cost when off.** Every hook is a new insert-only fence of the form
`if(MDFN_UNLIKELY(SeSsHwEvMask & SE_HWEV_EN_x)) SeMednafenHwEvent(...)`. `SeSsHwEvMask` is one global in the glue,
written only on the emulate thread (`SeMednafenSetHwEvents`, called by the exporter at a frame boundary or at the
paused-frame gate). No site is in a per-instruction or per-bus-access path: they are on exception entry, DMA
start/end, device commands and frame-level VDP code. Two run often, so their mask test comes first:
`SCU_SetHBVB` (once per H phase) and `CheckDoMasterInt` (per interrupt check). Nothing needs the debugger loop, so
the log works in a build without `--enable-debugger`, at full speed.

**Per-frame cap.** The glue lets through at most `SE_HWEV_FRAME_CAP` (64) records per source and per CPU per frame
(master, slave and no-CPU events each have their own budget), counts the rest, and at the end of the frame (in `SeMednafenEndFrameHook`, after the profiler's frame and before
`SeExportApplyInstalls`) queues one `SE_HWEV_K_SUPPRESSED` record per overflowing (source, CPU): the record's `cpu` says whose events were
dropped (`SE_HWEV_CPU_NONE` for a device source), arg0 = how many were dropped, arg1 = the cap. A state load clears the counters.

| Source (bit) | File | Site | Event |
|---|---|---|---|
| CPU_EXC (0x01) | `sh7095_ops.inc` | after each `Exception(EXCEPTION_x, ...)`, behind the profiler's ownership fence (INT, NMI, CPUADDR, DMAADDR, ILLINSTR, ILLSLOT) and after the TRAPA one | `SH2_EXCEPTION`: saved PC (exact), vector, class, level (INT), handler PC. Same text serves `Step<>` and the slave's resumable copy |
| SCU_INT (0x02) | `scu.inc` | `CheckDoMasterInt` after `IPending &= ~(1U << bpos)`; the IMS write (`case 0xA0`) | `SCU_INT_ACCEPT` (delivered, not raised: `SetInt` fires per HBlank line); `SCU_INT_MASK` (new, old) |
| SCU_DMA (0x04) | `scu.inc` | `StartDMATransfer` after `d->ReadFunc = rftab[rb]` (once per indirect table entry); `SCU_DoDMAEnd`; the two illegal-address refusals | `SCU_DMA_START` (level, indirect, start factor, buses; src, dst, bytes), `SCU_DMA_END`, `SCU_DMA_ILLEGAL` (level \| side<<8 with side 0 = source refused, 1 = destination refused; src, dst) |
| SH2_DMAC (0x08) | `sh7095.inc` | top of `DMA_RecalcRunning` | `DMAC_START` / `DMAC_END`: the glue keeps a per-CPU shadow of the run levels and reports the edges. The shadow is unknown after a state load, a CPU reset, or when the source is switched on; the first recalc then only seeds it. The start PC (read through `SsDbgSh2Regs`, which applies the RPC-4 / PC_ID rule) is approximate and flagged `SE_HWEV_F_PC_APPROX`; the end carries none |
| VDP1 (0x10) | `vdp1.cpp` | `StartDrawing`; "Drawing finished"; "aborted by framebuffer swap"; "Displayed framebuffer changed"; "VB erase start" | `VDP1_DRAW_START` (draw FB, previous draw interrupted), `VDP1_DRAW_END` (last command VRAM byte address, reason 0 END bit / 1 aborted by a swap), `VDP1_FB_SWAP`, `VDP1_FB_ERASE` |
| VDP2 (0x20) | `scu.inc`, `vdp2.cpp` | `SCU_SetHBVB` on a VBlank edge; the TVMD write, only when the display bit changes | `VDP2_VBLANK`, `VDP2_DISPLAY` |
| CD (0x40) | `cdb.cpp` | after `CTR.Command = ...` (inside the command coroutine); the end of `BasicResults` and of `CDStatusResults` | `CD_COMMAND`, `CD_RESPONSE`. The periodic status and the disc-authentication result write `Results[]` directly and are not logged |
| SMPC (0x80) | `smpc.cpp` | after `OREG[0x1F] = ExecutingCommand` (coroutine) | `SMPC_COMMAND`. INTBACK continuations are not logged |

The supported mask the glue registers (`SE_MDFN_HWEV_SUPPORTED`) is `0xFF`: every source has at least one hook.
Hooks inside the coroutine-style bodies (`cdb.cpp`, `smpc.cpp`, the VDP1 command loop) are single statements with
no declaration, because a jump to a `case __COUNTER__:` label past an initialised local is ill-formed.

Timestamps (`ts`) are the master SH-2 clock; a device event can be off by the scheduler's slack, so nothing
should depend on an exact value. Device events carry no CPU and no PC.

Checking it: `Integration/tests/HwEventGlueTests.c` (the glue: mask, mapping, cap, DMAC shadow, resets),
`Integration/tests/check_apply_hooks.py` (every key once, behind a mask test), and with a checkout
`check_component.py` (`component/hwev_harness.cpp` runs the real patched `vdp1.cpp` and the real SH-2 core, SCU
and DMAC against the real glue; `--drop <key>` for every `SE_HWEV_*` key and `--mutate hwev-mask-ignored` /
`hwev-wrong-kind` must each fail it) and `check_migration.py` (upgrade from earlier `apply.py` versions). The CD
block, SMPC and VDP2-display hooks are covered by the placement/compile checks only; they need a whole machine to run.

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

- **You need Mednafen's `ss` source and a build.** Which source is recorded in
  [`UPSTREAM.json`](UPSTREAM.json): the upstream release (1.32.1), its tarball hash, and the
  commit in our fork `NoahSteam/mednafen-git` that is byte-identical to it. `install.py` pins its
  clone to that commit, `apply.py` warns if the checkout is a different version, and a release's
  `SOURCE.txt` is generated from it. **Bumping the base means editing that file in the same commit
  as the fork bump and the re-anchoring.** Releases ship a prebuilt, clearly branded patched binary
  with the source; see [`../DISTRIBUTION.md`](../DISTRIBUTION.md) ("Prebuilt binaries" and "Cutting a
  release") for the terms and the fork-and-pin guidance (and why the Mednafen-vs-Beetle build split matters).
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
