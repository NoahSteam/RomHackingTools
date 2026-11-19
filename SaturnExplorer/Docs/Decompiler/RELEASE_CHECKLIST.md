# Release checklist

The last item of "Done, overall" in `REVIEW_PROGRAM.md` is "the release checklist passing on a tagged build". This is that
checklist. It covers what `.github/workflows/saturn-explorer-release.yml` produces (the Windows zip and the macOS dmg), the
checks CI does not make, and the manual steps a person runs on a clean machine. It was written in area 12, round 1
(`REVIEW_LEDGER.md`, A12-41), from the workflows, `Integration/DISTRIBUTION.md`, `Docs/MacReleaseSteps.md` and
`Docs/WindowsReleaseVerification.md`; those stay the long-form references for how each step works.

Every step has a number, who or what runs it, and the condition that makes it pass. **CI** names the job (workflow file in
brackets: V = `saturn-explorer-verify.yml`, D = `saturn-explorer-decompiler.yml`, R = `saturn-explorer-release.yml`).
**Manual** is a person. **Gap** marks a check the workflows do not make yet (ledger row in brackets): it is manual until the row
is fixed, and the row says what automating it takes. The release is a failure if any step fails; do not publish around one.

The tag is `v<number>[.<number>...]` (`v1.0`, `v1.2.3`): Check for Updates orders tags as dotted numbers. Nothing here tags or
publishes for you: a person pushes the tag (step 20) and publishes the draft (step 33).

## A. Before tagging: the commit is the one that was reviewed

| # | Who / what | Pass condition |
|---|---|---|
| 1 | Manual | The commit to tag is on `master`, and `git status` is clean in the checkout that will be tagged. `Integration/` has no uncommitted change (the packager refuses otherwise). |
| 2 | Manual | `REVIEW_LEDGER.md`: no open P1 or P2 row in any area; every area 1 to 12 is recorded done in `15_DECOMPILER_STEP5.md` with its two clean rounds. |
| 3 | CI, V: `Supported platform build + tests (windows-latest)` | Green on the commit: build, the full `ctest` (95 tests on 2026-11-19; the step "Check the registered test count" passes), the decompiler tests with their output, and "Decompiler tests in Release (Windows)". |
| 4 | CI, V: `Supported platform build + tests (macos-latest)` | Green on the commit (110 tests), and `se-render` and `SaturnExplorer.app` exist. |
| 5 | CI, V: `Decompiler engine + tests (macOS x86_64 under Rosetta)` | Green: the binaries are x86_64 (`lipo`), every selected test was built, all pass. |
| 6 | CI, V: `build-release.bat (Windows, CMake Release)`, `build-release-msbuild.bat (Windows, checked-in solution, auto)` and `(... v142)` | All three green: `SaturnExplorerFrontEnd.exe` / `SaturnExplorer.exe` exist, the exe carries the commit (MSBuild), and all seven licence files are beside it. |
| 7 | CI, V: `macOS bundle + DMG (ad-hoc signed)` | Green: the bundle verifies (`codesign`), carries the licences, has no Homebrew reference (A12-35 made this check real), the DMG verifies, and both bundled binaries start with `--selftest` with Homebrew's SDL removed. |
| 8 | CI, D: `Decompiler tests under ASan + UBSan + LSan (Linux, gcc)` | Green on the commit: no sanitizer report; the fifteen expected binaries all have a log and a pass line. |
| 9 | CI, D: `Vendored tree = pinned Ghidra + patches (Linux)` | Green on the commit: the vendored engine and the SuperH spec files are the pinned upstream plus `patches/` (so what ships is what `VENDOR.md` says), the patch bookkeeping lints, the gate's self-test rejects its planted defects, no raw string literal is over MSVC's limit. |
| 10 | CI, D: `Area 9 store tests incl. the real full disk (Linux)` | Green on the commit; the full-disk case ran (`SE_A9R5_FULLDIR_REQUIRED=1`). |
| 11 | CI, D: `Generator, long run (Linux)` | The nightly has run its whole step list green on every night of the last 7, and the last run is on a commit at or after the one being tagged, or a manual dispatch on the commit is green (`gh workflow run saturn-explorer-decompiler.yml`; it takes about 35 minutes). Its log ends `SUMMARY: seeds=20000` and `all passed`. |
| 12 | Manual | The CI matrix (steps 3 to 11) has been green on `master` for a week ("Done, overall"): no red run of V or D on `master` in that time other than runner failures ("not acquired by Runner", "lost communication") that a re-run cleared, each noted in the ledger. |
| 13 | Manual | `Integration/Mednafen/UPSTREAM.json` names the commit the release is meant to ship; `_emu/mednafen` is not part of the repository and CI rebuilds it from the pin (step 22). |
| 14 | Manual | The release label is a plain tag name: letters, digits, `.`, `_`, `-` only. |

