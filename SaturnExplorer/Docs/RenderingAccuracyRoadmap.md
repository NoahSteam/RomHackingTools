# Rendering Accuracy Roadmap

## Goal

Make the **VDP Output** view reproduce the Saturn's displayed frame while keeping
Saturn Explorer's debugger abilities: hide individual layers, select VDP1 commands,
inspect source textures, and explode command geometry in the 3D view.

**The frame is always constructed from the command list — never copied from the
emulator's output.** That is the whole point of the tool: because every pixel is built
from a VDP1 command and a VDP2 layer, the user can disable individual commands/layers,
draw bounding boxes, select a sprite, and attribute a pixel back to its source for
reverse engineering. A pre-drawn framebuffer is an opaque blob none of that works on.

The emulator's **displayed VDP1 framebuffer** (`Vdp1Fb()`) therefore has exactly one
role: a **validation reference** we diff our construction against (Track E). It is never
a compositing input.

Almost every remaining feature is blocked on one architectural change (see
**The central re-architecture**), so the plan is ordered around unblocking it.

## Current status

### Implemented

VDP1 (command-inspection path):
- Command traversal; textured **normal / scaled / distorted** sprites; palette and
  direct-color decode; transparent-pixel handling; **Gouraud** shading.
- Scaled-sprite **zoom-point** decode (all vertical/horizontal anchor modes), not just
  the two-corner case.

VDP2 (background compositor, priority-band model):
- NBG0–3 cell maps, 1/2-word pattern names, every cell color depth, plane/page
  addressing, CRAM offsets, plane sizes, BGON transparent-pixel-disable.
- NBG0/1 **bitmap** mode; **RBG0** bitmap too.
- NBG0/1 **fractional scroll, zoom, and per-line scroll/zoom** (SCRCTL/LSTA).
- **Horizontal mosaic**; per-screen **color offset** (CLOFEN/COxR/G/B).
- **Color calculation** (CCCTL/CCRN ratio + additive) — *band-model approximation*.
- Real **back screen** (BKTA single / per-line).
- **RBG0 rotation**: parameter set A/B, coefficient tables, screen-over repeat, 16-plane
  map + bitmap.
- NBG **normal and line windows** W0/W1 with per-line tables and AND/OR logic.
- Frame **resolution from TVMD** (HRES incl. hi-res doubling, VRES incl. interlace) for
  pure-VDP2 scenes; VDP1 system clip stays authoritative when present.

### Known approximations (correct output only in the common case)

- **Color calc / color offset run in the priority-band loop**, blending each layer
  against whatever was already drawn below rather than against a resolved per-pixel
  stack. Wrong when three+ layers interact or a sprite sits mid-stack. Superseded by the
  descriptor mixer (Track B).
- **Sprites occupy a single priority band** (PRISA low bits), not per-pixel priority.
- **RBG0 windows** are approximated with the NBG0 window byte instead of WCTLC.
- Static sources render **end-of-frame registers over instantaneous VRAM** — a
  reconstruction, not a scanline-accurate replay.

### Not yet implemented

- VDP1: untextured **polygon/polyline/line**; **user clipping**; end codes; draw-mode
  effects **mesh / half-luminance / half-transparency / shadow**; exact distorted-sprite
  edge stepping.
- VDP2: **vertical cell scroll**, **vertical mosaic**, **shadow** (normal + MSB),
  **special priority / special color calc** (SFPRMD/SFCCMD/SFSEL/SFCODE) and per-cell
  priority, **line color screen** (LNCLEN/LCTA), **RBG1** and **RPMD 2/3**, **extended
  color calculation** (3-layer/gradient), **sprite window**, **per-pixel sprite
  priority**, VRAM bank/cycle-pattern access limits.

## The central re-architecture

The band loop (`RenderFrame`: for p in 0..7, draw VDP2 layers at p, then VDP1 sprites at
p, overwriting RGBA) cannot express per-pixel priority or blend a pixel against a
resolved stack. Replace it with a **descriptor-based per-pixel mixer**:

1. **Descriptor buffer.** Every source (NBG0–3, RBG0/1, back, line color, sprite) emits,
   per pixel, a small descriptor: `{color, opaque, layer, priority, cc_enable, cc_ratio,
   color_offset_sel, line_color_enable, shadow_flags, command_index}`. Use bounded
   **per-scanline** buffers, not full-frame metadata. `command_index` (sprite layer only)
   keeps every pixel attributable to the VDP1 command that drew it, so selection,
   bounding boxes, and per-command toggling still work after mixing.
2. **Sprite descriptors from the command rasterizer.** As it rasterizes each command, the
   VDP1 renderer emits the per-pixel descriptor above: the color it constructed, and the
   VDP2 **SPCTL** sprite-type decode applied to that constructed pixel's color-data bits →
   priority number (→ PRISA–D), cc ratio (→ CCRSA–D), shadow, sprite-window bit. This is
   what yields per-pixel sprite priority *without* copying the emulator's framebuffer —
   the layer stays fully decomposable (disabled commands simply don't write). The drawn
   `Vdp1Fb()` is used only to validate this construction (Track E), never as input.
3. **The mixer.** Per pixel: sort sources by priority and hardware tie rules, then apply
   the color pipeline in hardware order — special-function selection → shadow test →
   color calculation (standard/additive/extended) → color offset → line-color insertion.

Once this exists, most "not yet implemented" VDP2 items become localized additions to
one mixer stage instead of special cases scattered across the band loop.

## Execution plan

Ordered by dependency and leverage. Each numbered item is independently shippable with
its own synthetic test(s), committed and fast-forwarded like the existing tier work.

### Track E — Validation harness (do first)

Everything above is currently verified only by synthetic unit tests; there is no
pixel-level check against a reference. This is the single highest-leverage investment.

- **E1. Golden-frame capture + diff.** Extend the Mednafen/live export with the
  emulator's final displayed framebuffer and its native dimensions/field. Save
  synchronized bundles (final FB, VDP1 FB, VDP1/2 VRAM, CRAM, registers). Add a headless
  command that renders a bundle and reports exact per-pixel mismatch counts + a diff
  image.
- **E2. Per-scanline VDP2 state (later, for mid-frame changes).** Versioned per-line
  block: TVMD/width, field, BGON, scroll/zoom accumulators, window, priority, color-calc,
  rotation params. Keep the frame-wide fallback with a "static reconstruction" UI badge.

*Acceptance:* a regression command compares SE to Mednafen with no visual judgment.

### Track A — Command-inspection rasterizer completeness

Independent of the mixer; immediately visible; low risk.

- **A1. [DONE]** Untextured polygon (cmd 3), polyline (4), line (5) — solid RGB555 fill /
  edges. Filled quads reuse the sprite path; lines use a clipped DDA plotter.
- **A2. [DONE]** User clip (cmd 6) + CMDPMOD user-clip enable/mode (inside/outside),
  applied per pixel to quads and line edges.
- **A3. [DONE]** CMDPMOD draw-mode decode into `se_draw_mode` + **mesh** (stipple) and
  **half-luminance** in the rasterizer.
- **A4. [DONE]** **Half-transparency** and **shadow** (incl. MSB) against the target
  buffer; Gouraud already handled. (Blends against the band-loop background — the mixer
  will make the VDP1/VDP2 two-stage blend exact.)
- **A5. [deferred refinement]** Exact distorted-sprite edge stepping to replace the
  two-affine-triangle + half-pixel expansion. Distorted sprites already render; this is
  sub-pixel accuracy, not a missing feature — revisit against the E1 golden diff.

*Acceptance:* command-isolation output matches the corresponding region of the captured
VDP1 framebuffer and stays selectable in 2D/3D.

### Track B — Descriptor mixer (the re-architecture)

**Model.** VDP2 color calculation only ever blends the top-priority pixel with the one
*immediately below* it, so the mixer does not need a full per-pixel sort — only the top
two contributions. Maintain, per pixel, a two-deep accumulator:

```
struct PixDesc {                 // one source's contribution at a pixel
    uint8_t r, g, b;             // palette/gouraud-resolved color
    uint8_t prio;               // 0 = no contribution
    uint8_t layer;              // NBG0..3=0..3, RBG0=4, sprite=5, back=6, line=7
    uint8_t ccEnable, ccRatio;  // color-calc enable + 5-bit ratio (from this layer)
    uint8_t ccAdd;              // additive blend
    uint8_t coSel;              // color-offset set (none / A / B) for this layer
    uint8_t shadow;             // this pixel is a shadow (halve the pixel below)
};
struct PixColumn { PixDesc top, second; };   // highest and 2nd-highest by prio
```

Each source computes its per-pixel `PixDesc` and calls `insert(col, d)`, which keeps the
two highest-priority entries. **Tie rule** (equal priority): fixed layer order matching
today's band loop — sprite over RBG0 over NBG0 > NBG1 > NBG2 > NBG3 > back. Back screen
and line color seed the column at the bottom.

**Mixer** per pixel, in hardware order: start from `top`; if `top.shadow`, halve
`second`; else if `top.ccEnable` and `second` exists, blend `top` over `second` by ratio
(or additive); apply `top`'s color offset; insert line color if enabled. Write RGBA.

Increment sequence (each gated by the existing synthetic test suite — the mixer must
reproduce current test output before extending it):

- **B1. Buffer + mixer skeleton.** Add `PixColumn` scanline buffer, `insert`, and the
  mixer. Convert `RenderBackScreen` to seed the column. No behavior change yet.
- **B2. NBG/RBG emission.** `RenderLayer`/`RenderRbg0` emit `PixDesc` (with the existing
  color-calc/offset/priority values) instead of blending RGBA; the mixer reproduces the
  current color-calc result. Retire the band loop for VDP2. Existing color-calc, offset,
  priority, window, mosaic, bitmap, rotation tests must stay green.
- **B3. Sprite emission.** The command rasterizer writes sprite `PixDesc` (constructed
  color + SPCTL sprite-type decode → priority, cc ratio, shadow, sprite-window bit +
  `command_index`) into the column, giving **per-pixel sprite priority**. Half-transparency
  becomes a real mixer blend against the resolved pixel below (retiring the A4
  band-buffer approximation). Stays fully command-constructed and selectable.

*Acceptance:* disabling any debugger layer changes only that source; the rest still mix
with hardware-correct priority and color.

### Track C — Cross-layer VDP2 features (unlocked by Track B)

- **C1. Per-pixel sprite priority** — falls out of B2+B3.
- **C2. Sprite window + color-calc window;** proper **RBG0 WCTLC** windows.
- **C3. Shadow** — normal/transparent shadow + MSB shadow (halve the pixel below).
- **C4. Line color screen** (LNCLEN/LCTA) insertion into the cc chain.
- **C5. Special priority (SFPRMD)** + special color calc (SFCCMD/SFSEL/SFCODE) +
  per-cell/dot priority bit.
- **C6. Extended color calculation** (3-layer / gradient, CCRTMD) + full ratio/additive
  modes.

### Track D — Remaining background features (interleave freely)

- **D1. Vertical cell scroll** (NBG0/1) + **vertical mosaic.**
- **D2. RBG1** (second rotation bg) + **RPMD 2/3** (per-dot-coefficient / per-window
  parameter selection) + rotation-parameter windows + coefficient switching.
- **D3. VRAM bank / cycle-pattern access restrictions** and hi-res fetch limits
  (accuracy edge; low visual impact).

### Track F — Performance & release gates

- Bounded scanline descriptor buffers (not full-frame metadata arrays).
- Cache decoded pattern names / cells with VRAM-generation invalidation.
- A standing scene suite: NBG windows (NiGHTS), rotation floors/skies, hi-res/interlace,
  bitmap NBGs, per-pixel sprite priority, color calculation. Zero unexpected diffs on
  synthetic tests; a decreasing mismatch budget on commercial-game captures.

## Recommended sequence

1. **E1** (golden-frame diff) — makes every later step verifiable instead of guessed.
2. **Track A** in parallel — visible wins (polygons, half-transparency), no re-arch.
3. **B1 → B2 → B3** — the descriptor mixer, with E1 confirming parity at each step.
4. **Track C** on top of B; **Track D** interleaved as convenient.
5. **E2 / Track F** when mid-frame accuracy and performance become the limiting factor.

Doing Track A before B improves isolated sprites but not the largest final-frame errors;
doing B without E1 means flying blind. E1 + B is the shortest path to materially correct
output.
