# Saturn Explorer — Architecture

> Status: **M0–M7 done.** Both seams exist as headers under `include/saturnexplorer/`; the core
> static lib implements the C++/C-ABI boundary and software-renders the frame from VDP1 sprite
> quads with VDP2 NBG backgrounds composited under them; the savestate driver reads the Yabause
> family and Mednafen; the live driver talks to a patched emulator over a socket or named pipe
> (pause/step, breakpoints, watchpoints, tracepoints, input injection, memory writes, SCSP and CD
> state, savestate rewind); and the frontend runs on Windows, macOS and the web behind the Seam C
> abstraction. The disc, search and ROM-hacking work planned as M6 landed in the frontend rather
> than the core — see §6 and §10. All of it is verified against real Yabause dumps and `.yss`
> savestates; `Docs/FunctionalityVerification/` records what was executed and what still needs a
> real emulator or game, and `Docs/CodeReview/` the open defects.
>
> Supported platforms are **Windows and macOS**. Linux and the web build are development targets,
> not release gates.
>
> This document is the source of truth for the component split, the three interface seams
> (A data, B host, C platform), and the module breakdown. Where it describes a plan rather than
> the code, it says so.

---

## 1. Vision

Saturn Explorer is a visual reverse-engineering and debugging tool for Sega Saturn
games. It presents the console's graphics hardware (VDP1 sprites, VDP2 backgrounds,
VRAM, CLUTs, the VDP1 command table) as an interactive interface, and traces the
graphics it shows back to their origin on the game disc.

The product is deliberately split — following the **libretro / RetroArch** model —
so the analysis engine is reusable and platform-agnostic, while the parts that touch
a specific emulator, console, or capture format are swappable.

---

## 2. The three components

```
                        ┌──────────────────────────────────────────┐
   Seam A (data)        │              SaturnExplorer              │        Seam B (host)
   driver → core        │  (C++ internally, C ABI at both seams)   │        core → host
        ┌───────────────┤                                          ├───────────────┐
        │               │  VDP1/VDP2 parsers · texture/palette     │               │
        │               │  decode · VRAM map · command table ·     │               │
        ▼               │  ROM/archive search · asset trace ·      │               ▼
┌───────────────┐       │  memory history · reference explorer     │       ┌───────────────┐
│    DRIVER     │──────▶│                                          │◀──────│   FRONTEND    │
│ (data source) │       └──────────────────────────────────────────┘       │    (host)     │
│               │                                                           │               │
│ emulator hook │   provides live Saturn state + disc access                │ Dear ImGui +  │
│ savestate/dump│                                                           │ D3D11 UI      │
│ live capture  │                                                           │ (reference)   │
└───────────────┘                                                           └───────────────┘
```

### 2.1 `SaturnExplorer` — the engine (library)

All platform-agnostic logic. Has **no** dependency on a UI framework, no windowing,
and no Win32 types in its public headers. Builds as a **static lib** (for the bundled
frontend) and optionally as a **DLL** (for third parties who load it at runtime).

Owns: VDP1/VDP2 parsing, **software VDP1 rasterization**, **dual 2D-screen / 3D-world
geometry generation** (see §7), texture/palette decoding, the VRAM map, the command-table
model, ROM/archive search, asset trace, memory history, and the reference explorer.

### 2.2 Drivers — the platform adapters (Seam A implementers)

A driver satisfies the core's **Data Source** interface: it gives the core read access
to live Saturn state and to the game disc. The core never knows whether that data comes
from an emulator, a savestate/memory dump, or a live devkit capture.

Third parties write drivers. Planned reference drivers:
- **Savestate/dump driver** (`Drivers/Savestate`) — reads a static snapshot: a Yabause-family
  `.yss`, a Mednafen/Beetle `MDFNSVST`, a directory of region files, or a raw linear dump.
- **Live driver** (`Drivers/Live`) — talks to a running emulator patched by `Integration/`,
  over a Unix socket, a Windows named pipe, or TCP for the web build. Adds live registers,
  framebuffer readback, execution control, breakpoints and watchpoints, write-back, and
  savestate rewind.

### 2.3 Frontend — the host (Seam B consumer)

Consumes the core's **Host API** to render the UI. The reference frontend is a standalone Dear
ImGui app with one backend per platform behind Seam C: **Win32 + D3D11** on Windows, **SDL2 +
OpenGL** on macOS (and Linux, as a development target), and **SDL2 + WebGL** for the Emscripten
build. It is *also* where the drivers ship from — in practice a "platform driver app" ships a
driver and a frontend together — and where the debugger, disc, search and patch systems live
(§6, §10).

Because the boundary is a C ABI, someone else's application (e.g. an emulator's own debug
UI) can embed the core directly instead of using our frontend.

---

## 3. Why a hybrid C++/C-ABI boundary

- **Inside** the core and the frontend: modern C++ — value types, `std::vector`,
  RAII, templates. Fast to develop, expressive.
