# Licenses for a shipped Saturn Explorer + patched Mednafen build

`Integration/package_release.py` copies this folder into every downloadable archive (as `Licenses/` beside
the app on macOS; merged into the build's `licenses/` folder on Windows, which is where the About box points).
It covers what is *added* when SE and the patched Mednafen are packaged together. The licence texts for code
compiled into the SE binary itself stay where the build already finds them: `FrontEnd/third_party/*/LICENSE*`,
which CMake and the MSBuild solution copy next to the executable as `licenses/` (macOS: inside the app's
Resources). They are not duplicated here.

| Component | Where it appears | Licence | Files |
|---|---|---|---|
| Saturn Explorer | the app | GPL-3.0 | `saturn-explorer/GPL-3.0.txt` (the repo-root `LICENSE`) |
| Mednafen 1.32.1 (patched) | `mednafen` / `mednafen.exe` | GPL-2.0-or-later | `mednafen/GPL-2.0.txt` |
| Zstandard (in Mednafen) | statically linked | BSD-3 (or GPL-2.0) | `mednafen/zstd-*.txt` |
| Tremor (in Mednafen) | statically linked, Ogg/Vorbis CD audio | BSD-3 | `mednafen/tremor-COPYING.txt` |
| libmpcdec (in Mednafen) | statically linked, Musepack CD audio | BSD-3 | `mednafen/mpcdec-COPYING.txt` |
| trio (in Mednafen) | statically linked | permissive (ISC-style) | `mednafen/trio-NOTICE.txt` |
| miniLZO (in Mednafen) | statically linked | GPL-2.0-or-later | `mednafen/minilzo-NOTICE.txt` (text: `mednafen/GPL-2.0.txt`) |
| Blip_Buffer, Fir_Resampler (in Mednafen) | statically linked | LGPL-2.1-or-later | `lgpl/LGPL-2.1.txt` |
| SDL2 / sdl2-compat / SDL3 | bundled dylib or DLL (SE and Mednafen) | zlib | `sdl/*.txt` |
| libFLAC | bundled dylib or DLL (Mednafen) | BSD-3 | `flac/COPYING.Xiph.txt` |
| libogg | bundled DLL (Mednafen, Windows) | BSD-3 | `ogg/COPYING.txt` |
| zlib | bundled DLL (Mednafen, Windows) | zlib | `zlib/LICENSE` |
| libintl (GNU gettext runtime) | bundled dylib or DLL (Mednafen) | LGPL-2.1-or-later | `lgpl/LGPL-2.1.txt` |
| libiconv | bundled DLL (Mednafen, Windows) | LGPL-2.1-or-later | `lgpl/LGPL-2.1.txt` |
| winpthreads | bundled DLL (Mednafen, Windows) | MIT-style | `winpthreads/COPYING.txt` |
| Microsoft Visual C++ runtime | `vcruntime140*.dll`, `msvcp140*.dll` beside the exe (Windows, app-local) | Microsoft redistributable terms (Visual Studio licence) | not open source; `package_release.py` copies it, no text to ship |
| libgcc / libstdc++ | statically linked into `mednafen.exe` (Windows) | GPL-3.0 + GCC Runtime Library Exception 3.1 | `gcc-runtime/*.txt` (GPL text: `saturn-explorer/GPL-3.0.txt`) |

Which rows apply depends on the platform. The SDL, FLAC, gettext, libiconv, libogg, zlib and
winpthreads rows describe the libraries `Integration/install.py` copies next to
`mednafen.exe` on Windows and the Homebrew dylibs a macOS bundle would carry. Re-check the
list against the actual archive contents (`ldd` / `otool -L`) when a release is assembled.

## Bundling a patched Mednafen

The GPL allows redistributing a modified Mednafen binary with its source, and `Integration/DISTRIBUTION.md`
("Prebuilt binaries") sets the terms for doing so. Upstream's own `README` asks that forks use "branding that
clearly conveys the forkiness", so label the binary as a Saturn Explorer build and not an official Mednafen
release. An SE-only release, with no Mednafen, needs just the texts the build already stages; everything
here except `saturn-explorer/` is for the bundled-Mednafen case (SDL covers SE's own dylib in a macOS bundle too).

## Obligations to remember

- **Mednafen source.** Mednafen is GPL, so every download that contains the patched binary
  must offer the corresponding source: the pinned upstream revision plus the `Integration/`
  patch for that SE tag. Add a `SOURCE.txt` to the archive (upstream version, repo URL, tag),
  or attach the patched source tarball to the same release.
- **Bundled libraries' source.** `package_release.py` lists every library it copies, with the exact version, licence
  and source URL read from the actual binaries (Homebrew's records on macOS; MSYS2 package data on Windows, which
  is not yet exercised on a runner), in `SOURCE.txt`. It also downloads the copyleft ones' source (libintl and
  libFLAC on macOS) into `third-party-source/`, checked against Homebrew's checksum; on Windows the source comes from
  MSYS2's source archives and only its hash is recorded, since MSYS2 publishes none. GPL-with-runtime-exception
  libraries (libgcc, libstdc++) have their source named and offered but not bundled. The written offer in
  `SOURCE.txt` covers all of these. LGPL libraries ship as separate DLLs/dylibs, so they stay replaceable.
- **No BIOS images or games** are distributed; users supply their own.
- Mednafen's own README flags a few GPLv3-incompatible parts (WonderSwan/Cygne, old QuickLZ);
  they are not part of the Saturn core, but if a release links Mednafen into anything else
  (instead of launching it as a separate program, as SE does now), get a legal read first.

Sources of the downloaded texts: libsdl-org/SDL (SDL2 and main branches), libsdl-org/sdl2-compat,
xiph/flac, xiph/ogg, mingw-w64 winpthreads, gnu.org (LGPL-2.1), gcc-mirror/gcc (COPYING.RUNTIME).
The Mednafen files were copied from the local checkout under `_emu/mednafen` (version 1.32.1).
This is an engineering inventory, not legal advice.
