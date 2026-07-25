# Rendering Accuracy Roadmap

## Goal

Make the **VDP Output** view reproduce the Saturn's displayed frame while keeping
Saturn Explorer's debugger abilities: hide individual layers, select VDP1 commands,
inspect source textures, and explode command geometry in the 3D view.

Two renderers, treated as related but **separate products**:

- **Final-frame renderer** — what the Saturn actually displays. Its most accurate
  input is the emulator's *displayed VDP1 framebuffer* plus the VDP2 register/VRAM
  state, mixed per pixel.
- **Command-inspection renderer** — re-rasterizes VDP1 commands so individual sprites
  stay selectable, attributable to a texture, and explodable in 3D. Useful even when a
  drawn framebuffer exists, and the only option for raw-VRAM sources.

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
   color_offset_sel, line_color_enable, shadow_flags}`. Use bounded **per-scanline**
   buffers, not full-frame metadata.
2. **Sprite descriptors from the drawn VDP1 framebuffer.** Decode `Vdp1Fb()` per pixel
   via VDP2 **SPCTL** sprite-type tables → color, priority number (→ PRISA–D), cc ratio
   (→ CCRSA–D), shadow bit, sprite-window bit. Fall back to the command rasterizer when
   no framebuffer is present (raw-VRAM sources).
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

Independent of the mixer; immediately visible; low risk. Ship alongside Track E.

- **A1. Untextured polygon (cmd 3), polyline (4), line (5).** Solid / Gouraud color, no
  texture fetch. (NiGHTS and many titles use these.)
- **A2. Clipping.** User clip (cmd 6) + CMDPMOD user-clip enable (inside/outside) +
  pre-clip disable; apply to the inspection rasterizer.
- **A3. Draw-mode decode.** Read the CMDPMOD color-calc field into `se_draw_mode`
  (currently hardcoded `NORMAL`); apply **mesh** (stipple) and **half-luminance** in the
  rasterizer; honor **end codes**.
- **A4. Sprite-vs-sprite blends.** **Half-transparency** and **shadow** against the
  rasterizer's own target buffer; all Gouraud + half-lum/half-trans combinations.
- **A5. Exact distorted-sprite stepping.** Replace the two-affine-triangle + half-pixel
  expansion with hardware edge stepping / texture sampling.

*Acceptance:* command-isolation output matches the corresponding region of the captured
VDP1 framebuffer and stays selectable in 2D/3D.

### Track B — Descriptor mixer (the re-architecture)

- **B1. Descriptor emission.** Convert `Vdp2Compositor` + back screen to write per-pixel
  descriptors into a scanline buffer instead of RGBA. Behind a flag; band path stays for
  A/B diffing until parity.
- **B2. Mixer core.** Per-pixel priority sort + tie rules; move the existing color-calc
  and color-offset math into mixer stages (removes the band-model approximation).
- **B3. Sprite framebuffer decode.** SPCTL sprite-type → per-pixel sprite descriptors
  from `Vdp1Fb()`; command-rasterizer fallback. Feed the mixer.

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
