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
update.bat                        :: re-run after editing Integration\; rebuilds only what changed
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

## Iterative re-install (rebuild only what changed)

After the first install, if you edit something under `Integration/` — `se_export.c`, the
Mednafen glue, `SeLiveProtocol.h`, `apply.py` — you don't need a full reinstall. Use
`update.bat` (or `install.bat --incremental`) to rebuild **only what changed**:

```bat
update.bat                        :: rebuild only what changed after an Integration\ edit
update.bat --mednafen-saturn-only :: ...and (re)configure Saturn-only
update.bat --with-yabause         :: also refresh the Yabause build
update.bat --dry-run              :: show the plan; change nothing
```

`update.bat` is exactly `install.py --incremental`, so every flag above still passes
through. Incremental mode:

- **Keeps the existing emulator checkout** — no `git clone` / `fetch` / `checkout` (that's
  network, and could reset your local edits). It just re-runs the patcher.
- **Skips the prerequisite package installs** (winget / MSYS2 `pacman -Syu`) — the slow,
  network-bound steps — assuming the toolchain is already there from the first run.
- **Skips `./configure`** when the tree is already configured (`config.status` present),
  going straight to `make` so only the objects whose sources changed are recompiled.
- Re-runs `apply.py`, which is **idempotent and content-aware**: a copied file
  (`se_export.c`/`.h`, `SeLiveProtocol.h`, the glue) is rewritten *only if its bytes
  changed*, so an unchanged source keeps its mtime and `make` skips it. Edit one file →
  exactly one object recompiles + a relink; edit nothing → `make` finds nothing to do.

Saturn Explorer itself (CMake) is already incremental every run — `cmake --build` only
rebuilds changed objects.

**When to do a full `install.bat` instead:** to move the emulator to a new pinned
revision (incremental deliberately doesn't touch git), or after a `--revert`. Changing
`--mednafen-saturn-only` on/off is handled automatically — passing it with `--incremental`
forces a reconfigure so the new `--disable-*` set takes effect. You never need
`make distclean` just to re-run.

## Prerequisites (auto-detected, assisted-installed)
The script checks for these and offers to `winget install` any that are missing:

- **git** (`Git.Git`)
- **CMake** (`Kitware.CMake`)
- **Visual Studio C++ toolset** (`Microsoft.VisualStudio.2022.BuildTools`, C++ workload) — for Saturn Explorer and Yabause
- **MSYS2** (`MSYS2.MSYS2`) — only if building Mednafen; the script then `pacman`-installs `gcc`, `SDL2`, `zlib`, `FLAC`, and the autotools

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

## Source: our forks (by default)
The installer clones **our own forks** of the emulators — under the `NoahSteam`
account by default — not the original upstreams. That's the "fork and pin" discipline
from `DISTRIBUTION.md`: the fork is a stable base you update deliberately, so an
upstream rename never breaks a build mid-flight.

| Emulator | Default source (fork) | Upstream (parent) |
|----------|-----------------------|-------------------|
| Mednafen | `NoahSteam/mednafen-git` | `libretro-mirrors/mednafen-git` |
| Yabause | `NoahSteam/yabause` | `Yabause/yabause` |
| Yaba Sanshiro | `NoahSteam/yaba-sanshiro` | `devmiyax/yabause` |
| Kronos | `NoahSteam/Kronos` | `FCare/Kronos` |

**These forks must exist and be public** under the owner. The installer clones
anonymously, so a private repo fails with `404 Not Found`. Create them once, then the
installer uses them every run. The Yabause-lineage upstreams are on GitHub — just use the **Fork**
button. Mednafen isn't hosted on GitHub by its author, so import it from the official
release source: see [`Mednafen/FORK_SETUP.md`](Mednafen/FORK_SETUP.md) for exact steps
(clean standard-Mednafen provenance, no third-party mirror in the lineage).

**If a fork doesn't exist yet**, the installer notices before cloning (a `git ls-remote`
preflight) and offers to build from **upstream** instead — automatically with `--yes`,
or with a `Build from upstream now? [y/N]` prompt otherwise. So you can run it before
creating any forks; it just falls back to upstream. To choose the source explicitly:
- `--fork-owner <name>` — use a different account/org that holds the forks.
- `--upstream` — clone the original upstream repos instead of the forks.
- `--mednafen-repo <url>` / `--yabause-repo <url>` — an explicit git URL (wins over both).

## Pinning
The pinned revisions live in `EMULATORS` in `install.py` (default `master`). For a
reproducible build, pin a known-good commit in your fork — either edit that table or
pass `--mednafen-rev <sha>` / `--yabause-rev <sha>`. Re-running `apply.py --check`
against fresh upstream tells you if an anchor drifted (see `DISTRIBUTION.md`,
"Fork and pin your upstream").

## Flags
```
--prefix DIR           where to clone+build emulators (default: <repo>\_emu)
--no-mednafen          skip Mednafen
--with-yabause         also build a Yabause-lineage fork
--yabause-variant V    yabause | sanshiro | kronos   (default: yabause)
--mednafen-rev SHA     override the pinned Mednafen revision
--yabause-rev  SHA     override the pinned Yabause-fork revision
--fork-owner NAME      GitHub owner holding our emulator forks (default: NoahSteam)
--upstream             clone the original upstream repos instead of our forks
--mednafen-repo URL    explicit Mednafen git URL (overrides fork/upstream)
--yabause-repo  URL    explicit Yabause-fork git URL (overrides fork/upstream)
--se-only              build only Saturn Explorer
--incremental          iterative rebuild: only what changed (alias: --update; see below)
--mednafen-saturn-only build only the Saturn core (--disable-* every other console; faster)
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

The installer records each built emulator's path in the viewer's per-user settings
(`%APPDATA%\SaturnExplorer\settings.ini`, or `~/.config/SaturnExplorer/settings.ini`),
so Saturn Explorer's toolbar **Launch** menu can start the patched build directly — the
viewer then auto-connects (live) once it's up. Open the menu (the **Launch ▾** button) to
pick the emulator and the ROM to boot (the ROM **Browse…** picker is filtered to Saturn
disc images — `.cue/.chd/.iso/.ccd/…`). **Launch Settings…** there lets you edit each
emulator's executable, launch arguments, BIOS, and working directory. Put `{rom}` where the
game path goes and `{bios}` for the BIOS — e.g. `"{rom}"` for Mednafen (which usually takes
its BIOS from its own `ss.bios_*` config, so leave `{bios}` out there), or
`-a -i "{rom}"` / `-a -b "{bios}" -i "{rom}"` for Yabause. A live preview shows the exact
command line. Selecting a ROM also adopts it as the Data Directory when none is set, so the
texture "Find in game data" search scans the same disc. That same settings file persists
your emulator/ROM choice, recent ROMs, panel layout, and visibility between runs.
