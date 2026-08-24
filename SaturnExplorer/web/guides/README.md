# Hardware guides — canonical source

These self-contained HTML files **are the source of truth** for the Saturn hardware field
guides (audio, VDP1, VDP2). Edit the content here.

- `index.html` — landing page linking the three guides and the feature tour.
- `audio.html`, `vdp1.html`, `vdp2.html` — the hardware guides (inline CSS/JS, no external
  requests, theme-aware via `prefers-color-scheme`).
- `features.html` — a tour of the Saturn Explorer app itself: every panel, tool, and workflow.

The top-level `../../AUDIO_GUIDE.md`, `../../VDP1_GUIDE.md`, and `../../VDP2_GUIDE.md` are
thin pointer stubs, not copies — so there is nothing to keep in sync.

## Publishing

The GitHub Pages workflow (`.github/workflows/web.yml`) copies this folder into the deployed
site under `/guides/` on every push to `master` that touches `SaturnExplorer/**`. Live at:

- https://noahsteam.github.io/RomHackingTools/guides/

No build step — just edit an HTML file and push.
