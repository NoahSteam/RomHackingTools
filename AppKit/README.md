# AppKit — a cross-platform Dear ImGui app base

AppKit is the reusable, domain-agnostic scaffolding extracted from Saturn Explorer's
frontend. It gives you a Dear ImGui application that builds and runs on **three targets
from one codebase** — Windows (Win32 + Direct3D 11), desktop (SDL2 + OpenGL), and the web
(Emscripten + WebGL2) — behind a single small platform interface. You write your app's UI
(and, optionally, an embeddable engine); AppKit handles the windowing, GPU, event loop,
and file I/O per platform.

It carries **no domain logic** — no Saturn, no emulator specifics. That's the point: drop
your own panels into `src/App.cpp` and go.

## What's here

```
AppKit/
├── include/appkit/IPlatform.h   ← the one seam: window/loop/ImGui-frame/texture/file
├── src/App.{h,cpp}              ← YOUR app. Demo shows all 4 IPlatform capabilities.
├── Platforms/
│   ├── Windows/                 ← Win32 + D3D11 backend + WinMain
│   └── Web/                     ← SDL2 + GL backend (browser + native) + WebMain + shell.html
└── CMakeLists.txt               ← implicit WIN32 / EMSCRIPTEN / else(desktop-SDL) selection
```

The `Platforms/Web/` backend **dual-compiles**: under Emscripten it's the browser build;
built natively it's a desktop SDL2/OpenGL app — the browser-free way to run and verify.

## Build & run

**Desktop (SDL2 + OpenGL)** — needs `libsdl2-dev` + OpenGL:
```sh
cmake -S AppKit -B AppKit/build
cmake --build AppKit/build --parallel
./AppKit/build/AppKitApp [optional-file]
```

**Web (Emscripten + WebGL2)** — needs the emsdk on PATH:
```sh
emcmake cmake -S AppKit -B AppKit/build-web
cmake --build AppKit/build-web        # → build-web/index.html + .wasm
```
Serve `index.html` over HTTP (drag-drop a file onto the canvas, or File > Open).

**Windows (Win32 + D3D11)** — Visual Studio 2019/2022 with the C++ workload:
```bat
cmake -S AppKit -B AppKit\build -G "Visual Studio 17 2022" -A x64
cmake --build AppKit\build --config Release
```

## The one interface: `IPlatform`

Your app depends only on `ak::IPlatform` (+ ImGui). Each platform implements:
lifecycle (`Initialize`/`Shutdown`), the event pump (`PumpEvents`), the ImGui frame
(`BeginFrame`/`EndFrame`), a **GPU texture bridge** (`CreateTexture`/`UpdateTexture`/
`DestroyTexture`, for showing RGBA images with `ImGui::Image`), and **file I/O**
(`OpenFileDialog`/`SaveFile` — a native dialog on Windows, a browser picker/download on
the web). The entry point constructs one `App` + one `IPlatform` and runs
`PumpEvents → BeginFrame → App::BuildUI(platform) → EndFrame`.

## Spin up a new app

1. Rewrite `src/App.cpp` with your panels (the demo exercises the texture bridge, Open,
   and Save so you can see the seams working, then delete it).
2. Add your sources to `AK_APP_SRC` in `CMakeLists.txt` (one shared list, used by all
   three platform branches — don't duplicate it per branch).
3. Set your window title in each entry point / `PlatformConfig`.

## Going further: an embeddable engine

If your app has a processing core you'd want to embed elsewhere (as Saturn Explorer's
emulator-embeddable renderer does), keep it behind a **flat C ABI** the way that project
does: versioned POD structs, a capability bitmask, size-in/size-out buffer filling, no STL
or exceptions across the boundary. See `SaturnExplorer/ARCHITECTURE.md` for the full seam
model (Seam A data-source drivers, Seam B host/embed API) — AppKit is that project's Seam C.

## Provenance & notes

- Extracted from `SaturnExplorer/FrontEnd` (namespace `sfe` → `ak`, Saturn theme/panels
  removed). ImGui styling is stock `StyleColorsDark()` — swap in your own.
- For the in-repo starter, `CMakeLists.txt` reuses Saturn Explorer's vendored ImGui via
  `AK_IMGUI_DIR`. A standalone copy of AppKit should vendor its own ImGui (docking branch)
  or pull it with FetchContent; override with `-DAK_IMGUI_DIR=/path/to/imgui`.
