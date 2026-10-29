# Design: user-scriptable panels (Python)

**Status:** proposal / not started. This captures the idea and the work involved so we
can pick it up later. Nothing here is built yet.

## Goal

Let users drop a Python script into a folder and have it appear as a panel inside Saturn
Explorer, with read (and eventually write) access to everything the app already pulls from
the emulator — VDP1/VDP2 VRAM, CRAM, work RAM, registers, the decoded VDP1 command list,
decoded textures/palettes, SH-2 state — so they can build whatever analysis or visualization
they want without touching C++ or rebuilding the app.

## Why this fits the existing architecture unusually well

Two properties of the current design do most of the work for us (see `ARCHITECTURE.md`):

1. **The data is already behind a stable C ABI (Seam B).** `se_read_vram`,
   `se_read_cram_colors`, `se_decode_texture`, `se_decode_palette`, `se_command_count`,
   `se_get_system_status`, `se_references_of_texture/palette`, `se_history_for`, … are all
   C functions on `se_context`. A Python binding is a thin wrapper over an interface that
   already exists and is already versioned — not a new extraction. That's ~80% of the data
   side done.
2. **Every panel is already "a function that draws each frame."** The built-in panels
   (`DrawTextureViewer`, `DrawRegisters`, …) are uniform immediate-mode draw callbacks over
   the current `se_context` (savestate *or* live snapshot). A script panel is the same shape
   — a `draw()` called once per frame inside a `Begin/End` — so it slots into the existing
   loop, the Windows-menu visibility toggles, and the layout/persistence system (see the
   `PanelList()` table and `Settings` module) with no special-casing.

So the scripting layer is mostly: embed an interpreter, expose two clean modules (`saturn`
data + `ui` drawing), and register script panels into the machinery that already exists.

## The one decision that shapes everything: in-process vs out-of-process

### Option A — embed CPython in the app  *(recommended)*
Link a Python interpreter into the frontend; scripts run in-process and draw real ImGui
widgets each frame via a bound `ui` module.

- **Pros:** tight integration; panels are first-class (real widgets, per-frame, docked
  alongside built-ins); direct access to in-memory data with minimal copying; hot-reload is
  easy; reuses the Windows-menu + Settings persistence for free.
- **Cons:** bundles a Python runtime (Windows ships a ~10 MB "embeddable" CPython); a script
  can stall the frame or, via bad native calls, crash the app (mitigate: catch Python
  exceptions per-frame and show the traceback *in the panel*); the `saturn`/`ui` API becomes
  a compatibility surface we must version (same discipline as `SeLiveProtocol`). **The web
  build won't get this** (Emscripten + CPython means Pyodide, which is heavy and has an awkward
  threading/ABI story) — so scripting is a native-desktop / Windows power feature, gated
  behind an `SE_ENABLE_PYTHON` compile flag exactly like `SE_ENABLE_LIVE`.

### Option B — out-of-process Python SDK  *(cheap stepping stone, not a replacement)*
We already have a live IPC protocol (`SeLiveProtocol`, the LiveDriver ⇄ emulator export
server). A small Python client library over it lets people write **analysis** scripts today —
connect, pull VRAM/registers, do pandas/matplotlib, render their own windows — with almost
no C++ work.