- **At each seam** (Seam A and Seam B): a flat **C ABI** — versioned structs of
  function pointers, POD data types, opaque handles. No C++ types, no STL, no
  exceptions cross the boundary.

This buys ABI stability and language-agnostic embedding (a driver or host can be built
with a different compiler/STL, or not be C++ at all) without paying the ergonomic cost
of C internally. The price is a thin translation layer at each seam, which we own.

**Rules for anything crossing a seam:**
1. Only C types: fixed-width integers, `char*`, POD structs, opaque `void*`/handle typedefs.
2. Every interface struct starts with a `uint32_t abi_version` and a `uint32_t` capability
   bitmask. Consumers check version before use; the core degrades on missing capabilities.
3. Ownership is explicit: whoever allocates frees. Buffers are caller-allocated with a
   size in / size-needed out convention (call once for size, once to fill).
4. No exceptions cross the boundary. Errors are returned as `se_result` enum codes.
5. Strings are UTF-8, `NUL`-terminated, caller-owned unless documented otherwise.

---

## 4. Seam A — the Data Source interface (driver → core)

The driver hands the core a `se_data_source` struct. The core calls these to read state.
Not every driver can provide everything (a static dump has no event stream), so the driver
advertises `capabilities` and the core adapts.

```c
/* Capability bits — driver advertises what it can do. */
enum {
    SE_CAP_VDP1_VRAM     = 1 << 0,
    SE_CAP_VDP2_VRAM     = 1 << 1,
    SE_CAP_CRAM          = 1 << 2,   /* color RAM / palette */
    SE_CAP_MAIN_RAM      = 1 << 3,
    SE_CAP_VDP1_REGS     = 1 << 4,
    SE_CAP_VDP2_REGS     = 1 << 5,
    SE_CAP_FRAMEBUFFER   = 1 << 6,   /* optional reference frame, for diffing vs core render */
    SE_CAP_EVENT_STREAM  = 1 << 7,   /* memory-write / DMA events (Memory History) */
    SE_CAP_DISC          = 1 << 8,   /* raw ISO / file access (ROM Search) */
    SE_CAP_FRAME_STEP    = 1 << 9,   /* pause / step / advance (Frame Timeline) */
};

typedef struct se_data_source {
    uint32_t abi_version;
    uint32_t capabilities;
    void*    user;                     /* driver's own context, passed back to every call */

    /* --- Bulk memory reads: return bytes copied. --- */
    size_t (*read_vdp1_vram)(void* user, uint32_t offset, void* dst, size_t size);
    size_t (*read_vdp2_vram)(void* user, uint32_t offset, void* dst, size_t size);
    size_t (*read_cram)     (void* user, uint32_t offset, void* dst, size_t size);
    size_t (*read_main_ram) (void* user, uint32_t offset, void* dst, size_t size);

    /* --- Registers. --- */
    uint16_t (*read_vdp1_reg)(void* user, uint32_t reg);
    uint16_t (*read_vdp2_reg)(void* user, uint32_t reg);

    /* --- Optional: reference framebuffer (SE_CAP_FRAMEBUFFER) for diffing vs the
           core's own software render. NOT the primary display path — the core composites
           the frame itself from VRAM (see §7). --- */
    int (*get_framebuffer)(void* user, se_framebuffer* out);

    /* --- Optional: memory-access event pump (SE_CAP_EVENT_STREAM). --- */
    size_t (*poll_events)(void* user, se_mem_event* out, size_t max);

    /* --- Optional: disc / archive access (SE_CAP_DISC). --- */
    int    (*disc_stat)(void* user, se_disc_info* out);
    size_t (*disc_read)(void* user, uint64_t byte_offset, void* dst, size_t size);

    /* --- Optional: frame control (SE_CAP_FRAME_STEP). --- */
    int (*frame_pause) (void* user);
    int (*frame_step)  (void* user, int32_t frames);
    uint64_t (*frame_number)(void* user);
} se_data_source;
```

`se_mem_event` is the record that powers **Memory History** and **Asset Trace**:

```c
typedef enum {
    SE_MEM_CPU_WRITE, SE_MEM_DMA, SE_MEM_COPY, SE_MEM_FILE_LOAD
} se_mem_event_kind;

typedef struct se_mem_event {
    se_mem_event_kind kind;
    uint64_t frame;
    uint32_t src_addr;      /* 0 if N/A (e.g. CPU write) */
    uint32_t dst_addr;
    uint32_t size;
    uint32_t channel;       /* DMA channel, or driver-defined tag */
} se_mem_event;
```

---

## 5. Seam B — the Host / Embed API (core → host)

The host (our frontend, or a third-party app) creates a core context around a driver,
snapshots a frame, then issues queries. This is the surface the frontend's ImGui panels
are built on.

