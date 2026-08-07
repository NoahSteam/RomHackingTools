# Saturn VDP1 — a plain-English guide

This guide explains **what the Saturn's VDP1 chip does** and **how Saturn Explorer's VDP1
tools let you inspect, understand, and extract the graphics it draws**. No prior hardware
knowledge is assumed — every term is defined the first time it appears.

The Saturn has **two** video chips that work as a team. This guide is about **VDP1, the
drawing engine** — it draws the moving, textured, 3D-ish stuff: characters, ships, effects,
polygons. Its teammate **VDP2** handles scrolling backgrounds and final compositing; it has
its own guide (`VDP2_GUIDE.md`).

If you remember one thing: **everything VDP1 draws is a four-cornered quad.**

---

## Part 1 — What VDP1 does

### The drawing engine

VDP1 is a **sprite and polygon drawing processor**. Every frame, it works through a
to-do list of drawing commands and paints the results into an off-screen image called a
**frame buffer**. When it's done, that image is handed to VDP2, which layers it together
with the backgrounds and sends the final picture to the TV.

The important mental model: VDP1 doesn't think in "sprites sitting at an X/Y position"
like an old 2D console. It thinks in **quads** — shapes with **four independently placed
corners**. This is what makes the Saturn's graphics distinctive:

- A **normal sprite** is just a quad whose corners form an upright rectangle.
- A **scaled sprite** is a quad stretched bigger or smaller.
- A **distorted sprite** is a quad with its four corners dragged anywhere — which is how
  the Saturn fakes 3D, warps, and perspective. Under the hood, *all* textured sprites are
  distorted sprites; the upright and scaled ones are just the easy special cases.
- VDP1 also draws untextured **polygons**, **polylines**, and **lines**.

So when you see a spinning, tilting, warping object in a Saturn game, you're usually
looking at a flat texture stretched across a quad whose corners are being moved each frame.

### The command table — VDP1's to-do list

VDP1 is driven by a **command table** in its memory: a linked list of drawing commands.
Each command is **15 words (32 bytes)** describing one thing to draw — its type, where its
texture is, its four corner coordinates, its color mode, and drawing options. The chip
walks the list in order (commands can jump, call, and return like little subroutines) until
it hits an **end** marker.

Each command's 15 fields have names you'll see in the tool: `CMDCTRL` (control/type),
`LINK` (address of the next command), `PMOD` (draw mode), `COLR` (color/palette), `SRCA`
(texture address), `SIZE`, the four corners `XA YA XB YB XC YC XD YD`, and `GRDA` (a
pointer to Gouraud shading data).

### Textures and color modes

A textured sprite reads its picture (its **texture**) from VDP1's 512&#8202;KB of memory.
Textures come in several **color modes**, which trade color count against memory:

| Mode | Colors | How it stores pixels |
|------|--------|----------------------|
| **16 (bank)** | 16 | 4 bits/pixel; index into a 16-color slice of the palette |
| **16 (LUT)** | 16 | 4 bits/pixel; a small lookup table in VDP1 memory picks the colors |
| **64 / 128 / 256 (bank)** | 64–256 | 8 bits/pixel; index into a larger palette slice |
| **RGB555** | ~32,000 | 16 bits/pixel; the color is written directly, no palette |

"Bank" and "LUT" modes are **palettized** — the pixels are just numbers that look up real
colors in a shared palette (see below). RGB555 is **direct color** — each pixel already
*is* a color. Direct color looks great but eats twice the memory.

One universal rule: **palette index 0 means transparent** (unless the command turns that
off with a flag called SPD, "transparent-pixel disable").

### The palette lives in VDP2

Here's a teamwork detail: VDP1's palettized textures don't carry their own colors. The
actual RGB values live in **Color RAM (CRAM)** — a palette shared with VDP2. A texture
pixel says "color #37"; CRAM says what #37 actually looks like. That's why recoloring or
understanding a sprite often means looking at both the texture *and* the palette.

### Extra tricks: Gouraud, draw modes, flips

