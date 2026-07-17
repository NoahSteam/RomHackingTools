# Building Saturn Explorer

Saturn Explorer is split into three pieces:

| Piece | What it is | Portable? |
|---|---|---|
| **SaturnExplorerCore** | the analysis engine (static library) | yes — Windows, macOS, Linux |
| **SaturnExplorerSavestateDriver** | reads `.yss` / Mednafen savestates & dumps (static library) | yes |
| **SaturnExplorerFrontEnd** | the app window (Dear ImGui) | yes — three backends |

The Core and driver are pure C++14 and build everywhere. The FrontEnd has three
interchangeable Seam C backends, selected automatically by the toolchain:

| Backend | Stack | Selected when |
|---|---|---|
| **Windows** | Win32 + Direct3D 11 | building on Windows |
| **Web** | SDL2 + WebGL2/GLES3 (Emscripten) | configuring with `emcmake cmake` |
| **Desktop** | SDL2 + OpenGL | non-Windows and system SDL2 + OpenGL are found |

You can build with **CMake** (recommended — works everywhere, can *generate* a Visual
Studio solution, and drives the web build) or by opening the checked-in Visual Studio
solution directly.

---

## Option 1 — CMake (recommended)

[CMake](https://cmake.org/download/) reads one set of build scripts and produces the native project files for whatever toolchain you have — a Visual Studio solution, Makefiles, Ninja, Xcode, etc. Install CMake (and, on Windows, Visual Studio with the "Desktop development with C++" workload) first.

From the `SaturnExplorer` directory:

### Windows — generate and build a Visual Studio solution
```
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Debug
```
This *creates* `build/SaturnExplorer.sln`. You can keep using the command line, or just open that `.sln` in Visual Studio and press **F5** (the FrontEnd app is already set as the startup project). Use `"Visual Studio 16 2019"` if you're on VS 2019.

### Windows / macOS / Linux — build directly (no IDE)
```
cmake -B build
cmake --build build
```
CMake picks a default generator (Ninja or Makefiles). On non-Windows platforms the
**Core** and **savestate driver** libraries always build; the **FrontEnd** app builds too
when SDL2 and OpenGL development packages are present (e.g. `apt install libsdl2-dev
libgl1-mesa-dev`), otherwise it is skipped with a message. This native SDL2 build runs the
same code as the web app, so it's a convenient way to try the frontend without a browser.
It also accepts a savestate path on the command line: `./build/bin/SaturnExplorerFrontEnd
state.yss`.

Outputs land in `build/bin` (the app) and `build/lib` (the libraries).

### Web (browser) — Emscripten + WebGL2
Install the [Emscripten SDK](https://emscripten.org/docs/getting_started/downloads.html),
then from the `SaturnExplorer` directory:
```
emcmake cmake -B build-web -DCMAKE_BUILD_TYPE=Release
cmake --build build-web
```
This produces `build-web/bin/index.{html,js,wasm}`. Serve that directory over HTTP (a
`file://` open won't load the `.wasm`), e.g. `python3 -m http.server -d build-web/bin`,
and open it in a browser. Load a savestate by **dragging it onto the canvas** or via the
toolbar's **Open** button. Every push to `master` also builds and publishes this to GitHub
Pages via `.github/workflows/web.yml`.

### Live mode — inspect a running emulator (native builds)
The Windows and native-SDL2 builds can read a **running** Yabause in realtime
instead of a savestate. Apply the small patch in
[`Integration/Yabause/`](Integration/Yabause/README.md) to your Yabause build, run
a game, then launch Saturn Explorer with `--live` (or use **Open ▸ Connect to
Yabause**). Every panel then tracks the live game. (The web build is savestate-only
— live mode needs sockets/threads.)

---

## Option 2 — the checked-in Visual Studio solution

If you just want to open it in Visual Studio without CMake, double-click **`../RomHackingTools.sln`** at the repository root (it includes the three Saturn Explorer projects), set **FrontEnd** as the startup project, choose **Debug / x64**, and press **F5**.

---

## Using the app

1. Launch the FrontEnd.
2. **File → Open Savestate (.yss)…** and choose a Yabause / Yaba Sanshiro / Kronos `.yss` or a Mednafen / Beetle Saturn savestate.
3. The panels (VDP Output, 3D View, Command List, Texture / Palette / VRAM, References) populate from the loaded state.

---

## Troubleshooting

- **CMake can't find a compiler / Visual Studio** — install Visual Studio with the "Desktop development with C++" workload, then re-run the `cmake -B build …` line.
- **A build error in the FrontEnd** — the Core and driver are verified continuously, but the Win32/D3D11 app is Windows-specific; if a file reference or linker setting is off, copy the exact error text and send it along and it can be fixed quickly.
