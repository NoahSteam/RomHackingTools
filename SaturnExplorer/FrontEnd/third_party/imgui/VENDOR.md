# Vendored: Dear ImGui

- **Upstream:** https://github.com/ocornut/imgui
- **Branch:** `docking` (the docking/multi-viewport branch — the frontend uses a
  docked panel layout, which is only available on this branch).
- **Version:** 1.92.9 WIP
- **License:** MIT — see `LICENSE.txt`.

## What's included

Core (portable): `imgui.{h,cpp}`, `imgui_draw.cpp`, `imgui_tables.cpp`,
`imgui_widgets.cpp`, `imgui_demo.cpp`, `imgui_internal.h`, `imconfig.h`,
`imstb_*.h`.

Backends (`backends/`), one pair per frontend backend (see BUILD.md):

| Files | Used by |
|---|---|
| `imgui_impl_win32.{h,cpp}`, `imgui_impl_dx11.{h,cpp}` | the Windows build (Win32 + Direct3D 11) |
| `imgui_impl_sdl2.{h,cpp}`, `imgui_impl_opengl3.{h,cpp}`, `imgui_impl_opengl3_loader.h` | the web build (SDL2 + WebGL2) and the desktop SDL2 + OpenGL build on macOS and Linux |

## Updating

Re-fetch the same file set from the `docking` branch. Keep the core and the
backends at the same revision. If we later need to pin for reproducibility,
switch to a specific commit hash instead of the branch head.
