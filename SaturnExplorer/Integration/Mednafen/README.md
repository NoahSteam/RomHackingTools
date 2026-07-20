# Saturn Explorer live tap for Mednafen (Beetle Saturn)

**Status: scaffold, symbols mapped.** This folder holds the plan, the
Yabause→Mednafen symbol map, and a glue template (`se_mednafen_glue.c`). The symbol
names, storage layouts, and hook sites below were read from the Mednafen `ss` source
([libretro-mirrors/mednafen-git `src/ss`](https://github.com/libretro-mirrors/mednafen-git/tree/master/src/ss)).
What remains is a Mednafen build to compile against: the injected **accessors** and
the empirical **work-RAM byte order** (§"Byte order") are the last `TODO(mednafen)`
spots.

Saturn Explorer already **loads Mednafen savestates** (`.ncm`/`MDFNSVST`) statically —
VDP1/VDP2 VRAM, both register files, and CRAM (see
`Drivers/Savestate/src/SavestateDriver.cpp`). This folder is about the **live tap**:
attaching to a *running* Mednafen the way `Integration/Yabause/` attaches to Yabause.

## How little is actually new

The whole live machinery is emulator-agnostic and lives in
[`../Common/`](../Common): the socket server, protocol framing, double-buffering,
pause/step gate, breakpoint sync, and memory-write dispatch are all in `se_export.c`,
which takes **raw pointers + function-pointer hooks — no emulator types**. The
LiveDriver client and the wire protocol are unchanged and need **no edits** to talk
to a Mednafen server (the client already tolerates older protocol versions and
zero-length sections, so a partial Mednafen server degrades gracefully).

So a Mednafen tap = **reuse `../Common/se_export.{c,h}` verbatim** + write the glue
that maps Mednafen's `ss` internals to `SeExportSnapshot(...)` + inject it into a
Mednafen build. That glue is one screen of code; the template is here.

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
| Work RAM low (1M) | `LowWram` | `WorkRAML[0x80000]` u16 (ss.cpp) | **static** | byte order — **verify** (§Byte order) |
| Work RAM high (1M) | `HighWram` | `WorkRAMH[0x80000]` u16 | **static** | byte order — **verify** |
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
| Exec breakpoints | `SH2AddCodeBreakpoint`, `SH2SetBreakpointCallBack` | `ss` debug module (Tier 3 — confirm API) |
| Memory poke | `MappedMemoryWriteByte` | `ss` bus/debug byte write (Tier 2/3 — confirm) |

## Accessors (the Mednafen-specific integration wrinkle)

Yabause exposes its memory as extern globals, so its glue reads them directly.
Mednafen keeps VDP2 `VRAM`/`CRAM`/`RawRegs`, `WorkRAML/H`, and the individual VDP1
registers as **file-scope `static`** — invisible to a separate glue `.c`. Mednafen
already anticipates debug taps (`VDP2::PeekVRAM`, the `RawRegs // For debugging`
array), so the patcher follows that lead: inject a tiny **C-linkage accessor** into
each file *where the static is visible*, and have the glue call it. The glue declares
these (`extern`, fenced under `#if 0` until the build has them):

```c
/* vdp2.cpp (namespace VDP2): */  const uint16_t* SsDbgVdp2Vram(void){ return VRAM; }
                                  const uint16_t* SsDbgCram(void)    { return CRAM; }
                                  const uint16_t* SsDbgRawRegs(void) { return RawRegs; }
/* vdp1.cpp (namespace VDP1): */  const uint16_t* SsDbgVdp1Vram(void){ return VRAM; }
                                  const uint16_t* SsDbgVdp1Fb(void)  { return FB[!FBDrawWhich]; }
                                  void SsDbgVdp1Regs(uint16_t o[11]){ o[0]=TVMR; o[1]=FBCR; o[2]=PTMR;
                                        o[3]=EWDR; o[4]=EWLR; o[5]=EWRR; o[7]=EDSR; o[8]=LOPR; /*6,9,10=0*/ }
/* ss.cpp   (namespace MDFN_IEN_SS): */
                                  const uint16_t* SsDbgWramL(void){ return WorkRAML; }
                                  const uint16_t* SsDbgWramH(void){ return WorkRAMH; }
                                  void SsDbgSh2Regs(int c, uint32_t o[23]){ /* CPU[c].GetRegister(GSREG_R0+i)…GSREG_PC */ }
```

(Give them `extern "C"` when injecting into the C++ `ss` sources so the C glue links
against them.) This is the bulk of what the Mednafen patcher does; the rest is the
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
- **Work RAM** — `WorkRAML/H` are `uint16` arrays accessed through `ne16_rbo_be`
  (see ss.cpp), so their in-array layout is **not obviously the same** as the
  protocol's "host-order 16-bit words, client swaps to BE" assumption. This is the one
  order the savestate path never exercised, so it's **unproven**: pass raw first, and
  if the Hex/Watch panels show byte-swapped values, apply `SwapU16ToBE` in the
  `SsDbgWramL/H` accessors. Nail it with a known value the way the Yabause work-RAM
  order was.
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
   protocol unchanged): `struct[kVdp2RegStructOffset[i]] = RawRegs[i]` (little-endian),
   using the same fixed mapping table the client uses. That table lives in
   `Drivers/Common/src/SaturnStateShared.cpp` (`kVdp2RegStructOffset`, 144 entries) —
   copy it into the glue. This is what the template sketches (`BuildYabauseVdp2Struct`).
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
- **Tier 3 — CPU/debugger** (SH-2 Assembly + breakpoints): add SH-2 register
  extraction + Mednafen's breakpoint API + the stop-event bridge. Hardest tier —
  Mednafen's debug/step model differs from Yabause's.

Every arg to `SeExportSnapshot` may be `NULL` (that section ships as length 0 and the
client no-ops it), so you can build the tiers incrementally and test each.

## Build / patch workflow + caveats

- **You need Mednafen's `ss` source and a local build.** Mednafen discourages
  redistributing modified binaries; patching a **local** build for your own
  reverse-engineering is fine under its license, but don't ship a patched "Mednafen".
  (Same personal-patch model as the Yabause tap.)
- Add `se_export.c` to Mednafen's `ss` build, `#include "se_export.h"` at the hook
  sites, and wire the five calls (init / per-frame snapshot / frame gate / deinit /
  bridge registration) — see `se_mednafen_glue.c` for the exact bodies.
- **Frame-gate caveat:** Mednafen's `Emulate()` is driven by its frontend and is
  coupled to audio/timing. Blocking in the gate (`while (!SeExportGateFrame()) {}`)
  the way Yabause does may stall Mednafen's audio. For Tier 1 you can skip the gate
  entirely (live view only, no pause/step); add pause carefully at Tier 2/3.

## Verify without a Mednafen rebuild

Reuse the Yabause live-path harness pattern: a stub that feeds `SeExportSnapshot`
with bytes parsed from a **real Mednafen savestate**, then connect the real
LiveDriver and assert the panels render (Tier 1) and — as tiers land — that work RAM
+ SH-2 round-trip and a `WRM` poke reaches the write hook. This exercises the glue's
byte-order + VDP2 handling entirely in-repo, before touching a Mednafen build.
