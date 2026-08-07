# Saturn VDP2 — a plain-English guide

This guide explains **what the Saturn's VDP2 chip does** and **how Saturn Explorer's VDP2
tools let you inspect the backgrounds, palettes, and layering it produces**. No prior
hardware knowledge is assumed — every term is defined the first time it appears.

The Saturn has **two** video chips working as a team. This guide is about **VDP2, the
background-and-compositing chip**. Its teammate **VDP1** draws the moving sprites and
polygons (see `VDP1_GUIDE.md`); VDP2 draws the scrolling scenery *and* stacks everything —
backgrounds plus VDP1's sprites — into the final picture on your TV.

If you remember one thing: **VDP2 is a stack of layers, merged by priority.**

---

## Part 1 — What VDP2 does

### The compositor

Where VDP1 *draws* things, VDP2 *arranges* them. Every frame, VDP2 produces several
scrolling **background layers**, takes the **sprite layer** that VDP1 drew, and merges
them all — deciding, for every single pixel, which layer shows in front and whether layers
blend together. The result is what actually reaches the screen.

Think of it as a stack of transparent sheets:

- At the very bottom, a plain **back screen** (a backdrop color).
- Above it, up to **five background layers**.
- The **sprite layer** from VDP1 mixed in by priority.
- **Color math** where sheets are semi-transparent and blend.

### The five backgrounds

VDP2 can show up to five backgrounds at once. Four are ordinary flat scrolling layers; one
can rotate and scale like a 3D floor or ceiling:

| Layer | Nickname | What it's for |
|-------|----------|---------------|
| **NBG0** | Scroll A | A normal scrolling background |
| **NBG1** | Scroll B | A normal scrolling background |
| **NBG2** | Scroll C | A normal scrolling background |
| **NBG3** | Scroll D | A normal scrolling background |
| **RBG0** | Rotation | A background that can rotate, scale, and tilt (e.g. Mode-7-style floors) |

("NBG" = Normal Background; "RBG" = Rotation Background.) Not every game uses all five —
the more color and detail a layer has, the more memory bandwidth it costs, so games trade
layers against richness.

### How a background is built: plane → page → pattern → cell

A background isn't stored as one giant picture — that would be far too much memory. Instead
it's assembled from small reusable tiles, in a four-level hierarchy:

- A **cell** is an 8×8-pixel tile — the smallest picture piece.
- A **pattern** is one or more cells plus which palette and flip to use (a "character").
- A **page** is a grid of patterns.
- A **plane** is a grid of pages — the full scrollable map.

So a long scrolling level is really a small set of tiles arranged and re-arranged by a map.
This is the classic tile-based approach, and it's why you can change one tile and see it
update everywhere it's used. (A background can also be a **bitmap** — one flat image —
when a game wants that instead of tiles.)

### Scrolling, zooming, and line effects

Backgrounds move by changing their **scroll** position. VDP2 adds richer tricks on the two
main layers (NBG0/NBG1):

- **Fractional scroll and zoom** — scroll and scale by sub-pixel amounts for smooth motion.
- **Line scroll** — give each horizontal scanline its own scroll offset, which is how games
  make water ripple, floors recede, or heat shimmer.

### The palette: Color RAM, shared with VDP1

VDP2 owns the console's shared color table, **Color RAM (CRAM)** — up to 2048 colors. Both
VDP2's tile backgrounds *and* VDP1's palettized sprites look their colors up here, so CRAM
is the one place the whole console's palette lives. It comes in a few modes (set by a
register called RAMCTL): **RGB555** with 1024 or 2048 colors, or higher-precision
**RGB888** with 1024 colors.

### Priority: who's in front

Every layer carries a **priority number**. When two layers overlap, the higher priority
wins that pixel. A few rules matter:

- **Priority 0 means "not displayed"** — it's how a layer is switched off.
- The back screen sits at the bottom (priority 0 backdrop).
- On a **tie**, VDP1's **sprite** sits in front of a same-priority background — matching
  the real hardware, so characters stay on top of scenery they share a priority with.

### Color math (translucency)

VDP2 can make a layer semi-transparent and **blend** it with the layer immediately beneath
it — this is **color calculation**. There are two flavors: a **ratio** blend (mix the two
by a set percentage, for glass, water, ghosts) and an **additive** blend (add the colors,
for glows and light). Blending only ever combines the top pixel with the one right below
it.

### Extra tricks

- **Back screen** — the always-present backdrop, either one flat color or a different color
  per scanline (for sky gradients).
- **Color offset** — nudge a layer's red/green/blue up or down (fades, tints, day-to-night).
- **Mosaic** — deliberately chunk a layer into big blocks (the classic pixelated
  censor/transition effect).
- **Windows** — mask a layer to a rectangle or region, so an effect only shows inside (or
  outside) a shape.
- **Rotation (RBG0)** — the rotation background uses rotation-parameter and coefficient
  tables to spin, scale, and tilt into the distance.

---

## Part 2 — How Saturn Explorer sees VDP2

The tool contains a **software VDP2 compositor** that rebuilds the background layers and
merges them with VDP1's sprites the way the hardware does — so you can toggle layers on and
off, inspect the palette, and read the register state that drives it all.

### 🧱 VDP2 Table — *the five backgrounds at a glance*

