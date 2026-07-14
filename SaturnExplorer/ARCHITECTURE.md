# Saturn Explorer — Architecture

> Status: **Design draft for review.** No project restructuring has been done yet;
> the current `SaturnExplorer.vcxproj` is a placeholder. This document defines the
> component split, the two interface seams, and the module breakdown so we can
> agree on the boundaries before writing them.

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
   Seam A (data)        │            SaturnExplorerCore            │        Seam B (host)
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

### 2.1 `SaturnExplorerCore` — the engine (library)

All platform-agnostic logic. Has **no** dependency on a UI framework, no windowing,
and no Win32 types in its public headers. Builds as a **static lib** (for the bundled
frontend) and optionally as a **DLL** (for third parties who load it at runtime).

Owns: VDP1/VDP2 parsing, texture/palette decoding, the VRAM map, the command-table
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
    SE_CAP_FRAMEBUFFER   = 1 << 6,   /* composited frame readback */
    SE_CAP_LAYER_CONTROL = 1 << 7,   /* driver can toggle VDP2 layers / re-render */
    SE_CAP_EVENT_STREAM  = 1 << 8,   /* memory-write / DMA events (Memory History) */
    SE_CAP_DISC          = 1 << 9,   /* raw ISO / file access (ROM Search) */
    SE_CAP_FRAME_STEP    = 1 << 10,  /* pause / step / advance (Frame Timeline) */
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

    /* --- Optional: composited framebuffer readback (SE_CAP_FRAMEBUFFER). --- */
    int (*get_framebuffer)(void* user, se_framebuffer* out);

    /* --- Optional: layer control (SE_CAP_LAYER_CONTROL). --- */
    int (*set_layer_enabled)(void* user, se_layer layer, int enabled);

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

Rendering stays entirely in the host — the core returns **data and decoded images**
(`se_image` = raw RGBA + metadata), never ImGui or D3D calls. The host uploads
`se_image` to a texture and draws it.

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
| `Compositor` | Optional software re-render of VDP1 sprites for layer toggling (see §7) | Live Visualization |
| `SearchEngine` | Scans disc/archives/compressed assets for a target asset | ROM & Archive Search |
| `AssetTracer` | Correlates a texture with disc origin + references + history | Asset Trace |
| `ReferenceIndex` | Reverse index: texture/palette → commands that use it | Reference Explorer |
| `MemoryHistory` | Ring buffer of `se_mem_event`, indexed by address range | Memory History |
| `FrameTimeline` | Snapshot bookmarks, frame compare, step control | Frame Timeline |

---

## 7. Rendering & compositing strategy (open decision)

"Toggle individual VDP2 layers / VDP1 sprites" is the one feature that can't be served by
a plain framebuffer readback — a finished frame has already been composited. Two ways to
get selective layers, not mutually exclusive:

- **(A) Driver-controlled** — the driver exposes `SE_CAP_LAYER_CONTROL` and toggles layers
  inside the emulator, then the core reads back the framebuffer. Accurate (it's the real
  hardware path) but only possible when the driver is a cooperating emulator.
- **(B) Core software re-render** — the `Compositor` re-rasterizes VDP1 sprites from the
  command table we already parse, and optionally VDP2 tilemaps from VRAM. Works with any
  driver (even a static dump), but is a large amount of work to match hardware exactly.

**Proposed phasing:** start with the driver framebuffer as the base image and the core's
sprite re-render for **overlays** (wireframes, bounds, priority/object-number labels,
sprite highlight-on-select). Add full core VDP2 compositing later. Use driver layer control
when `SE_CAP_LAYER_CONTROL` is present. This gives the selection/inspection UX immediately
without blocking on a full software VDP.

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
  ARCHITECTURE.md                 ← this file
  include/saturnexplorer/         ← public C-ABI headers (the two seams)
    se_abi.h                      ← versions, result codes, capability bits
    se_data_source.h             ← Seam A
    se_host.h                     ← Seam B
    se_types.h                   ← POD structs (se_command, se_image, se_mem_event, …)
  Core/                           ← SaturnExplorerCore (C++ static lib + optional DLL)
    src/…                         ← modules from §6
    SaturnExplorerCore.vcxproj
  Drivers/
    Savestate/                    ← first reference driver (static dump)
    Emulator/                     ← later
  Frontend/                       ← reference Win32 app (Dear ImGui + D3D11)
    src/… (panels: RenderView, CommandTable, TextureViewer, VramMap, Search, Trace)
    third_party/imgui/            ← vendored (repo has no package manager)
    SaturnExplorer.vcxproj
```

`imgui` will be **vendored as source** under `Frontend/third_party/` since the repo has no
package manager; the D3D11 + Win32 ImGui backends ship with it.

---

## 11. Milestones

1. **M0 — Seams (this doc + headers).** Land `include/saturnexplorer/*.h`; no logic yet.
2. **M1 — Skeleton.** Core static lib that compiles against the headers, a savestate driver
   stub, and an ImGui+D3D11 window that calls `se_create` and shows an empty layout.
3. **M2 — First vertical slice.** Savestate → `Vdp1Parser` → Command Table Explorer panel
   with sprite selection + the Sprite Inspection detail view.
4. **M3 — Textures & VRAM.** `TextureDecoder`, Texture & Palette Viewer, VRAM Visualization.
5. **M4 — Search & trace.** `SearchEngine`, ROM & Archive Search, Reference Explorer.
6. **M5 — Live driver.** Emulator driver with framebuffer + event stream; Memory History,
   Frame Timeline, layer control.
