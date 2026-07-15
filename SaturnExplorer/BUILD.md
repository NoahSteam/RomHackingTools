# Building Saturn Explorer

Saturn Explorer is split into three pieces:

| Piece | What it is | Portable? |
|---|---|---|
| **SaturnExplorerCore** | the analysis engine (static library) | yes — Windows, macOS, Linux |
| **SaturnExplorerSavestateDriver** | reads `.yss` / Mednafen savestates & dumps (static library) | yes |
| **SaturnExplorerFrontEnd** | the app window (Dear ImGui + Win32 + Direct3D 11) | **Windows only** |

You can build two ways: with **CMake** (recommended — works everywhere and can *generate* a Visual Studio solution for you) or by opening the checked-in Visual Studio solution directly.

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
CMake picks a default generator (Ninja or Makefiles). On non-Windows platforms the **Core** and **savestate driver** libraries build; the **FrontEnd** app is skipped because it depends on Direct3D 11 (you'll see a message saying so).

Outputs land in `build/bin` (the app) and `build/lib` (the libraries).

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
