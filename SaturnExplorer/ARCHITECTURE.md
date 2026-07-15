# Saturn Explorer — Architecture

> Status: **M1 complete.** The two seams exist as headers under
> `include/saturnexplorer/`, the core static lib implements the C++/C-ABI boundary,
> the savestate driver is functional, and the `FrontEnd` app (ImGui + a Win32/D3D11
> platform backend behind the Seam C abstraction) shows the docked layout. **M2 is
> also done:** the VDP1 command table is parsed and shown in the Command List +
> Selected Object panels, verified against a real Yabause dump. Next is M3 (software
> render + 2D geometry). This document is the source of truth for the component split,
> the three interface seams (A data, B host, C platform), and the module breakdown.

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
- **Savestate/dump driver** — reads a static snapshot. First to build; needs no live target.
- **Emulator driver** — hooks a running emulator (Yabause/Mednafen/Kronos style). Adds
  live registers, framebuffer readback, and — where the emulator cooperates — a
  memory-access event stream.

### 2.3 Frontend — the host (Seam B consumer)

Consumes the core's **Host API** to render the UI. The reference frontend is a standalone
**Win32 app using Dear ImGui + D3D11**. It is *also* where the reference driver lives —
in practice a "platform driver app" ships a driver and a frontend together.

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
| `SearchEngine` | Scans disc/archives/compressed assets for a target asset | ROM & Archive Search |
| `AssetTracer` | Correlates a texture with disc origin + references + history | Asset Trace |
| `ReferenceIndex` | Reverse index: texture/palette → commands that use it | Reference Explorer |
| `MemoryHistory` | Ring buffer of `se_mem_event`, indexed by address range | Memory History |
| `FrameTimeline` | Snapshot bookmarks, frame compare, step control | Frame Timeline |

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
   X/Y/Z). Overlapping 2D sprites are separated along **Z by priority, then stable draw
   order** as tiebreak, so the scene "explodes" into layers. The **core owns the geometry;
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
- **Search** is long-running and runs on core-managed worker threads; the host polls
  results incrementally (`se_rom_search_poll`) and shows a confidence-rated result list.
- **Memory History** events are pumped from the driver during `se_begin_frame` (or on a
  driver callback) into the core's ring buffer; the host never touches the event stream directly.

---

## 9. Versioning & compatibility

- `SE_ABI_VERSION` is a single integer bumped on any breaking change to either seam struct.
- Structs are **append-only** within an ABI version; new fields go at the end guarded by a
  new capability bit, so an old driver/host keeps working.
- The DLL exports a `se_abi_version()` entry point so a host can refuse an incompatible core
  before calling anything else.

---

## 10. Proposed repository layout

```
SaturnExplorer/
  ARCHITECTURE.md                    ← this file
  include/saturnexplorer/            ← public headers (the two seams) [DONE]
    se_abi.h                         ← versions, result codes, capability bits
    se_types.h                       ← POD structs (se_command, se_image, se_mem_event, …)
    se_data_source.h                 ← Seam A
    se_host.h                        ← Seam B
    saturnexplorer.h                 ← umbrella include
  Core/                              ← SaturnExplorer core engine (C++ static lib) [DONE]
    src/
      context.h                      ← C++ core behind the opaque se_context*
      host_abi.cpp                   ← C-ABI shim for Seam B
      hardware_snapshot.{h,cpp}      ← pulls state through Seam A per frame
    SaturnExplorer.vcxproj
  Drivers/
    Savestate/                       ← reference driver: region dir + full dump [DONE]
      src/savestate_driver.{h,cpp}
      SaturnExplorerSavestateDriver.vcxproj
    Emulator/                        ← later
  FrontEnd/                          ← reference app (Dear ImGui) [M1 DONE]
    FrontEnd.vcxproj                 ← builds SaturnExplorer.exe
    src/
      App.{h,cpp}                    ← portable: owns core ctx + driver + panels
      Platform/IPlatform.h           ← Seam C: the platform abstraction
    Platforms/
      Windows/                       ← Win32 + D3D11 implementation of IPlatform
        WindowsPlatform.{h,cpp}
        WinMain.cpp                  ← entry point + main loop
    third_party/imgui/               ← vendored ImGui (docking), core + backends
```

The frontend is itself split so it can be ported (see §13). `Platforms/` holds
per-OS/GPU backends; only they touch Win32/D3D11. The portable `App` and panels
depend solely on the core (Seam B), ImGui, and `IPlatform`.

Core modules from §6 (Vdp1Parser, TextureDecoder, SearchEngine, …) attach to the
`Core/src/` skeleton at their milestones; M1 ships only the context + snapshot +
C-ABI shim, with every Seam B query present but returning `SE_ERR_UNIMPLEMENTED`.

`imgui` will be **vendored as source** under `Frontend/third_party/` since the repo has no
package manager; the D3D11 + Win32 ImGui backends ship with it.

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
4. **M3 — Software render + 2D geometry. [CORE DONE]** `GeometryBuilder` resolves LocalCoord →
   screen quads; `Vdp1Rasterizer` composites them (barycentric UV, vdp1_color texel/CRAM
   decode); `se_render_frame`/`se_sprite_2d`/`se_hit_test` wired. Verified against the battle
   dump: the 117 distorted sprites reconstruct the mech units pixel-faithfully, hit-testing
   maps screen→command. Remaining: the frontend VDP Output panel (display + overlays + click).
5. **M4 — 3D world view.** Emit `se_sprite_3d`; frontend camera to orbit/fly the exploded
   geometry. `Vdp2Compositor` for background layers in both views.
6. **M5 — Textures & VRAM.** `TextureDecoder`, Texture & Palette Viewer, VRAM Visualization.
7. **M6 — Search & trace.** `SearchEngine`, ROM & Archive Search, Reference Explorer.
8. **M7 — Live driver.** Emulator driver with event stream (+ optional reference framebuffer);
   Memory History, Frame Timeline.

---

## 12. Coding conventions

Applies to the **C++ implementation** (core, drivers, frontend). It does **not**
apply to the public C-ABI headers (`include/saturnexplorer/*.h`), which stay
`snake_case` — that is the stable C interface third parties compile against.

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
   and own the entry point + the ImGui platform/renderer backends. `Windows/`
   (Win32 + D3D11) is the reference; an SDL/OpenGL backend would slot in beside it.

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