A decoded summary table with one row per background (NBG0–3), showing whether it's **On**,
its **color** depth (16 / 256 / 2048 / 32K), its **priority**, and its **scroll (X, Y)**
position. The fastest way to see which layers are active and how they're stacked.

### 🎚️ Layer toggles & VDP Output — *see each sheet in the stack*

The output view composites the full scene, and the **Layers** menu lets you switch NBG0–3,
RBG0, and the sprite layer on and off independently — plus toggles for color calculation
and windows. Turning layers off one at a time is the quickest way to figure out *which
layer a given element belongs to*.

### 🎨 Color RAM — *the whole console palette*

A grid of every color in Color RAM (up to 2048 swatches), with the current mode shown
(RGB555 or RGB888). Hover any swatch for its index, raw value, and decoded RGB. Because
CRAM is shared, this is the palette behind **both** backgrounds and sprites.

### 🖌️ Palette Viewer — *the colors a specific object uses*

Focused on the selected VDP1 sprite, it shows the exact CRAM bank (or LUT) that object
draws from — the bridge between "this sprite" and "these colors in the shared palette."

### 🎛️ Registers (VDP2 tab) — *the control panel*

A curated list of VDP2's registers by name and value — resolution (`TVMD`), memory control
(`RAMCTL`), which backgrounds are on (`BGON`), tile formats (`CHCTLA`/`CHCTLB`), priorities
(`PRINA`/`PRINB`/`PRIR`), scroll positions, color-calculation control (`CCCTL`), window
settings, sprite control (`SPCTL`), and more. This is the ground truth for how a frame is
configured.

---

## Part 3 — Reading a scene, end to end

The layers and the tools line up stage by stage:

1. **Back screen** seeds the frame with a backdrop. → *Registers (BKTA), VDP Output*
2. **Backgrounds** NBG0–3 and RBG0 are built from their tile maps (or bitmaps) and scrolled.
   → *VDP2 Table, Layer toggles*
3. **Palette** colors are looked up in Color RAM. → *Color RAM, Palette Viewer*
4. **Priority** decides, per pixel, which layer is in front. → *VDP2 Table (priority column)*
5. **Sprites** from VDP1 are merged in, winning priority ties. → *see the VDP1 guide*
6. **Color math** blends any semi-transparent top layers with the one below. → *Color Calc toggle*

### A worked example: "which layer is this, and how is it colored?"

1. Open **VDP Output** and use the **Layers** menu to toggle backgrounds off one by one
   until the element you care about disappears — now you know which NBG/RBG it is.
2. Check the **VDP2 Table** row for that layer: its color depth, priority, and scroll.
3. Open **Color RAM** to see the palette it's drawing from; hover swatches to read exact
   values.
4. Read the **Registers (VDP2 tab)** to see the exact settings — useful if a background is
   missing (often a priority of 0 or an off bit in `BGON`).

---

## A few things to watch out for

- **Priority 0 = invisible.** If a background you expect is missing, the usual cause is a
  priority of 0 or its enable bit being off in `BGON` — check the VDP2 Table and Registers
  before assuming anything's broken.
- **The palette is shared.** Editing a Color RAM entry changes it for *every* background and
  sprite that uses that index — powerful for recoloring, but watch for side effects.
- **Backgrounds are tiles, not one image.** Change a cell and it updates everywhere that
  tile is used across the map. That's a feature, not a glitch.
- **Some advanced effects are approximated.** The tool's compositor models the common,
  high-impact features (scrolling, zoom, line scroll, rotation, color math, color offset,
  horizontal mosaic, windows, the back screen). A few less-common ones are **not yet
  modeled** — VDP2 shadow/highlight, the line-color screen, vertical cell-scroll, vertical
  mosaic, and true per-pixel sprite priority — so a scene leaning heavily on those may look
  slightly different from real hardware. (Some toggles for these exist in the UI ahead of
  the underlying feature.)

---

## Glossary

- **VDP2** — the Saturn's background and compositing chip; draws scenery and stacks all layers.
- **VDP1** — the companion chip that draws sprites and polygons into a frame buffer.
- **NBG0–NBG3** — the four normal (flat) scrolling backgrounds; also called Scroll A–D.
- **RBG0** — the rotation background; can spin, scale, and tilt.
- **Layer / plane** — one background sheet in the stack.
- **Cell** — an 8×8-pixel tile, the smallest piece of a tiled background.
- **Pattern (character)** — one or more cells plus palette/flip info.
- **Page / plane** — grids that arrange patterns into a full scrollable map.
- **Bitmap background** — a background stored as one flat image instead of tiles.
- **Color RAM (CRAM)** — the shared color palette (up to 2048 colors) used by VDP2 and VDP1.
- **Priority** — the number that decides which layer shows in front; 0 means hidden.
- **Color calculation** — VDP2's translucency: blending the top layer with the one below.
- **Back screen** — the backdrop color behind everything, at the bottom of the stack.
- **Color offset** — shifting a layer's red/green/blue for fades and tints.
- **Line scroll** — giving each scanline its own scroll offset, for ripple and recede effects.
- **Mosaic** — chunking a layer into big blocks (the pixelated-censor effect).
- **Window** — masking a layer to a region so an effect only shows inside or outside a shape.