Each command can enable effects:

- **Gouraud shading** — smoothly blends brightness across the quad from values at its four
  corners, so a flat texture can appear lit or shaded. (Named after Henri Gouraud.)
- **Draw modes** — how the sprite combines with what's already drawn: **Normal**,
  **Mesh** (a checkerboard stipple used to fake transparency), **Shadow**, **Half
  Luminance** (half-brightness), and **Half Transparent** (true 50% blend).
- **Flip** — mirror the texture horizontally and/or vertically.

### The double frame buffer

VDP1 has **two** frame buffers (each 512×256 pixels) and **swaps** between them: it draws
into one while the other is being displayed, then flips. This double-buffering is why you
never see a half-drawn frame.

---

## Part 2 — How Saturn Explorer sees VDP1

A key idea behind the tool: it doesn't just read back a finished picture. It contains a
**software VDP1** that **reconstructs the frame from the individual commands, exactly the
way the real chip does** — so you can inspect every sprite as a separate object, not a flat
image. That's what makes selecting, isolating, and extracting individual pieces possible.

### 🗂️ VDP1 Command List — *the to-do list, readable*

A table of every command the chip will draw this frame, with columns for **#**, **Type**
(Normal / Scaled / Distorted / Polygon / Polyline / Line / clip / coordinate commands),
**Size** (with the texture's memory footprint), **Position**, **Color** mode, and **Tex
Addr**. Click a row to select that object; **Shift-click** to select several at once;
double-click to jump to its bytes in memory.

### 🔎 Selected Object — *one command, fully decoded*

The complete inspector for the selected command: its table/link/texture/palette addresses,
size, position, color mode, **Draw Mode** (Normal / Mesh / Shadow / Half Luminance / Half
Transparent), transparency, **Gouraud** on/off, color-calc, flip, and the raw `CMDCTRL` /
`CMDPMOD` control words.

### 🖼️ Texture Viewer — *see (and export) a sprite's texture*

Decodes the selected sprite's texture and shows it over a checkerboard (so transparent
pixels read clearly), with a header giving its dimensions, color mode, and memory address.
**Export** saves it as a BMP — and if the sprite is palettized, the BMP keeps the game's
palette. You can also right-click to **break when this texture is written to**, or jump
straight to its bytes.

### 🎨 Palette Viewer — *the colors behind a sprite*

For a palettized sprite, shows its palette as a grid of swatches — either the shared CRAM
bank it uses or, in LUT mode, the small lookup table in VDP1 memory. Hover any swatch for
its index and RGB value. (Direct-color RGB555 sprites have no palette, and it says so.)

### 🗺️ VRAM Map (VDP1) — *what's using the 512&#8202;KB, and where*

A map of VDP1's whole memory, color-coded by what each region is: **Command Table**,
**Texture**, **CLUT** (a LUT-mode lookup table), **Gouraud Table**, **Unused**, and
**Other**, with a legend and a "% of 512&#8202;KB used" readout. Hover a block to see its
address, size, and which command owns it; click to select that command. A great way to see
how a game budgets its graphics memory.

### 🧊 3D View — *the frame exploded in space*

Renders the frame's quads as objects you can **orbit** (drag) and **zoom** (wheel).
Overlapping sprites are separated along depth so you can see the layering. **Click** an
object to select it; **Shift-click** to add more to the selection — the tool ray-tests your
click against the actual quad geometry to figure out what you hit. Selection is shared with
every other panel, so clicking a sprite here highlights its command, texture, and palette
everywhere.

### 🖥️ VDP Output & VDP1 Framebuffer — *the drawn result*

- **VDP Output** shows the reconstructed scene, with toggles for wireframe, bounding boxes,
  object numbers, and per-object highlighting.
