# RomHackingTools

A collection of tools for reverse-engineering and translating Sega Saturn games —
including project-specific tooling (Sakura Taisen, Dragon Force, Slayers Royal, …) and
general-purpose utilities.

## Saturn Explorer

The flagship tool: a visual reverse-engineering and debugger for the Sega Saturn. Point it
at a savestate, a memory dump, or a *running* emulator and it reconstructs what the console
is drawing — VDP1 sprites, VDP2 backgrounds, VRAM, palettes, the command table — and lets
you step through the SH-2 code behind it. See [`SaturnExplorer/`](SaturnExplorer/) and its
[ReadMe](SaturnExplorer/ReadMe.txt).

**Try it in your browser:** https://noahsteam.github.io/RomHackingTools/

### Hardware field guides

Plain-English guides to the Saturn's audio and video hardware and how Saturn Explorer's
tools map onto it — no prior hardware knowledge assumed:

- **[Saturn Audio](https://noahsteam.github.io/RomHackingTools/guides/audio.html)** — the
  SCSP sound chip, sound RAM, streaming vs. CD audio, tracing a sound to its file on disc.
- **[VDP1](https://noahsteam.github.io/RomHackingTools/guides/vdp1.html)** — the drawing
  engine: sprites, quads, textures, palettes, the command table, the 3D view.
- **[VDP2](https://noahsteam.github.io/RomHackingTools/guides/vdp2.html)** — the layer
  engine: scrolling backgrounds, the shared palette, priority, and color math.

  → [All guides](https://noahsteam.github.io/RomHackingTools/guides/)

The guide pages are self-contained HTML in
[`SaturnExplorer/web/guides/`](SaturnExplorer/web/guides/) and deploy to GitHub Pages
automatically on every push to `master`.
