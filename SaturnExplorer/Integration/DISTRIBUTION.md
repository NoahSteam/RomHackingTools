# Distribution & building the emulator side

The live tap patches a *third-party* emulator (Yabause, Mednafen/Beetle). That raises
two practical questions — how to keep the patcher from breaking when upstream moves,
and whether you can hand users a prebuilt binary. Short answers below; none of this is
legal advice.

## What you are shipping, security-wise

Whatever you hand a user -- patch or binary -- the patched emulator listens on a local endpoint
that is a **privileged control channel**, not a read-only viewer feed. Anything that can connect
can write any bus address, install breakpoints, pause and step the CPUs, inject controller input
and restore a savestate, and none of it is authenticated: no handshake, no token, no distinction
between a viewer and a controller. That is what a debugger needs, and it is also why the
transport is the only access control there is.

- The Unix socket and the Windows named pipe are reachable only by local users with permission to
  them, which is the intended arrangement.
- The **TCP listener** (loopback, port 6845, for the web build's WebSocket bridge) is reachable by
  every local process and every local user -- and by whatever can reach that bridge, if it is run
  with a wider bind or a permissive origin policy.

So: keep it on loopback, do not forward or expose it, and do not leave a patched build listening
on a shared or multi-user host. Say the same to users if you distribute a prebuilt binary, since
they did not choose to start a server. The full statement lives with the protocol, in
`Integration/Common/SeLiveProtocol.h` (review finding HOOK-03).

## Fork and pin your upstream

`apply.py` injects at specific upstream symbols and code sites (`espec->MasterCycles`,
the `ss/Makefile.am` source list, `DBG_AddBreakPoint`, `VDP2::RawRegs`, `CheatMemWrite`,
Yabause's `Vdp2VBlankOUT` / `MappedMemoryWriteByteNocache`, …). If upstream renames or
moves one, the anchor misses. It fails **loudly** — the patcher prints `ANCHOR MISS`
and the injected accessors become compile errors, never silent corruption — but that's
still a broken build until someone re-anchors it.

So:

- **Fork the emulator and pin the patcher to a known-good commit.** That's your stable
  base; you update deliberately, not whenever upstream shifts.
- **Periodically run `apply.py --check` against fresh upstream** to detect drift early —
  it reports exactly which anchor moved, so you can re-anchor before it bites.

**Mednafen vs. Beetle Saturn:** they share the `ss/` Saturn core but are *different
build targets*. `Integration/Mednafen/apply.py` targets **standalone `mednafen-git`**
(autotools: `src/ss/Makefile.am`, `./configure`, `--enable-debugger` for breakpoints) —
that's the path verified end-to-end (it builds + links into a real Mednafen 1.32.1
binary). Beetle Saturn wraps the same core in the libretro API with a different build
and hook sites (`retro_run` instead of `Emulate()`), so its accessors would port but
the build wiring + lifecycle hooks need a separate Beetle patcher. Pick one to support
first; standalone Mednafen is the tested one.

## Prebuilt binaries: Yabause and Mednafen, both with source

Both emulators are GPLv2, so *legally* you may distribute a modified binary **as long as
you also provide the corresponding modified source** (which you already do — the patch
is public). But policy and practicality differ:

- **Yabause** — fine to ship a prebuilt patched binary alongside a link to the patched
  source. Standard GPL compliance.
- **Mednafen** — GPLv2, so a prebuilt patched binary is fine with the corresponding source. Upstream's
  own `README` ("Distributing a separate patch set or an outright fork, with branding that clearly
  conveys the forkiness, is recommended") is the stance to follow: **do not present the binary as an
  official Mednafen release.** Brand it as a Saturn Explorer build -- a distinct archive/binary name,
  the existing window-title suffix (`apply.py` appends `SaturnExplorer Enabled …`), and a release note
  saying it is a modified fork and that problems with it do not go to upstream.
- **The blocker that hits both:** a Saturn emulator needs a **Saturn BIOS**, which is
  copyrighted and **cannot be redistributed**. A "double-click this exe" bundle still
  can't include the one file users must supply themselves (plus disc images). So the
  BIOS step is the real user friction, not the build.

**Recommendation:** ship prebuilt, clearly branded binaries of both (Mednafen is the tested one) so most
users never need a toolchain, **and** keep the patch + one-command setup below for anyone who would
rather build. Document the BIOS-is-yours-to-supply step prominently either way, and warn about the
live endpoint (see "What you are shipping, security-wise") since users did not choose to start a server.

## The one-command setup users run

**Windows:** [`install.bat`](../install.bat) (→ [`install.py`](install.py)) does all of the
below automatically — builds Saturn Explorer, downloads a pinned emulator, patches it, and
builds it, installing any missing toolchain (git, CMake, MSVC, MSYS2) via `winget` with your
consent. `install.bat --dry-run` previews the plan. See [`INSTALL.md`](INSTALL.md).

The manual steps it wraps:

```sh
# Yabause:
python3 Integration/Yabause/apply.py  /path/to/yabause     # then build Yabause
# Mednafen (add --with-pause for the pause/step gate; --enable-debugger for breakpoints):
python3 Integration/Mednafen/apply.py /path/to/mednafen --with-pause
cd /path/to/mednafen && ./configure --enable-debugger && make
```

Both patchers are idempotent (`--check` to preview, `--revert` to undo). Then launch
Saturn Explorer and **File → Connect to emulator (live)** (or start it with `--live`).
For **browser** live viewing, see the "Web (browser) live viewing" section in
`Integration/Yabause/README.md` (build with `-DSE_WEB_LIVE=ON`, serve cross-origin
isolated, run a WebSocket→TCP relay).

## Shipping Saturn Explorer itself: third-party licences

Saturn Explorer compiles in, or embeds, code under these licences:
- Dear ImGui (MIT);
- the IPAGothic subset (IPA Font License);
- Liberation Sans (OFL 1.1);
- zlib;
- nlohmann/json (MIT);
- the **Ghidra decompiler** with its SuperH language spec (Apache-2.0).

Each licence asks that its notice travel with the binary, so the build puts every licence text into the output:

| Build | Where the texts land |
|---|---|
| CMake, Windows / Linux | `licenses/<component>/` beside the executable |
| `build-release-msbuild.bat` | `x64\Release\licenses\<component>\` |
| macOS `.app` / DMG | `SaturnExplorer.app/Contents/Resources/licenses/<component>/` (sealed by the signature) |

The CI release jobs fail if any text is missing. **Help → About → Third-party** names the same set.

When you add a third-party component, update all four places:
1. the CMake `SE_LICENSE_FILES` list;
2. the `CopyFileToFolders` items in `FrontEnd/FrontEnd.vcxproj`;
3. the CI checks in `.github/workflows/saturn-explorer-verify.yml`;
4. the About list in `App::DrawAboutModal`.

Ghidra specifics:
- **`LICENSE` and `NOTICE` must ship.** Apache-2.0 §4(d) requires the NOTICE. The vendored sources are modified by `FrontEnd/third_party/ghidra-decompiler/patches/`. Each patched file says so in its header (§4(b)), and the About dialog says the engine is modified.
- **No new runtime dependency.** The engine is compiled into the executable, and its SLEIGH files are embedded (`Sh2SpecData.h`) and written to the config directory at first use. No dynamic libraries were added, so macOS bundling (`SE_MACOS_BUNDLE_LIBS`) and signing are unchanged.
- **No Java and no Ghidra install** on the user's machine.

### If a release bundles a patched Mednafen

The list above covers only what is compiled into Saturn Explorer. A package that also carries a
patched Mednafen adds Mednafen's own GPL and bundled-code notices and the licences of the
libraries shipped beside it (SDL, FLAC, libogg, gettext, libiconv, winpthreads, the GCC runtime).
`Licenses/` holds those texts, with an index of which component needs which file and the source-offer
obligation; merge it with the build's `licenses/` folder when assembling the archive. A bundled binary
also carries the GPL source offer and the fork branding described under "Prebuilt binaries" above.

### Cutting a release

A release is one archive per platform: Saturn Explorer plus `mednafen-se/` (the patched Mednafen,
Saturn core only) beside it, the licences, and the GPL source offer.

| File | Role |
|---|---|
| `Integration/Mednafen/UPSTREAM.json` | The Mednafen base: upstream version and tarball hash, and the fork commit `install.py` pins. Edit it with any fork bump. |
| `Integration/make_source_offer.py` | Writes `SOURCE.txt` and `mednafen-se-source.tar.xz` (the fork base with `apply.py` already run on it), from `UPSTREAM.json` and the same patch arguments and defines `install.py` builds with. |
| `Integration/package_release.py` | Lays out the archive from the two existing builds, relinks Mednafen's dylibs into `mednafen-se/lib` (macOS), merges the licences, smoke-tests the result, then makes the `.dmg` / `.zip`. |
| `.github/workflows/saturn-explorer-release.yml` | Runs the above on both platforms for a `v*` tag and leaves a DRAFT GitHub release. |
| `make-release.bat` | Windows, on your own machine: `install.py --yes --mednafen-saturn-only`, then `package_release.py`, giving `dist\SaturnExplorer-<label>-windows.zip` (label = the argument, else the short commit hash). For trying a build; a release for others should come from the workflow, whose clean runner cannot carry over a stale DLL or setting from a developer's machine. |

`mednafen-se/` sits beside the app (beside the `.app` on macOS) because a signed bundle cannot be
written to and the user adds the BIOS to `mednafen-se/firmware/`. Saturn Explorer finds that copy when no
other Mednafen path is recorded (`BundledMednafenPath` in `Launcher.cpp`), tells Mednafen to read the
BIOS from that `firmware/` folder (`-filesys.path_firmware`), and shows a one-time notice about the BIOS
and the local-only debugging endpoint. On macOS the user must keep the app and the folder together;
an app run straight from a downloaded, still-quarantined location will not see the folder.

On Windows the archive holds `SaturnExplorerFrontEnd.exe` (not the test executables the build folder also
has), the app-local Visual C++ runtime, `mednafen-se/` with the MSYS2 DLLs it needs, and `licenses/`.
Saturn Explorer reads its own path in the active ANSI code page, so a folder name with characters outside
that page (for example Japanese on a Western-European Windows) hides the bundled Mednafen (measured, not
fixed); an accented Latin-1 name is found but its path is drawn garbled. Windows packaging was checked on a real machine and on a
`windows-latest` runner (workflow_dispatch, label only, no tag). The macOS job was not re-run after its
Mednafen bootstrap was changed (`Docs/MacReleaseSteps.md`).

The in-app **Check for Updates** compares the build's release tag with GitHub's latest published release
(`releases/latest`) and offers the release page. The tag is baked in at build time
(`cmake/GenerateBuildInfo.cmake`, and `FrontEnd.vcxproj` for the MSBuild project) from `SE_RELEASE_VERSION`,
which the release workflow sets to the tag it builds (and which stands even if the tree reads as modified;
the tag jobs fail if it did not get baked in), else from a `v*` tag exactly on HEAD of an unmodified tree.
A build with no tag (built from source, a manual workflow run) has no release to compare, so it is compared
with the latest commit on the default branch instead ("pull and rebuild") and shown the latest release.
So tag releases
`v<number>[.<number>...]` (`v1.0`, `v1.2.3`): the check orders tags as dotted numbers, and a release must be
*published* (a draft does not count) before users can see it.

The Windows archive is **not code-signed** (a deliberate choice for now; see the options below). What stands in for it:
the archive is built by the workflow from the tagged commit, each archive is published with a `SHA256SUMS-<platform>.txt`
(also in the draft release's notes and attachments), and the README in the zip, the project `ReadMe.txt` and the release
notes tell users to expect a SmartScreen prompt and how to get past it. After the macOS image is notarised by hand and
replaces the CI one, regenerate its checksum (`shasum -a 256`) before uploading. If people report blocked or flagged
downloads, sign: Azure Artifact Signing (about $10/month; individuals in the USA and Canada) or SignPath Foundation
(free for open source, publisher shown as the Foundation) both work from GitHub Actions; sign `SaturnExplorerFrontEnd.exe`
and `mednafen.exe` (and the MSYS2 DLLs if wanted) after the build and before `package_release.py`, with an RFC 3161
timestamp. A key file in a repository secret is not an option with current CA rules: keys live on hardware or in a cloud HSM.

Still manual: Developer ID signing and notarisation (below), Windows signing, and running the
Windows archive on a machine that has no Visual Studio or MSYS2.

### macOS Developer ID signing and notarisation

CI's archive is ad hoc signed, so Gatekeeper quarantines a downloaded copy. A release for others is
signed with a Developer ID (hardened runtime, timestamped) and notarised **after** it has been
assembled, so the thing Apple approves is the thing users download -- the app *and* the sibling
`mednafen-se/` folder together. Do it by hand on a Mac that holds the certificate, in this order, and
record each release in `Docs/FunctionalityVerification/` (version, commit, `spctl` output):

```sh
ID="Developer ID Application: <name> (<team>)"
# 1. The app, signed (the install step fails if the signature does not verify).
cmake -S . -B build-release -DCMAKE_BUILD_TYPE=Release -DSE_MACOS_BUNDLE_LIBS=ON -DSE_MACOS_SIGN_IDENTITY="$ID"
cmake --build build-release --parallel
cmake --install build-release --prefix "$PWD/stage"
# 2. The patched Mednafen (built by install.py, see above), then the combined, smoke-tested image with
#    Mednafen's relinked libraries and the .dmg itself signed by the same identity. Not the CPack .dmg: that holds only the app.
python3 Integration/package_release.py --se-app stage/SaturnExplorer.app --mednafen _emu/mednafen/src/mednafen \
        --ref v1.0 --out dist --sign-identity "$ID"
# 3. Notarise and staple that image, then check it.
xcrun notarytool submit dist/SaturnExplorer-v1.0-macos.dmg --keychain-profile <profile> --wait
xcrun stapler staple dist/SaturnExplorer-v1.0-macos.dmg
spctl -a -vvv -t install dist/SaturnExplorer-v1.0-macos.dmg   # expect: accepted, Notarized Developer ID
```

`package_release.py` refuses to run when the Mednafen binary does not match the source offer it would
publish. The checkout must be at the commit in `UPSTREAM.json`; every source file in it must be identical to a
freshly made copy of that commit with `apply.py` run on it (so a stale `se_export.c` or a local edit is caught;
the autotools-generated files are ignored) and none may be newer than the binary; it must have been built
Saturn-only with rewind; `Integration/` must have no uncommitted changes; and the bundled Homebrew libraries
must be the versions Homebrew currently publishes source for (`brew upgrade` and rebuild Mednafen first if not).
This shows nothing newer or different sits beside the binary, not that it was compiled from that tree.
`--allow-mismatch` overrides for local trials only. A copyleft library whose source cannot be found (MSYS2:
`repo.msys2.org/mingw/sources`, Homebrew: the formula's tarball) also stops the run.

On Windows the frontend uses the dynamic Visual C++ runtime, so the packager copies the app-local
`vcruntime140*.dll` / `msvcp140*.dll` redistributables (from the Visual Studio that built it, or `--crt-dir`)
beside the exe; verify on a machine without Visual Studio installed.