## B. The tagged build

| # | Who / what | Pass condition |
|---|---|---|
| 20 | Manual | `git tag vX.Y.Z <commit>` and `git push origin vX.Y.Z`. The tag points at the commit checked in steps 3 to 11 (`git rev-parse vX.Y.Z^{commit}`). This is the one step that publishes nothing. |
| 21 | CI, R: workflow starts | One run of "Saturn Explorer Release" on the tag with the jobs `macOS archive`, `Windows archive`, `macOS Developer ID signing and notarization` and `Draft GitHub release`. |
| 22 | CI, R: `macOS archive` and `Windows archive`, step "Build Saturn Explorer and the patched Mednafen" | Succeeds. The patched Mednafen was built at the pinned commit, Saturn core only, with rewind. |
| 23 | CI, R: both build jobs, step "Check the Demo menu is off" | `SE_DEMO_MENU:BOOL=OFF` is in the build's `CMakeCache.txt`. |
| 24 | CI, R: both build jobs, step "Check the release version is baked in" | `BuildInfo.h` contains `kBuildVersion = "<tag>"`. (Checks the generated header, not the shipped exe; step 41 checks the exe.) |
| 25 | CI, R: both build jobs, step "Package" | `package_release.py` ends `smoke test ok` and `wrote ...`. It has refused, by itself, any of: a Mednafen binary that does not match the source offer, a copyleft library with no source found, a missing Visual C++ runtime, a wrong licence layout. Never run it with `--allow-mismatch` for a release. |
| 26 | CI, R: both build jobs, steps "Checksum" and upload | `SHA256SUMS-windows.txt` and `SHA256SUMS-macos.txt` exist beside `SaturnExplorer-<tag>-windows.zip` and `SaturnExplorer-<tag>-macos.dmg`; the artifacts `windows` and `macos` are uploaded (`if-no-files-found: error`). |
| 27 | CI, R: `macOS Developer ID signing and notarization` | Needs a person's approval of the `release` environment. Runs "Sign, notarize and staple" (not skipped) and uploads the artifact `macos-notarized` with its own `SHA256SUMS-macos.txt`. If the job only warns "Signing secrets are not configured" the dmg is ad hoc signed and not notarized: a release for others is **not** ready (step 52 below is the manual route). |
| 28 | CI, R: `Draft GitHub release` | A draft release `Saturn Explorer <tag>` exists with the Windows zip, the macOS dmg (the notarized one), both checksum files, and generated notes. Its notes say the macOS archive is not notarized only when it is not. |
| 29 | Manual | No test was run on the tagged build by the release workflow: it neither builds the tests nor requires steps 3 to 11 to pass on the tag. Step 12's evidence is the substitute. (Gap: A12-42.) |

## C. The archives, opened

Run on the draft's attachments, downloaded as a user would get them (a browser download, so the macOS quarantine flag is set).

