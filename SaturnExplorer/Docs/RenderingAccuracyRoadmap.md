# Rendering Accuracy Roadmap

## Goal

Make the VDP Output view reproduce the Saturn's displayed frame while retaining
Saturn Explorer's debugger-only abilities: hide individual layers, select VDP1
commands, inspect source textures, and explode command geometry in the 3D view.

The final-frame renderer and the command-inspection renderer should be treated as
related but separate products. Re-rasterizing VDP1 commands is useful for inspection,
but the emulator's displayed VDP1 framebuffer is the more accurate input to VDP2's
pixel mixer.

## Current baseline

### Implemented

- VDP1 command-list traversal, textured normal/scaled/distorted sprite geometry,
  palette/direct-color decoding, transparency, and Gouraud shading.
- VDP2 NBG0-NBG3 cell maps, 1/2-word pattern names, all cell color depths, scrolling,
  plane/page addressing, CRAM offsets, and coarse priority interleaving with VDP1.
- NBG normal and line windows W0/W1, including per-line tables and AND/OR logic.
- All four VDP2 plane-size encodings and BGON transparent-pixel-disable behavior.

### Architectural limits

- VDP2 layers currently write final RGBA pixels directly. Accurate priority, color
  calculation, shadows, and sprite mixing require preserving pixel metadata until a
  dedicated final mixer chooses and combines pixels.
- Live snapshots expose one end-of-frame VDP2 register image. Games may change
  registers during active display, so exact rendering requires per-scanline state.
- The final composite re-rasterizes VDP1 commands even though the live protocol already
  captures the displayed VDP1 framebuffer. That loses exact VDP1 draw-mode and
  framebuffer behavior.
- A savestate normally contains an instantaneous machine state, not the register
  history that produced the preceding displayed frame. Static-source rendering should
  therefore be described as a reconstruction unless the format includes line state.

## Recommended implementation sequence

### Phase 1: Build a pixel-diff validation harness

1. Extend the live protocol with the emulator's final displayed framebuffer and its
   native dimensions/field information.
2. Save synchronized reference bundles: final framebuffer, VDP1 framebuffer, VDP1/2
   VRAM, CRAM, registers, and per-line state.
3. Add headless golden-frame tests with exact mismatch counts and diff images.
4. Keep synthetic register tests for isolated features such as plane sizes, windows,
   transparency, and color calculation.

Acceptance: a regression command can compare SE with Mednafen without visual judgment.

### Phase 2: Capture display-time state correctly

1. Add a versioned per-scanline VDP2 state block to the Mednafen export: TVMD/display
   width, field/interlace state, BGON, scroll/zoom accumulators, window state, priority,
   color-calculation state, and rotation parameters used for that line.
2. Use TVMD and line-width state for output dimensions instead of deriving the frame
   size solely from VDP1's system-clip command.
3. Preserve a fallback frame-wide state path for old protocol versions and savestates,
   with an explicit "static reconstruction" indicator in the UI.

Acceptance: titles that change display mode or registers mid-frame no longer combine
the current VRAM with unrelated end-of-frame register values.

### Phase 3: Use the displayed VDP1 framebuffer for the final composite

1. Decode the captured displayed framebuffer according to TVMR/FBCR and VDP2 SPCTL.
2. Produce a per-pixel sprite descriptor containing color/palette data, priority
   number, color-calculation ratio, shadow flags, and sprite-window bit.
3. Feed those descriptors to the VDP2 mixer instead of assigning one maximum priority
   to an entire command.
4. Keep the existing command rasterizer for selection, texture attribution, overlays,
   and the exploded 3D view.

Acceptance: final-frame accuracy no longer depends on SE reproducing every VDP1 edge
and write rule, and sprite priority/window/color behavior can vary per pixel.

### Phase 4: Complete normal-background generation

1. NBG0/NBG1 bitmap modes and bitmap palette/address rules.
2. NBG0/NBG1 line scroll, vertical cell scroll, and coordinate increment/zoom.
3. Horizontal and vertical mosaic.
4. Back screen (BKTA, including per-line mode) and line-color screen (LCTA).
5. VRAM bank/cycle-pattern access restrictions and high-resolution fetch limits.
6. Special priority modes from SFPRMD and pattern/dot special-function bits.

Acceptance: every non-rotation NBG test in the Sega VDP2 feature matrix has a synthetic
case and at least one captured commercial-game case.

### Phase 5: Implement rotation backgrounds

1. RBG0 parameter A/B tables, RPMD selection, coefficient tables, and screen-over
   modes.
2. Rotation bitmap and cell-map paths, including all 16 map planes.
3. Rotation-parameter windows and coefficient switching.
4. RBG1/NBG0 sharing rules and VRAM bank allocation constraints.

Acceptance: representative floor/sky/road scenes align with the reference at native
resolution before color calculation is enabled.

### Phase 6: Replace RGBA overwrites with the VDP2 pixel mixer

Each source should emit a scanline of descriptors rather than immediately overwriting
RGBA. A descriptor needs at least color, transparent state, source layer, priority,
color-calculation enable/ratio, color-offset selection, line-color enable, and shadow
flags.

Implement in this order:

1. Exact priority and tie rules across NBG/RBG/VDP1.
2. Sprite windows and the color-calculation window.
3. Color offsets A/B.
4. Standard, additive, and extended color calculation with CCR ratios.
5. Special color calculation modes and special function codes.
6. Normal/transparent shadows, MSB shadow, line-color insertion, blur, and gradation.

Acceptance: disabling any debugger layer changes only that source; the remaining
sources still mix with hardware-correct priority and color rules.

### Phase 7: Improve the VDP1 command-inspection renderer

These items remain valuable even after the final frame uses the captured framebuffer:

1. Scaled-sprite zoom-point modes instead of treating every scaled sprite as A/C.
2. Untextured polygon, polyline, and line commands.
3. System/user clipping modes and pre-clipping disable.
4. Mesh, end-code control, half-luminance, half-transparency, shadow, and all Gouraud
   combinations.
5. Exact VDP1 distorted-sprite edge stepping/texture sampling instead of two generic
   affine triangles and the current half-pixel expansion workaround.
6. Erase/write windows, framebuffer rotation, 8/16-bit framebuffer modes, interlace,
   and double-buffer selection for offline command replay.

Acceptance: command-isolation output matches the corresponding region of the captured
VDP1 framebuffer and remains selectable in 2D/3D views.

### Phase 8: Performance and release gates

1. Use bounded scanline buffers for pixel descriptors instead of full-frame metadata
   arrays.
2. Cache decoded pattern names/cells with VRAM-generation invalidation.
3. Add representative scenes for NBG windows (NiGHTS), rotation backgrounds,
   high-resolution/interlace, bitmap NBGs, per-pixel sprite priority, and color
   calculation.
4. Require zero unexpected pixel differences for synthetic tests and track a decreasing
   mismatch budget for commercial-game captures.

## Priority summary

The shortest path to materially better output is: validation capture, per-scanline
state, displayed VDP1 framebuffer decoding, then a descriptor-based VDP2 mixer. Porting
more command rasterization before those pieces would improve isolated sprites but would
not solve the largest final-frame errors.
