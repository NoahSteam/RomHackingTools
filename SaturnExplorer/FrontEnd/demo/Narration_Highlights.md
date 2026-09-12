# Narration — Highlights reel

Read over `Highlights.sedemo`. In **manual mode** (default), read a beat's line, then press
**F8** to advance to the next. The ⏱ time is that beat's `hold` (the auto-mode dwell and a
rough pacing guide) — in manual mode you set the pace, so don't rush to hit it.

> Prep: load a savestate on a scene with a clear foreground sprite. The `select` in the script
> is index 8 — change it in the `.sedemo`, or just click the object you want during the beat.

---

**1 · intro** ⏱7s — *VDP Output only*
> This is Saturn Explorer — a visual reverse-engineering tool for Sega Saturn games. It takes
> the raw contents of video memory and rebuilds, on screen, exactly what the console's VDP
> chips would draw — and lets you take it apart.

**2 · render** ⏱8s — *VDP Output*
> The frame you're looking at isn't a screenshot. The core is a software VDP1 — it walks the
> draw-command table out of VRAM and rasterizes every sprite itself, then composites the VDP2
> background layers underneath, the same way the hardware does.

**3 · pick-sprite** ⏱10s — *select a sprite → Selected Object*
> And because every object is reconstructed from its own command, every object on screen is
> selectable. Click one — here's its fully decoded Selected Object entry: its position, its
> four corners, color mode, priority, and the texture and palette it draws with.

**4 · explode-3d** ⏱10s — *3D View*
> Those same sprites can be pushed apart into 3D. Saturn Explorer separates them along depth by
> priority, then by draw order, so a flat frame explodes into layers you can orbit around — a
> great way to see how a scene is actually stacked up.
> *(Drag in the 3D View to orbit while you talk.)*

**5 · peel-layers** ⏱10s — *VDP Output; VDP1 sprites hidden*
> Layering is just a render option, so we can peel it apart live. Watch — hide the VDP1
> sprites, and the VDP2 scroll backgrounds underneath are laid bare. Bring the sprites back and
> the scene is whole again. No emulator cooperation needed; this works straight off a memory
> dump.

**6 · layers-back** ⏱7s — *sprites on; wireframe + bounding boxes on*
> Sprites back on. And the same panel exposes wireframe, bounding boxes, and object numbers —
> overlays for seeing the geometry instead of the picture.

**7 · assets** ⏱11s — *Texture Viewer + Palette Viewer + VRAM Map*
> Pick any sprite and you can trace it straight down to its data. The Texture Viewer decodes it
> onto a transparency checkerboard; the Palette Viewer shows the exact colors it indexes into;
> and the VRAM Map places its bytes on a proportional map of all of video memory — command
> table, textures, palettes, and free space, color-coded.

**8 · references** ⏱9s — *References + Selected Object*
> One more. Every texture and palette is reverse-indexed, so the References panel answers the
> question a reverse-engineer actually asks: what else in this frame uses this same art? One
> click and it lists them — and selecting one jumps you right to it.

**9 · outro** ⏱7s — *VDP Output*
> Rebuild the frame, take it apart, trace it to its bytes — all from a savestate, before you
> touch a debugger. That's the graphics half of Saturn Explorer. Next up: watching a game run.

---

Approx. auto-mode runtime: ~1:19 (manual narration usually runs longer — budget ~2–3 min).