```c
typedef struct se_context se_context;   /* opaque */

se_context* se_create (const se_data_source* ds, const se_config* cfg);
void        se_destroy(se_context*);

/* Snapshot the current Saturn state for this frame; all queries below read the snapshot. */
se_result   se_begin_frame(se_context*);

/* --- Command Table Explorer / Interactive Sprite Inspection --- */
size_t      se_command_count(se_context*);
se_result   se_get_command(se_context*, size_t index, se_command* out);
se_result   se_hit_test(se_context*, int x, int y, size_t* out_index);  /* click → command */

/* --- VDP1 geometry: every sprite emitted in TWO coordinate spaces (see §7). --- */
size_t      se_sprite_count(se_context*);
se_result   se_get_sprite_2d(se_context*, size_t index, se_sprite_2d* out);  /* screen-space quad */
se_result   se_get_sprite_3d(se_context*, size_t index, se_sprite_3d* out);  /* world-space quad  */

/* --- Software VDP composite: the finished frame, rasterized by the core from the
       sprite quads + VDP2 layers. `opts` carries all layer toggles and overlays
       (hide VDP1 / hide NBGx / windows / shadows / wireframe / bounds / priority /
       object-number). The driver supplies a camera only for the 3D view; the 2D frame
       is rendered here. --- */
se_result   se_render_frame(se_context*, const se_render_opts* opts, se_image* out);

/* --- Texture & Palette Viewer --- */
se_result   se_decode_texture(se_context*, const se_texture_ref* ref, se_image* out);
se_result   se_decode_palette(se_context*, uint32_t clut, se_palette* out);

/* --- VRAM Visualization --- */
size_t      se_vram_region_count(se_context*);
se_result   se_get_vram_region(se_context*, size_t index, se_vram_region* out);

/* --- ROM & Archive Search / Asset Trace --- */
se_search   se_rom_search_begin(se_context*, const se_search_query*);
size_t      se_rom_search_poll (se_context*, se_search, se_search_result* out, size_t max);
void        se_rom_search_end  (se_context*, se_search);

/* --- Reference Explorer --- */
size_t      se_references_of_texture(se_context*, const se_texture_ref*, se_reference* out, size_t max);
size_t      se_references_of_palette(se_context*, uint32_t clut, se_reference* out, size_t max);

/* --- Memory History --- */
size_t      se_history_for(se_context*, const se_texture_ref*, se_mem_event* out, size_t max);
```

Division of rendering labor: the **core** does *software* rasterization — it composites
the finished 2D frame into an `se_image` (raw RGBA + metadata) and emits the per-sprite
2D/3D geometry. The **host** does *GPU* work — it uploads `se_image` as a texture for the
2D view, and draws the 3D geometry through its own camera and D3D pipeline. No ImGui or
D3D types ever cross the seam; the core never makes a graphics API call.

---

## 6. Core module breakdown

Internally (C++, not exposed across the seam):

| Module | Responsibility | Feature(s) served |
|---|---|---|
| `HardwareSnapshot` | Pulls VRAM/CRAM/regs via Seam A into a per-frame immutable snapshot | everything |
| `Vdp1Parser` | Walks the VDP1 command table into a typed command list | Command Table Explorer, Sprite Inspection |
| `Vdp2Parser` | Decodes VDP2 layer config (NBG0–3, RBG0), windows, priorities | Layer Visualization |
| `TextureDecoder` | VRAM + CLUT → RGBA, all sprite color modes, RGB555 | Texture & Palette Viewer |
| `PaletteDecoder` | CRAM/CLUT → color swatches | Palette Viewer |
| `VramMap` | Classifies VRAM into texture pages / CLUTs / command table / free | VRAM Visualization |
| `GeometryBuilder` | Turns each VDP1 command into a 4-corner quad in **2D screen space** and **3D world space** (see §7) | Live Visualization, Sprite selection, 3D view |
| `Vdp1Rasterizer` | Software rasterizes the 2D quads (texture, gouraud, transparency, mesh, flip, draw modes) into the finished frame | Live Visualization |
| `Vdp2Compositor` | Composites VDP2 layers with the VDP1 output by priority; honors layer/window/shadow toggles | Live Visualization, Layer toggles |
| `ReferenceIndex` | Reverse index: texture/palette → commands that use it | Reference Explorer |

`Vdp2Parser`, `TextureDecoder`, `PaletteDecoder` and `VramMap` are responsibilities rather than
files: they live in `Vdp2Compositor`, `Vdp1Color.h` and `Context`. The table is the division of
labour, not a file list -- §10 has that.

Three modules in earlier drafts of this table were never built in the core, and the features they
named are frontend systems instead (review finding ABI-03, which removed their Seam B
declarations):

| Planned | Where it actually lives |
|---|---|
| `SearchEngine`, `AssetTracer` | `FrontEnd/src/DataSearch.*` + `Disc/` — an interactive, cancellable search over the disc, holding state across frames |
| `MemoryHistory` | `FrontEnd/src/Debug/AccessLog.*`, driven by watchpoint hits from the live tap |
| `FrameTimeline` | `FrontEnd/src/FrameRecorder.*` + the live tap's savestate rewind |