- **VDP1 Framebuffer** shows the raw 512×256 buffer VDP1 actually drew into, with display
  modes for **Resolved** (final colors, using VDP2's sprite settings and palette), **Raw
  RGB555**, and **Priority** (a heatmap of per-pixel draw priority). *Note: the frame buffer
  is only available from a live software-rendered emulator, not from savestates.*

### 🎛️ Registers (VDP1 tab) — *the chip's control knobs*

Lists VDP1's hardware registers by name — `TVMR`, `FBCR`, `PTMR`, `EWDR`, `EWLR`, `EWRR`,
`ENDR`, `EDSR`, `LOPR`, `COPR`, `MODR` — with their values. Most of VDP1's *drawing* state
lives in the command table rather than these registers, so the command panels above are
usually where the action is.

---

## Part 3 — The drawing pipeline, end to end

This is the journey every VDP1 frame takes — and each stage maps to a tool that shows it:

1. **Command table** — the game builds its to-do list in VDP1 memory. → *VRAM Map, Command List*
2. **Walk** — VDP1 follows the linked list (jump / call / return) command by command. → *Command List*
3. **Build quads** — each command becomes a four-corner quad with a texture. → *Selected Object, VDP1 Table*
4. **Rasterize** — the texture is stretched across the quad, palette colors are looked up,
   Gouraud shading and draw modes are applied. → *Texture Viewer, Palette Viewer*
5. **Frame buffer** — the result is painted into one of the two 512×256 buffers. → *VDP1 Framebuffer*
6. **Hand off to VDP2** — the finished sprite layer goes to VDP2 for final compositing. → *see the VDP2 guide*

### A worked example: "how is this sprite drawn?"

1. Find it in the **3D View** or **VDP Output** and click it — that selects its command.
2. Read the **Selected Object** panel for its draw mode, corners, and flags.
3. Open the **Texture Viewer** to see its picture and **Export** it as a BMP.
4. Open the **Palette Viewer** to see (and note) the colors it uses.
5. Use **VRAM Map** to see exactly where its texture and command sit in memory — handy if
   you want to edit or relocate them.

---

## A few things to watch out for

- **VDP1 has no "screenshot" to read back.** The real chip doesn't keep an addressable
  finished image the way you might expect — so the tool *rebuilds* the frame from the
  commands. That's a feature: it's why every sprite is a separate, selectable object.
- **The frame buffer needs a live software renderer.** The raw 512×256 buffer view is only
  populated when you're connected to a live emulator using its software renderer. Savestates
  and hardware-accelerated renderers won't have it, and the panel will say so.
- **Sprite colors depend on VDP2.** A palettized texture is just index numbers; its real
  colors come from VDP2's Color RAM, and the frame buffer's final look also depends on VDP2
  sprite settings. If a sprite's colors look wrong, check the palette, not just the texture.
- **Memory is big-endian; the frame buffer isn't.** VDP1/VDP2 memory is stored big-endian,
  but the frame buffer is in the host's native order — which is why the Framebuffer panel
  has a byte-swap toggle.

---

## Glossary

- **VDP1** — the Saturn's drawing engine; draws sprites and polygons into a frame buffer.
- **VDP2** — the companion chip that draws backgrounds and composites the final picture.
- **Sprite** — on the Saturn, a textured quad. Not a fixed-size 2D tile.
- **Quad** — a four-cornered shape; VDP1's basic drawing primitive.
- **Distorted sprite** — a quad with its four corners freely placed; how the Saturn fakes 3D.
- **Command table** — the linked list of drawing commands VDP1 works through each frame.
- **Texture** — the picture stretched across a sprite, stored in VDP1 memory.
- **Color mode** — how a texture stores pixels: 16/64/128/256-color palettized, or direct RGB555.
- **Palette / CRAM** — the shared color table (Color RAM) that palettized pixels look up.
- **CLUT** — a small color lookup table in VDP1 memory used by "16 (LUT)" textures.
- **Gouraud shading** — smooth brightness blended across a quad from its four corners.
- **Draw mode** — how a sprite blends: Normal, Mesh, Shadow, Half Luminance, Half Transparent.
- **Frame buffer** — the off-screen 512×256 image VDP1 draws into; there are two, and they swap.
- **RGB555** — a direct-color format: 5 bits each for red, green, blue.
