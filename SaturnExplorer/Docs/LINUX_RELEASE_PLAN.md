# Linux release build: plan

Goal: a downloadable Linux archive built by the same release workflow as the macOS and Windows ones,
with the same guarantees: self-contained (no build-machine paths), smoke-tested from where it sits,
licences and the GPL source offer included, release tag baked in, Demo menu off, checksummed.

## Starting point (what already worked)

- The SDL2 + OpenGL frontend (`FrontEnd/Platforms/Web/`) builds natively on Linux: executable path
  (`/proc/self/exe`), bundled-Mednafen lookup, process launch/kill, file dialogs (zenity, kdialog),
  `SDL_OpenURL`, sound preview, `--selftest`, the update check through libcurl.
- CMake stages `licenses/<component>/` beside the Linux executable.
- `install.py` has a Unix Mednafen build (`build_mednafen_unix`) and knows the Linux SE binary path.

## Gaps

| # | Gap | Where |
|---|---|---|
| 1 | `install.py` refuses to run on Linux ("supports Windows and macOS") and has no Linux prerequisite step | `Integration/install.py` |
| 2 | `package_release.py` copies no SE program on Linux (`SHIPPED_PROGRAMS` holds only the `.exe`) | `Integration/package_release.py` |
| 3 | No shared-library bundling or relinking on Linux (macOS has `bundle_dylibs`, Windows copies DLLs) | same |
| 4 | No licence/source manifest for bundled Linux libraries (macOS: `brew_manifest`, Windows: `msys2_manifest`) | same |
| 5 | Linux smoke test only runs `mednafen -help`: no dependency-resolution check, no `--selftest` | same |
| 6 | `.zip` archive loses the executable bit on extraction | same |
| 7 | README text has only macOS and Windows variants | same |
| 8 | The release workflow has no Linux job, the draft release no Linux asset | `.github/workflows/saturn-explorer-release.yml` |
| 9 | The verification matrix has no Linux runner: the Linux frontend, `se-render` and the non-decompiler suites are not built or run on Linux (the decompiler workflow covers only the decompiler suites, under sanitizers, and the Area 9 store tests) | `.github/workflows/saturn-explorer-verify.yml` |
| 10 | Bundling libcurl is not portable: distributions build it against different TLS libraries and CA paths, and Debian's symbols are versioned (`CURL_OPENSSL_4`) | `CMakeLists.txt`, `WebPlatform.cpp` |
| 11 | No window icon on Linux (Windows uses `.rc`, macOS `.icns`) | frontend |
| 12 | Build, install and distribution docs say nothing about a Linux release | `Docs/`, `Integration/` |

## Decisions

- **Format: `SaturnExplorer-<ref>-linux.tar.gz`, x86_64, built on `ubuntu-22.04`.** A tarball keeps
  permissions and symlinks. Building on the oldest supported runner sets the glibc baseline (2.35),
  so it runs on that and any newer distribution. AppImage is deferred: it would not hold `mednafen-se/`
  beside the program, where the user has to add the BIOS (the same reason the macOS app keeps it outside the bundle).
- **Bundle everything except the base system, like the macOS bundle.** Every shared library the SE
  program and Mednafen need, followed through `DT_NEEDED`, is copied into a `lib/` beside each one, and
  `patchelf` sets the runpath (`$ORIGIN/lib` on the executables, `$ORIGIN` on the libraries). The
  libraries that must come from the user's system (glibc, the C++ runtime, OpenGL/EGL and the GPU driver
  stack, X11/xcb/Wayland, ALSA/PulseAudio/PipeWire/JACK, D-Bus/systemd, fontconfig/freetype, zlib) are
  an exclusion list modelled on AppImage's excludelist. A dependency that does not resolve is an error.
- **libcurl is loaded at run time on Linux (`dlopen("libcurl.so.4")`), not linked.** The user's own
  libcurl knows the distribution's CA store and TLS library. Without one the update check reports
  "unsupported", which was already the no-curl behaviour. macOS keeps linking the system libcurl.
- **Licences and sources of bundled libraries come from dpkg/apt**: the owning package, its source
  package and version, the licence named in its Debian copyright file, and the source files with their
  SHA-256 from `apt-get source --print-uris` (the CI job enables deb-src). Copyleft sources are
  downloaded into `third-party-source/` as on the other platforms.
- **Native menu bar: not done.** SDL2 has no OS menu-bar API; Linux keeps the ImGui toolbar
  (already documented in CMakeLists.txt). File dialogs need zenity or kdialog; the README says so.

## Work items

1. `install.py`: run on Linux; a prerequisite step that checks the compiler, git, CMake, pkg-config,
   autotools, SDL2/FLAC (and libcurl headers) and, with consent, installs what is missing through
   apt, dnf, pacman or zypper.
2. `CMakeLists.txt` + `WebPlatform.cpp`: `SE_CURL_DLOPEN` on Linux (headers only, `dlopen` at run time).
3. Frontend: set the window icon at run time on Linux from an embedded PNG.
4. `package_release.py` on Linux: ship `SaturnExplorerFrontEnd`; bundle and relink libraries for SE and
   Mednafen; dpkg manifest; smoke test (every `DT_NEEDED` resolves inside the package or to an excluded
   system library, `--selftest`, `mednafen -help`) in a bare environment; `.tar.gz`; Linux README text.
5. `Integration/tests/check_package_release.py`: offline checks of the exclusion rule, the
   `apt-get --print-uris` / dpkg parsing and (Linux) that a bundled library is found through the runpath
   after the original is gone.
6. Release workflow: a `linux` job (deps, Mednafen, SE Release with `SE_DEMO_MENU=OFF`, the Demo-menu and
   tag checks, package, `SHA256SUMS-linux.txt`); the draft release waits for it and attaches it.
7. Verification workflow: add `ubuntu-latest` to the matrix with its dependency step, a test-count floor, and a
   check that the frontend starts and does not link libcurl. The decompiler suites stay in the sanitizer workflow.
8. Docs: `Docs/BUILD.md`, `Integration/INSTALL.md`, `Integration/DISTRIBUTION.md`, `install.sh` header,
   `package_release.py` docstring, the Codebase map (`DEPENDENCIES.md`, `Systems/Frontend.md`,
   `KNOWN_LIMITATIONS.md`).

## Status

All eight items are implemented. Checked locally on Ubuntu 24.04 (not yet on a GitHub runner):
`install.py --yes --no-se --mednafen-saturn-only` builds Mednafen; `package_release.py` produces the tarball,
bundling SDL2, libdecor, libsamplerate (both programs), FLAC and libogg (Mednafen); its smoke test passes;
FLAC's Debian source lands in `third-party-source/`; `check_package_release.py` passes; the window icon is set on X11.
Not yet tried: the workflows on GitHub, and the tarball on another distribution (Fedora, Arch, an older Ubuntu).

## Done when

- `python3 Integration/package_release.py --se-app build-release/bin --mednafen _emu/mednafen/src/mednafen
  --ref <ref> --out dist` produces a `.tar.gz` whose smoke test passes on a clean `ubuntu-22.04` runner.
- The release workflow attaches `SaturnExplorer-<tag>-linux.tar.gz` and `SHA256SUMS-linux.txt` to the draft.
- The verification workflow builds and runs the suite on Linux.

## Not covered (later)

- Code signing (as on Windows, none). AppImage/Flatpak packaging. aarch64 Linux builds.
- A native menu bar on Linux.