`se_mem_event` remains in Seam A: the driver still pushes memory events through `poll_events`.

---

## 7. Rendering & geometry model (settled)

The core is a **software VDP1**. It reconstructs the frame from individual sprites exactly
as the Saturn's VDP1 does, rather than reading back an already-composited framebuffer from
the driver. VDP1 draws *distorted sprites*: every command is a quad with four independently
positioned corners, which is how the hardware does skew, rotation, scaling, and translation
in one primitive. The core takes that **4-corner textured quad** as its native primitive
(normal/scaled sprites are the axis-aligned special case; polygons, polylines, and lines are
the other command types).

For every VDP1 command the core emits the sprite in **two coordinate spaces**:

1. **2D screen space** — the quad exactly where the Saturn places it on screen (4 corners +
   texture UVs, color mode, gouraud, transparency, flip, draw mode, priority). Rasterizing
   these in priority order reproduces the finished frame. Because each sprite is a discrete
   quad, the host hit-tests a click against the quad list to select the underlying command —
   this is what makes **every on-screen object clickable**. This set is authoritative for
   "what the Saturn drew" and for selection.

2. **3D world space** — the *same* sprites emitted as world-space geometry (4 corners with
   X/Y/Z), **right-handed: X right, Y up, +Z towards the viewer**. `SeTypes.h` is the
   source of truth for that convention and for the camera's yaw/pitch senses; a host that
   picks the mirror of either renders plausibly but hit-tests the wrong sprite. Overlapping
   2D sprites are separated along **Z by priority, then stable draw order** as tiebreak, so
   the scene "explodes" into layers. Seen head-on the 3D view must reproduce the 2D
   composite — `Core/tests/Geometry3DTests.cpp` pins that (and the orbit sense) in CI, and
   `se-render --3d --reference` checks it against a real dump. The **core owns the geometry;
   the driver owns the camera** — it supplies its own view/projection and can orbit, pan, and
   fly through the geometry to inspect how the frame is assembled.

So the core's job is *geometry + software rasterization*; the driver's job for the 3D view is
*purely a camera*. The two sets are the same sprites — a selection in one highlights it in the
other.

**Layer toggles are a core render option, not a driver hook.** Because the core composites
the frame itself from VRAM (`Vdp1Rasterizer` + `Vdp2Compositor`), it can include or exclude
any layer, window, shadow, or effect on demand. Toggling VDP1, an individual VDP2 background
(NBG0–3, RBG0), wireframes, bounds, priority labels, and object numbers are all fields of
`se_render_opts` passed to `se_render_frame()`. No driver cooperation is required — the same
toggles work against a static memory dump. (This is why Seam A has no layer-control
capability.)

**VDP2 backgrounds** fold into the same model: each active layer is a textured plane, placed
under/over the VDP1 sprites by priority in both the 2D composite and the 3D scene.

**Driver framebuffer (optional).** When a driver advertises `SE_CAP_FRAMEBUFFER`, the host can
show the emulator's real output side-by-side with the core's software render to validate
accuracy — a diff/comparison view, not the primary display path.

**Z-mapping is the one tunable.** The default world layout derives Z from priority + draw
order. If it proves more legible to space layers by VRAM bank, command range, or a
user-controlled exaggeration factor, that stays internal to `GeometryBuilder` and does not
affect either seam.

---

## 8. Threading & lifecycle

- The **host** owns the loop. Each displayed frame: `se_begin_frame()` snapshots state,
  then the host issues read-only queries against that snapshot. Snapshots are immutable,
  so panels can be queried in any order without re-reading the target.
- The core itself is **single-threaded**: it does its work inside the host's call and owns no
  threads. Search was to have run on core-managed workers polled through `se_rom_search_poll`;
  that never happened, and the search that exists runs on a frontend worker thread with its own
  cancellation (`SearchProgress`). The threads in this project are the frontend's workers, the
  live driver's poll thread, and the emulator-side tap's server and savestate threads.
- **Memory events** are pumped from the driver during `se_begin_frame` via `poll_events`. The
  access log that consumes them is a frontend system (see §6).

---

## 9. Versioning & compatibility

- `SE_ABI_VERSION` is a single integer bumped on any breaking change to either seam. It is at
  **7**: 7 appended `begin_capture` / `end_capture` to `se_data_source`, which bracket one
  snapshot so a driver whose state advances on another thread (the live tap) answers every read
  of a capture from the same instant -- optional, signalled by a non-NULL pointer rather than a
  capability bit, since there is nothing for the core to adapt to. 6 dropped the
  never-implemented `se_rom_search_*` / `se_history_for` exports and their query structs, and
  added `SE_ERR_NO_MEMORY`.
