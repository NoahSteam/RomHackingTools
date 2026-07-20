# One-command installer (Windows)

`install.bat` (→ `Integration/install.py`) sets up Saturn Explorer **and** a patched
Saturn emulator in one go: it builds Saturn Explorer, downloads a *pristine* upstream
emulator at a pinned revision, patches it with the matching `apply.py`, and builds it.

It's an **orchestrator with assisted install** — it detects the tools it needs and,
*only with your consent*, installs missing ones via `winget` / MSYS2 `pacman`. Nothing
is installed or built until you approve, and `--dry-run` prints the whole plan while
touching nothing.

## Quick start
```bat
install.bat                       :: Saturn Explorer + Mednafen (default)
install.bat --with-yabause        :: also build Yabause (Qt)
install.bat --dry-run             :: show exactly what it would do; change nothing
install.bat --yes                 :: don't prompt before each install/build
```

## What it builds
| Target | Toolchain | Notes |
|--------|-----------|-------|
| Saturn Explorer | CMake + MSVC (Release) | this repo |
| Mednafen | MSYS2 / MinGW autotools | `./configure --enable-debugger && make`; patched with `--with-pause` (pause/step) + breakpoints |
| Yabause / Yaba Sanshiro / Kronos | CMake + MSVC + Qt | optional; pick with `--yabause-variant` |

```bat
install.bat --with-yabause --yabause-variant=kronos --no-mednafen
```
`--yabause-variant` is one of `yabause` (default), `sanshiro`, `kronos`. The window-title
mark auto-detects the fork name (see `Yabause/README.md`).

## Prerequisites (auto-detected, assisted-installed)
The script checks for these and offers to `winget install` any that are missing:

- **git** (`Git.Git`)
- **CMake** (`Kitware.CMake`)
- **Visual Studio C++ toolset** (`Microsoft.VisualStudio.2022.BuildTools`, C++ workload) — for Saturn Explorer and Yabause
- **MSYS2** (`MSYS2.MSYS2`) — only if building Mednafen; the script then `pacman`-installs `gcc`, `SDL2`, `zlib`, and the autotools

**Qt** (for a Yabause build) is not auto-installed — install it (`winget`, the Qt online
installer, or `aqtinstall`) and pass `--qt-path "C:\Qt\6.x\msvc2022_64"`.

If `winget` isn't available, the script names each missing tool and stops so you can
install it and re-run.

## Two things no installer can do for you
- **Saturn BIOS + disc images.** A Saturn emulator needs a copyrighted BIOS. It is
  **not** bundled or downloaded — you supply your own, or the emulator won't boot.
- **Redistribute a patched Mednafen.** The installer downloads pristine Mednafen source
  and builds it *on your machine* on purpose: Mednafen's project discourages
  redistributing modified "Mednafen" binaries. This is the community-accepted path.
  (See `DISTRIBUTION.md`.)

## Pinning
The emulator revisions live in `EMULATORS` in `install.py` (default `master`). For a
reproducible build, pin a known-good commit — either edit that table or pass
`--mednafen-rev <sha>` / `--yabause-rev <sha>`. Re-running `apply.py --check` against
fresh upstream tells you if an anchor drifted (see `DISTRIBUTION.md`,
"Fork and pin your upstream").

## Flags
```
--prefix DIR           where to clone+build emulators (default: <repo>\_emu)
--no-mednafen          skip Mednafen
--with-yabause         also build a Yabause-lineage fork
--yabause-variant V    yabause | sanshiro | kronos   (default: yabause)
--mednafen-rev SHA     override the pinned Mednafen revision
--yabause-rev  SHA     override the pinned Yabause-fork revision
--se-only              build only Saturn Explorer
--msys2 DIR            use an existing MSYS2 install (e.g. C:\msys64)
--qt-path DIR          Qt dir for the Yabause build (CMAKE_PREFIX_PATH)
--generator G          CMake generator (default "Visual Studio 17 2022"; "...16 2019")
--dry-run              print the plan; change nothing
--yes                  don't prompt before installs/builds
```

## After it finishes
Launch the patched emulator with a game, then start Saturn Explorer with `--live`
(or **File → Connect to emulator (live)**). A tapped build announces itself in its
window title: `… (SaturnExplorer Enabled. <ver> / <emu> <rev>)`.
