# Hardware guides — canonical source

These self-contained HTML files **are the source of truth** for the Saturn hardware field
guides (audio, VDP1, VDP2). Edit the content here.

- `index.html` — landing page linking the three guides and the feature tour.
- `audio.html`, `vdp1.html`, `vdp2.html` — the hardware guides (inline CSS/JS, no external
  requests, theme-aware via `prefers-color-scheme`).
- `features.html` — a tour of the Saturn Explorer app itself: every panel, tool, and workflow.
- `profiler.html` — how the profiler works and how its numbers were verified.
- `profiler-reference.html` — the Profiler panel field by field, with annotated screenshots from
  `img/profiler/` (the only images the guides use; keep them cropped to the panel). Saturn Explorer's
  Help > Profiler Reference (web) opens it on the published site.

The `../../Docs/AUDIO_GUIDE.md`, `../../Docs/VDP1_GUIDE.md`, and `../../Docs/VDP2_GUIDE.md` are
thin pointer stubs, not copies — so there is nothing to keep in sync.

## Publishing

`.github/workflows/saturn-explorer-guides.yml` deploys this folder to GitHub Pages on every push to
`master` that touches it; `index.html` is the site root. Each page is also self-contained: open it in a browser.

No build step — just edit an HTML file and push.