- Structs are **append-only** within an ABI version; new fields go at the end guarded by a
  new capability bit, so an old driver/host keeps working.
- The core exports `se_abi_version()` so a host can refuse an incompatible core before calling
  anything else, and `se_create` refuses a driver or config whose `abi_version` differs. Both
  are linked statically today; nothing ships as a DLL.
- Every seam function is **no-throw**, including under memory pressure — the host may be C, or
  JavaScript in the web build. `SeGuard.h` holds that contract and the reasoning.

---

## 10. Repository layout

What is actually there. (This section described a *proposal* through the first milestones, and
then went stale in the specific way that matters most: it named files under names they do not
have -- `hardware_snapshot.cpp`, `savestate_driver.cpp`, `Drivers/Emulator/` -- and a `.vcxproj`
per target for a build that is CMake. Review finding UI-02.)

```
SaturnExplorer/
  CMakeLists.txt                     ← one build for every target; see BUILD.md
  ARCHITECTURE.md                    ← this file
  include/saturnexplorer/            ← public headers: the two seams, plain C
    SeAbi.h                            versions, result codes, capability bits
    SeTypes.h                          POD structs (se_command, se_image, se_scsp_slot, …)
    SeDataSource.h                     Seam A (driver → core)
    SeHost.h                           Seam B (core → host)
    SeGuard.h                          the no-throw wrapper both seam shims sit behind
    SaturnExplorer.h                   umbrella include
  Core/                              ← the engine (C++14 static lib; also builds for wasm)
    src/                               Context.h, HostAbi.cpp, HardwareSnapshot.{h,cpp},
                                       Vdp1Parser, GeometryBuilder, Vdp1Rasterizer,
                                       Vdp2Compositor, PixelMixer.h, Vdp1Color.h, ByteOrder.h
    tests/                             through the public ABI (plus FakeVdpSource.h, shared)
    tools/SeRender.cpp                 se-render: composite a .sedump headlessly
  Drivers/                           ← Seam A implementers
    Common/src/                        SaturnStateShared: normalization shared by both drivers
    Savestate/                         .yss / MDFNSVST / region dir / raw dump  [+ tests/]
    Live/                              the running-emulator driver, over a socket or pipe
  Integration/                       ← the emulator side of the live tap
    Common/                            se_export.c (the server, plain C), SeLiveProtocol.h,
                                       SeStateCodec.h (rewind delta/RLE codec)  [+ tests/]
    Mednafen/                          apply.py: patches a Mednafen checkout. Read its README
    Yabause/                           the same shape for Yabause
  FrontEnd/                          ← the reference app (Dear ImGui)
    src/                               App.{h,cpp} + panels; portable, no OS calls
      DataSearchRunner.{h,cpp}         the data search's worker thread, queueing and routing
      Debug/                           debugger model: breakpoints, call stack, disasm,
                                       watch list, memory search, expression eval,
                                       StepHaltMachine (run control: the step hold and
                                       the transient Step Over/Out breakpoint)
      Disc/                            ISO/CUE read + build, CD sectors, disc image
      Demo/                            scripted demo playback
      Platform/IPlatform.h             Seam C: the platform abstraction
    Platforms/Windows|macOS|Web/       the only code that touches an OS or a GPU API
    tests/                             panel-interaction + model tests (ImGuiHarness.h)
    third_party/imgui/                 vendored ImGui (docking), core + backends
  Docs/                              ← the reviews, verification notes and hardware notes
  web/                               ← the wasm build's page and guides
```

The frontend is itself split so it can be ported (see §13). `Platforms/` holds the per-OS/GPU
backends; the portable `App` and panels depend only on the core (Seam B), ImGui and `IPlatform`.

ImGui is **vendored as source** under `FrontEnd/third_party/`, since the repo has no package
manager; the D3D11, OpenGL and SDL2 backends ship with it.

---

## 11. Milestones

1. **M0 — Seams (this doc + headers). [DONE]** `include/saturnexplorer/*.h` landed; verified
   compiling as C99/C11/C++14.
2. **M1 — Skeleton. [DONE]** Core static lib + savestate driver verified end to end
   (driver → `se_create` → `se_begin_frame` → snapshot) on a synthetic dump; ImGui frontend
   with the platform abstraction (Seam C) and a Win32/D3D11 backend, showing the empty docked
   panel layout. Portable layer (App + panels + ImGui core) compiles on non-Windows; the
   Win32/D3D11 backend builds in Visual Studio.
3. **M2 — Command list. [DONE]** `Vdp1Parser` walks the VDP1 command table (jump/call/return/
   skip, END, cycle-safe) into `se_command`; the Command List + Selected Object panels show it.
   Verified against a real Yabause dump (Sakura Taisen): 123 commands, matching an independent
   reference parse exactly. Priority (needs VDP2 SPCTL) and scale/rotation still default.