| # | Who / what | Pass condition |
|---|---|---|
| 30 | Manual, any machine | `shasum -a 256 -c SHA256SUMS-<platform>.txt` passes for each archive. For macOS, against the file that is attached (the notarized one), not the one the `macos` job printed. |
| 31 | Manual | Windows zip top level: `SaturnExplorerFrontEnd.exe`, the app-local `vcruntime140*.dll` / `msvcp140*.dll` (not older than the toolset that built it), a single merged `licenses\` folder, `mednafen-se\` (`mednafen.exe`, its DLLs, `firmware\PUT-BIOS-HERE.txt`, `README-FORK.txt`), `README.txt`, `SOURCE.txt`, `mednafen-se-source.tar.xz`, `third-party-source\`. No test executable and no `se-render` / `se-sleighc`. |
| 32 | Manual | macOS dmg: `SaturnExplorer.app` and `mednafen-se/` side by side, `Licenses/` beside them, `README.txt`, `SOURCE.txt`, `mednafen-se-source.tar.xz`, `third-party-source/`. |
| 33 | Manual (reading) | Licences: `licenses/` (Windows) or `SaturnExplorer.app/Contents/Resources/licenses/` (macOS) holds `ghidra-decompiler/LICENSE`, `ghidra-decompiler/NOTICE`, `zlib/LICENSE`, `nlohmann-json/LICENSE.MIT`, `imgui/LICENSE.txt`, `fonts/LICENSE-IPAfont.txt`, `fonts/LICENSE-Liberation.txt`, plus Mednafen's, SDL's, FLAC's, libogg's, the LGPL text and, on Windows, `gcc-runtime` and `winpthreads`. Help, About, Third-party names the same set. |
| 34 | Manual (reading) | `SOURCE.txt`: every library has a version, a licence and a source URL; the tag it names is the tag being released; the Microsoft runtime entry says it is not open source. `mednafen-se-source.tar.xz` extracts. |
| 35 | Manual, on the signing Mac, macOS only | `spctl -a -vvv -t install SaturnExplorer-<tag>-macos.dmg` says `accepted` and `Notarized Developer ID`; `xcrun stapler validate` passes. Then as a downloader gets it: copy the file, set the quarantine attribute (`xattr -w com.apple.quarantine "0083;$(printf '%x' $(date +%s));Safari;" copy.dmg`), mount it, `spctl -a -vvv -t exec` the app. |
| 36 | Manual, Windows only | The zip is not code-signed (a deliberate choice, `DISTRIBUTION.md`): the release notes and `README.txt` say to expect a SmartScreen prompt. Nothing to pass here but that the text is present. |

### The decompiler in the package

The SuperH spec files (`sh-2.sla`, `.ldefs`, `.pspec`, `.cspec`) are not files in the archive: they are compiled into the
executable (`Sh2SpecData.h`) and written to `<config dir>/decompiler/<first 16 hex of the bundle hash>/` at first use
(`PLAN.md` A5). So "the spec files are in the package" means the executable carries the bundle that CI checked, and the
steps below prove it from the shipped binary.

| # | Who / what | Pass condition |
|---|---|---|
| 37 | CI, D (steps 8, 9, 3, 4) | The spec bundle in the tagged commit equals the compiled `sh-2.slaspec` and the pinned Ghidra's files (`SaturnExplorerSlaFreshnessTests`, `Vendored tree` job). This is the vendored-tree check; it is not repeated on the archive. |
| 38 | Manual, first run, each platform | On a machine with no earlier Saturn Explorer settings (or after moving `<config dir>/SaturnExplorer` aside: `%APPDATA%\SaturnExplorer` on Windows, `$XDG_CONFIG_HOME` or `~/.config` + `/SaturnExplorer` on macOS): open a savestate that has game code, open the Decompiler panel on a function. Pseudocode appears with no "unavailable" message; `<config dir>/decompiler/<16 hex>/` now exists with `manifest.json` and the spec files. Delete one of them, restart, decompile again: it is repaired. (Gap: A12-42, no automated first-run check on either platform.) |
| 39 | Manual | Help, About: the version shows `<tag>`, not "not a tagged release", the engine is named as a modified Ghidra decompiler, and there is no Demo menu. |
| 40 | Manual, macOS | `grep -a -c "<tag>" SaturnExplorer.app/Contents/MacOS/SaturnExplorer` is non-zero. |
| 41 | Manual, Windows | The tag is in the shipped exe: `findstr /C:"<tag>" SaturnExplorerFrontEnd.exe` finds it (the workflow checked the header, step 24, not the exe). |

## D. Clean-machine smoke test

On a machine that has no Visual Studio, no MSYS2 and no Homebrew (a fresh VM). No BIOS or game is needed except where noted; do
not obtain or distribute either.

| # | Who / what | Pass condition |
|---|---|---|
| 42 | Manual, Windows | Extract the zip to a path with a space and plain ASCII letters. `SaturnExplorerFrontEnd.exe` starts with no "missing DLL" dialog, and so does `mednafen-se\mednafen.exe -help`. (`dumpbin /dependents` on a build machine lists nothing that is neither a Windows system DLL nor in the package.) |
| 43 | Manual, macOS | Mount the dmg on a Mac with no Homebrew, copy `SaturnExplorer.app` and `mednafen-se` to Applications side by side, open the app with right-click, Open (or directly, if notarized). It starts; `Contents/MacOS/SaturnExplorer --selftest` prints `selftest ok`. |
| 44 | Manual, both | The "Before you launch" notice appears once and not again after a restart. Launch Settings shows the bundled Mednafen path labelled as included with this release, and the two firmware names as missing. Move the folder and start again: Mednafen is still found. |
| 45 | Manual, both, only with a BIOS and a game the person owns | Launch from Saturn Explorer: the Mednafen command line has `-filesys.path_firmware` with an absolute path ending in `mednafen-se/firmware`, and the game starts. Then Live mode: open the Decompiler on a running function, and a savestate of the same game. |
| 46 | Manual, both | Open a savestate given on the command line (`SaturnExplorerFrontEnd.exe state.yss`), including a path with a non-ASCII character on Windows (A12-25). The File, Open dialog opens the same state. Open a gzip-compressed Mednafen `.mcs` state (the MSBuild path has no ctest for this, `PLAN.md` A6). |
| 47 | Manual, both | Quit with the Log panel showing an error row: hovering shows the full text, "Copy message" copies it (A12-18). Nothing writes outside the config folder and the folder beside the app. |

## E. Publish and record

| # | Who / what | Pass condition |
|---|---|---|
| 50 | Manual | Steps 30 to 47 are recorded in `Docs/FunctionalityVerification/` for this tag: version, commit, the `spctl` output (macOS), and for each step passed, failed or not run with the reason. A step that was not run is a reason not to publish, unless the ledger accepts it. |
| 51 | Manual | The ledger has a row for every defect found by the checklist, and none is an open P1 or P2. |
| 52 | Manual, only if step 27 did not notarize | Notarize by hand on the Mac that holds the certificate (`python3 Integration/notarize_release.py --dmg <ad hoc dmg> --out <signed dmg> --keychain-profile se-notary`, or `Docs/MacReleaseSteps.md` step 3); regenerate `SHA256SUMS-macos.txt` from that file (`shasum -a 256`) and replace both attachments. Repeat steps 30 and 35 on it. |
| 53 | Manual | Publish the draft (a draft is invisible to Check for Updates). Then, from a build older than the tag, Help, Check for Updates offers this release and its page. |
| 54 | Manual | Back up the Developer ID certificate as a `.p12` outside the repository if the signing was done by hand. |

## What is not covered, and why

* Linux is a development convenience (`PLAN.md`): no Linux archive is made or checked.
* Windows code signing is not done (step 36). Windows packaging has been run on a real machine and on a `windows-latest`
  runner; running the zip on a machine with no Visual Studio or MSYS2 is step 42, by hand.
* The checklist's automated half is the CI matrix on the tagged commit (steps 3 to 11) and the release workflow (21 to 28). The
  release workflow does not require the matrix, runs no test, does not start the Windows exe and does not exercise the
  decompiler in the package. Those are A12-42, and steps 29, 38 and 42 are manual because of it.