- **Pros:** trivial to ship; total crash isolation; any Python version; no runtime to bundle.
- **Cons:** those scripts can't draw a panel *inside* SE's window (they'd render offscreen and
  we'd blit a texture back, or accept a declarative UI description). Great for "analyze the
  data," weak for "custom in-app panel."

**Recommendation:** build **A**, but consider shipping **B** first (or alongside) as a quick
win, since it's nearly free given the live protocol already exists.

## Proposed API surface (Option A)

Two Python modules, both thin.

### `saturn` — read-only data (v1)
A wrapper over the current `se_context` (Seam B). Illustrative, not final:

```python
import saturn

saturn.has_data()                 # is a savestate/live source loaded?
saturn.is_live()                  # live emulator vs static savestate

# raw memory (big-endian bytes, exactly what se_read_vram returns)
saturn.vram1(addr, length)        # VDP1 VRAM      -> bytes
saturn.vram2(addr, length)        # VDP2 VRAM      -> bytes
saturn.work_ram(addr, length)     # -> bytes
saturn.read_u8(addr) / read_u16(addr) / read_u32(addr)

saturn.cram()                     # decoded CRAM colors -> list[(r,g,b)]
saturn.registers()                # VDP1/VDP2 + SH-2 register snapshot -> dict
saturn.system_status()            # se_get_system_status -> dict

# the decoded VDP1 command list (what the Command List panel shows)
saturn.command_count()            # se_command_count
saturn.command(i)                 # one command's fields -> dict/object
saturn.decode_texture(i)          # se_decode_texture     -> (w, h, rgba: bytes)
saturn.decode_palette(i)          # se_decode_palette     -> list[(r,g,b,a)]
saturn.references_of_texture(i)   # cross-refs
```

Notes:
- The disassembler lives in the frontend (`Debug/Sh2Disasm.cpp`), not the core ABI — expose a
  `saturn.disasm(addr, count)` that calls into it, or leave disasm out of v1.
- Everything returns plain Python types (`bytes`, `dict`, tuples) so scripts have no ImGui/C++
  objects to leak or mismanage.

### `ui` — a curated immediate-mode subset (v1)
A small, safe slice of ImGui so panels can actually draw. Not the whole API — just enough to
be useful and stable:

```python
ui.text(s); ui.text_colored(rgba, s)
ui.separator(); ui.same_line()
if ui.button("Scan"): ...
val = ui.slider_int("N", val, lo, hi)
with ui.table("cols", n_cols): ui.row(...); ...
ui.plot_lines("hist", values)
ui.image(rgba_bytes, w, h)        # e.g. a script-decoded texture or a matplotlib figure
```

`ui.image` is the escape hatch for "draw anything": a script can build a NumPy/matplotlib RGBA
buffer and hand it over, and SE uploads it through the existing `IPlatform` texture bridge.

### Panel contract
A `.py` in a `scripts/` folder exposes a title and a per-frame draw:

```python
# scripts/vram_histogram.py
TITLE = "VRAM Histogram"

def draw(saturn, ui):
    data = saturn.vram1(0, 0x80000)
    ui.plot_lines("byte freq", histogram(data))
```

(Or a class with `title` + `draw(self, saturn, ui)` if a script wants persistent state.)

## Loading, lifecycle, safety

- **Discovery:** scan a `scripts/` dir at startup; each valid script auto-registers as a panel
  and gets a Windows-menu entry + visibility persistence *for free* by adding it to the same
  `PanelList()` machinery the built-ins use (script panels become dynamic entries in that list).
- **Hot reload:** a "Reload Scripts" button re-imports the folder. This is the single biggest
  quality-of-life feature for authors — iterate without restarting.
- **Error isolation:** wrap each script's `draw()` in a try/except; on exception, render the
  traceback text *inside that panel* and keep the app running. A script can't take down SE via
  a Python error (native segfaults from misuse are still possible — documented, not sandboxed).
- **Performance:** a script doing heavy per-frame work will drop FPS. Offer a per-panel "run
  every N frames" throttle, and/or run expensive work only on an explicit button.
- **Security:** arbitrary Python = arbitrary code execution, at the same trust level as the app
  itself. Fine for a local RE tool running the user's *own* scripts. Do **not** auto-download or
  auto-run scripts from untrusted sources. No sandbox in v1.

## API versioning

Once users write scripts, `saturn`/`ui` are a public compatibility surface. Give the `saturn`
module a `saturn.API_VERSION` and bump it on breaking changes, mirroring how `SeLiveProtocol` is
versioned and surfaced in the title bar. Additive changes don't bump; removals/renames do.

## Build / packaging

- New compile flag `SE_ENABLE_PYTHON` (native desktop + Windows only), parallel to
  `SE_ENABLE_LIVE`. Web build leaves it off and inherits no scripting.
- **Binding tech:** pybind11 (header-only, easy to vendor under `third_party/`) over the
  existing C ABI. Alternatively a hand-written CPython C-API layer, or `cffi`/`ctypes` against
  the C ABI with no C++ at all (simplest binding, clumsier `ui`).
- **Runtime:** bundle Windows' embeddable CPython (~10 MB) with the app, or require a system
  Python. The installer (`Integration/install.py`, already Python) can set up the embedded
  runtime.
- **Both build systems** must pick it up: add the Python sources/flag to the CMake `WIN32`
  target *and* to `FrontEnd.vcxproj` (the vcxproj globs `src/**`, so a `Scripting/` subdir under
  `FrontEnd/src` is auto-included; the libpython link + include dirs are the manual part).

## Phased work breakdown

Each phase is independently useful and shippable.

**Phase 1 — embed + data bindings + one built-in console.**  *(the spike)*
- Add `SE_ENABLE_PYTHON`, vendor pybind11, link libpython (CMake + vcxproj).
- Bind `saturn` (read-only) over the C ABI.
- One hardcoded built-in "Python Console" panel: a REPL that can print live Saturn state.
- Proves the embed, the binding, and the data round-trip. *Rough size: medium.*

**Phase 2 — script-defined panels.**
- Bind the `ui` immediate-mode subset.
- `scripts/` discovery + auto-registration into `PanelList()` + Windows-menu + persistence.
- Hot reload + in-panel error/traceback display.
- Ship 2–3 example scripts (VRAM histogram, a custom sprite lister, a palette grid).
  *Rough size: medium–large (the `ui` binding + registration are the bulk).*

**Phase 3 — write access & niceties.**
- `saturn.write_*` over `se_write_vram` / `se_can_write` (respect the live memory-write seam).
- `ui.image` + a matplotlib-figure → texture helper.
- Optional: custom *data-source* scripts, script-defined exporters.
  *Rough size: medium.*

**Optional Phase 0 — out-of-process SDK (Option B).**
- A `saturn_live.py` client over `SeLiveProtocol`: connect, pull data, script freely in a
  normal Python process. Nearly free given the protocol exists; good for people who just want
  to analyze, and a hedge if embedding slips. *Rough size: small.*

## Open questions

- **pybind11 vs ctypes-over-C-ABI** for the binding — pybind11 gives a nicer `ui`, ctypes needs
  zero C++ but a clumsier drawing layer. Lean pybind11.
- **Bundle CPython vs require system Python** — bundling is friendlier but bigger; the
  embeddable package is the likely answer for Windows.
- **How much of ImGui to expose** in `ui` — start deliberately small; grow on demand. A too-wide
  binding is a maintenance and stability liability.
- **Script panels in the default layout / `imgui.ini`** — dynamic panels need stable dock IDs
  keyed by script name so a user's arrangement survives a reload.
- **Threading** — keep scripts on the UI thread for v1 (call `draw()` during the frame). Async
  work is a later concern.

## Not doing (for now)

- Web/Emscripten scripting (Pyodide) — out of scope; scripting is a native feature.
- Sandboxing untrusted scripts — the trust model is "your own scripts," same as the app.
- A package manager / script marketplace — a plain folder is enough to start.