4. **M3 — Software render + 2D geometry. [DONE]** `GeometryBuilder` resolves LocalCoord →
   screen quads; `Vdp1Rasterizer` composites them (barycentric UV, vdp1_color texel/CRAM
   decode); `se_render_frame`/`se_sprite_2d`/`se_hit_test` wired. The frontend VDP Output panel
   renders the frame (via the IPlatform texture bridge), overlays bounding boxes / object
   numbers, and click-selects sprites through hit-testing. Verified against the battle dump:
   117 distorted sprites reconstruct the mech units pixel-faithfully.
5. **M4 — 3D world view. [DONE]** `GeometryBuilder` emits `se_sprite_3d` (screen XY
   centered, Z by draw order); the core software-renders the exploded view with a depth buffer
   (`se_render_3d` + `se_camera3d`); the frontend 3D View panel orbits (drag) and zooms (wheel).
   Verified against the battle dump via the public API (mechs depth-sorted with correct
   occlusion). VDP2 backgrounds followed in M4b.
   > **Line primitives:** polylines and lines (CMDCTRL command codes 5 and 6) are drawn in the
   > exploded view too, as depth-tested edges in their solid colour -- they are part of the VDP1
   > command list, and a view that dropped them showed nothing where a game drew a wireframe
   > overlay or a debug cross, with no way to tell that from the game not having drawn it.
   > Corners are *not* expanded for them (`ExpandQuadInclusive` closes seams between abutting
   > quad strips; a line has no interior to widen). The 3D hit test picks them by distance to
   > the edges the renderer draws rather than by containment, since a line has no interior to
   > contain a click -- and so a click in the empty middle of a polyline selects whatever is
   > drawn there instead, even though the polyline is nearer the camera.
   > **VDP2 note:** VDP2 control registers (BGON, CHCTL, map/scroll/priority, rotation params)
   > are write-only on hardware, so a RAM dump reads them back as zero — confirmed on both dumps
   > (VRAM 67-75% full, registers empty). VDP2 compositing needs the real register state, best
   > via a Yabause savestate (`.yss`, which serializes the internal register structs) or an
   > explicit register export. The live emulator driver (M7) would also supply them.
