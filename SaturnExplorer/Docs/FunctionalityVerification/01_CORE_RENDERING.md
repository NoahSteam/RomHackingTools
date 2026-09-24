# Core / Rendering Verification

## VDP2 — Verified for covered cases
`SaturnExplorerVdp2Tests` passed on Linux, macOS, and ASan/UBSan.

## 3D geometry — Verified for covered cases
`SaturnExplorerGeometry3DTests` passed on Linux, macOS, and sanitizers.

## VDP1 — Partial
Core builds and rendering fixtures execute, but there is no checked-in real-frame golden suite. Per-pixel sprite-priority accuracy from the code review remains unverified against real captures.

## Headless renderer — Build verified
`se-render` builds on Linux/macOS. No `.sedump` fixture exists in the repo, so golden-frame comparison is Blocked.

### Needed
Add real `.sedump` + reference images covering normal/scaled/distorted sprites, gouraud, mesh, NBG0-3, RBG0, windows, color calculation, high-res/interlace, and mixed sprite priorities.