6. **M4b — VDP2 backgrounds. [DONE]** The savestate driver parses Yabause 0.9.15 `.yss` files
   (`se_savestate_open_yss`): VDP1/VDP2 VRAM, CRAM, and the VDP2 register file — rebuilt at
   hardware offsets from the packed struct via an **exact hw→struct offset table** (correct
   through the priority/color-offset registers, where the earlier `hw-2` approximation broke on
   the `u32` zoom/address union padding). `Vdp2Compositor` renders the NBG scroll screens by
   walking the plane→page→pattern-name→cell hierarchy (1/2-word pattern names; 16/256/2048-color,
   RGB555, RGB888 cells) and composites them back-to-front by priority; `Context::RenderFrame`
   lays the VDP1 sprites over that background. Verified against Battle3.yss: the isometric field
   (grass, stone walls, water channels, foliage) renders with the mech sprites correctly placed
   and colored.
   > **CRAM byte order.** Yabause keeps VDP2 color RAM in host-native order (its `T2` accessor),
   > so a `.yss` written on a little-endian host stores CRAM little-endian, whereas VRAM (`T1`)
   > stays big-endian like real hardware. The core is canonically **Saturn-native big-endian**
   > for all memory (`CramColor` reads BE, matching VRAM); the `.yss` driver **normalizes** CRAM
   > by byte-swapping on load (16-bit for RGB555 modes, 32-bit for RGB888). This was the fix for
   > the pink/blue speckle — a byte-swapped palette turns smooth ramps jumpy — and it corrected
   > the VDP1 sprite colors too.
   > **Known simplifications (M4b):** VDP1 and NBGs interleave by priority, per framebuffer pixel
   > -- a sprite whose CLUT or colour bank spans several SPCTL priority numbers interleaves at
   > each of them, as the hardware does. NBG0-3 normal and line windows are modeled;
   > sprite/color-calculation windows are not, and sprites do not yet carry a colour-calculation
   > ratio of their own. Still missing: rotation
   > screens (RBG0/1), bitmap-mode backgrounds, line/vertical-cell scroll, zoom, mosaic, color
   > calculation, and the VDP2 back/line-color screens (empty pixels use a flat backdrop). See
   > `Docs/RenderingAccuracyRoadmap.md` for the ordered completion plan.
   > **VDP1 framebuffer.** `Vdp1Rasterizer::EmitSprites` draws the command list into a VDP1
   > framebuffer (at VDP1's own width -- half the display in hi-res) in list order, then scans it
   > out to the mixer at the VDP2 priority each pixel's own word selects; priority 0 is not
   > displayed. Shadow / half-transparency read the framebuffer pixel, not VDP2, and do their
   > arithmetic on 5-bit channels; they act only on a pixel that holds an RGB colour. Texture end
   > codes (CMDPMOD ECD clear) and skipped (JP >= 4) commands follow the hardware.
   > **Sprite pixel coverage.** VDP1 corners are inclusive pixel *indices* and the game tiles a
   > mech out of many small strips; `ExpandQuadInclusive` moves each corner to its pixel's centre
   > and out half a pixel, so a primitive covers exactly the pixels between its corners and
   > adjacent strips neither overlap nor leave a seam.
   > **Multiple emulators.** `se_savestate_open` sniffs the file magic and dispatches to a
   > per-emulator parser, all producing the same `se_data_source` so the core stays format-agnostic.
   > Supported: the **Yabause family** `.yss` and **Mednafen/Beetle Saturn** `MDFNSVST`. The `.yss`
   > VDP2 section is matched *structurally* (at least a 288-byte `Vdp2` struct + 512 KiB VRAM + 4 KiB
   > CRAM; any trailing internal state is ignored) rather than by version number, because the `Vdp2`
   > register struct is a fixed hardware mirror shared byte-for-byte across the lineage (verified
   > identical in Yabause 0.9.x, Yaba Sanshiro, and Kronos) — so the one parser covers all three, and
   > safely degrades to VDP1-only for a fork that changed the struct/VRAM size. Mednafen's `ss` module instead
   > stores VRAM, CRAM, and a flat `RawRegs[0x100]` hardware-register array as host-endian `uint16`s,
   > byte-swapped to Saturn-native big-endian on load. **SSF** is closed-source, so there is no layout
   > to implement.
7. **M5 — Textures & VRAM. [DONE]** `se_decode_texture` walks a texture out of VDP1 VRAM through
   the shared `DecodeTexel` (index-0/RGB-code-0 left transparent); `se_decode_palette` reads a
   16-entry VDP1 CLUT, resolving color-bank codes through CRAM; `BuildVramRegions` (in
   `BeginFrame`) classifies VDP1 VRAM into the command-table / texture / CLUT / gouraud regions each
   drawable command references, deduped and sorted. The frontend gains a Texture Viewer (decodes the
   selected sprite's texture onto a transparency checkerboard), a Palette Viewer, and a VRAM Map. The
   Palette Viewer shows CLUT swatches for LUT sprites and, via `se_decode_bank_palette`, the CRAM
   sub-palette (16/64/128/256 entries) a color-bank sprite indexes into — with per-swatch hover
   (index / raw / RGB). The VRAM Map is a proportional 512 KiB region map with a kind legend, per-region
   hover tooltips (address / size / kind / owning command), and click-to-select. Verified against
   Battle3.yss: 161 VRAM regions, textures decode correctly, and a bank sprite's palette resolves to
   its CRAM colour ramp; the portable App layer compiles against ImGui.
8. **M6 — Search & trace. [DONE, in the frontend]** **Reference Explorer:**
   `se_references_of_texture` / `se_references_of_palette` scan the parsed command list for sprites
   sharing a texture VRAM address or CLUT, returning `se_reference`s (with the same object
   numbering GeometryBuilder uses); the References panel lists them for the selected sprite,
   click-to-select. **Disc + search:** the ISO9660 reader (`Disc/IsoFs`), CUE/BIN and disc-image
   handling, and the Disc Explorer landed in the frontend, together with a byte-sequence search
   over a data directory or an image (`DataSearch`) that also finds a needle inside a
   PRS-compressed block. None of that went behind Seam B as originally planned: the search is
   interactive, cancellable and holds state across frames, so it belongs to the host, and the
   `se_rom_search_*` / `se_history_for` declarations that anticipated it were removed at ABI 6
   (review finding ABI-03).
9. **M7 — Live driver. [DONE]** `Drivers/Live` connects to an emulator patched by
   `Integration/Mednafen/apply.py` (or the Yabause equivalent) over a Unix socket, a Windows named
   pipe, or TCP for the web build, and speaks the versioned protocol in `SeLiveProtocol.h`. Beyond
   the per-frame snapshot: pause / frame step / instruction step, execution breakpoints and memory
   watchpoints, tracepoints, controller injection, writes back to work RAM and sound RAM, decoded
   SCSP voices, live CD-block state, the emulator's own save slots, and savestate rewind with a
   keyframe/delta codec (`SeStateCodec.h`). The frontend's debugger, access log, rewind timeline
   and patch workflow are built on it. Read `Integration/Mednafen/README.md` before touching the
   emulator side -- a wrong binding there fails silently.
10. **M8 — ROM hacking workflow. [DONE]** Locate an edited memory range inside the game's data
   files, record the mapping, and emit a patch script that verifies the on-disc baseline before
   writing (`PatchLibrary`); rebuild a bootable image from a directory of extracted files
   (`Disc/IsoBuilder`, preserving the 32 KB IP.BIN boot header) and launch it.

---

## 12. Coding conventions

Naming below applies to the **C++ implementation** (core, drivers, frontend). The
public C-ABI headers are an exception for *identifiers* only: their types and
functions stay `snake_case` (`se_result`, `se_create`) — that is the stable C
interface third parties compile against — even though the header *files* follow
the UpperCamelCase file rule like everything else.

- **File names:** `UpperCamelCase` — `HardwareSnapshot.cpp`, `Vdp1Parser.h`,
  `SeHost.h`. Directories stay lowercase (`src/`, `include/saturnexplorer/`).
- **Functions / methods:** `UpperCamelCase` — `Capture`, `BuildDataSource`.
- **Member variables:** `m` + `UpperCamelCase` — `mVdp1Vram`; booleans `mb` + `UpperCamelCase` — `mbValid`.
- **Local variables & parameters:** `lowerCamelCase` — `baseAddress`, `count`.
- **Braces:** Allman — opening brace on its own line. (Trivial one-line inline
  accessors may keep `{ ... }` on the same line, matching the existing repo.)
- **File-scope constants:** `k` + `UpperCamelCase` — `kVdp1VramSize`.

---

## 13. Frontend architecture — Seam C (Platform)

The `FrontEnd` app is split the same way the product is: a portable core of logic
plus a swappable platform backend, so it can be ported without touching the UI.

**Three layers:**

1. **Portable app + panels** (`FrontEnd/src/`) — `App` owns the core context, the
   data source, the render toggles, and the current selection, and draws every
   panel each frame. It depends only on the core (Seam B), **ImGui** (portable),
   and `IPlatform`. No Win32/D3D11 types appear here.
2. **`IPlatform`** (`FrontEnd/src/Platform/IPlatform.h`) — **Seam C**, the port
   boundary. It abstracts: window + event loop (`PumpEvents`), frame begin/end
   (wrapping the ImGui backend + `NewFrame`/`Render`/present), a **GPU texture
   bridge** for `se_image` (`CreateTexture`/`UpdateTexture`/`DestroyTexture` →
   `ImTextureID`, so panels can display decoded frames/textures), and a native
   file-open dialog.
3. **Platform backends** (`FrontEnd/Platforms/<name>/`) — implement `IPlatform`
   and own the entry point + the ImGui platform/renderer backends. Two exist:
   - `Windows/` (Win32 + Direct3D 11) — the native desktop reference.
   - `Web/` (SDL2 + OpenGL) — an Emscripten/**WebGL2** build for the browser that
     also compiles natively against desktop SDL2 + OpenGL (a browser-free way to
     run the frontend). `WebPlatform` swaps the D3D11 texture bridge for GL
     textures and the Win32 message loop for `emscripten_set_main_loop`; the one
     browser-specific wrinkle is file loading — there is no synchronous native
     dialog, so files arrive as bytes from JS (drag-drop or `<input type=file>`)
     through `App::OpenSavestateBuffer`, backed by the driver's buffer-based
     `se_savestate_open_buffer`. Multi-viewport (floating OS windows) is disabled
     on the web; docking within the canvas still works.

   Adding a backend is additive: the portable App and every panel are shared
   untouched, and the CMake `if(WIN32)/elseif(EMSCRIPTEN)/else()` selects one.

**Loop ownership:** the platform's entry point owns the main loop and drives
`PumpEvents → BeginFrame → App::BuildUI(platform) → EndFrame`. `App` never sees
the OS.

**Two independent axes — don't conflate them:** a **data-source driver** (Seam A)
is *where Saturn state comes from* (savestate, emulator); a **platform backend**
(Seam C) is *the host OS/GPU*. A Windows build reading a savestate uses the
Windows platform backend **and** the savestate driver; each can change without
the other.

The core returns decoded `se_image` (RGBA) and geometry; the platform uploads to
the GPU and ImGui draws it. No graphics API type ever crosses Seam B — this is
what lets an emulator embed the core under its own renderer.

## Proposals (not built)

- **User-scriptable panels (Python):** [`SCRIPTING.md`](SCRIPTING.md) — a design
  for letting users write Python scripts that appear as panels, using the Seam B
  C ABI as a ready-made data API. Not started; captured for later.
- **Execution Actions (tracepoints + structured Log):**
  [`EXECUTION_ACTIONS.md`](EXECUTION_ACTIONS.md) — generalize breakpoints into
  per-address actions (Break, non-halting Tracepoint/Log, and future types), with a
  filterable event Log and a format mini-syntax. Built (phases 1–4); the client, wire
  protocol (v8), and Mednafen glue are in.
- **Call Stack + paused-state workspace:** [`CALL_STACK.md`](CALL_STACK.md) — a per-CPU
  call stack shown when execution stops, from an instrumented shadow stack (● confirmed)
  or a heuristic reconstruction of the stack image (◐ probable / ○ heuristic), with a
  coordinated breakpoint-hit workspace. Built: the client model, the heuristic reconstructor
  and the panel, plus the shadow stack over the wire (v9) and its Mednafen glue.
