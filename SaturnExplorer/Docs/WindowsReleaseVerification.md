# Windows release verification — brief for an automated agent

You are verifying, on a real Windows machine, the parts of Saturn Explorer's release packaging that were
written and reviewed on macOS and **have never run on Windows**. Your job is to run them, record exactly what
happens, and report. Do not fix anything unless a step says to; the person who gave you this file decides what
to change.

## Background (read once)

Saturn Explorer (SE) is a C++ Dear ImGui debugger for the Sega Saturn. It launches a **patched Mednafen**
(emulator, GPL) as a separate process. A release is one archive per platform: SE plus a `mednafen-se\` folder
beside it, the licences, and a GPL source offer. The Windows archive is a `.zip`.

The pieces involved (all in `SaturnExplorer\`):

| Piece | What it does | Windows-specific risk |
|---|---|---|
| `Integration\install.py` | builds SE (CMake, MSVC, Release) and Mednafen (MSYS2/MinGW autotools) | MSYS2 package install, DLL closure copied next to `mednafen.exe` |
| `Integration\package_release.py` | lays out the archive, merges licences, writes `SOURCE.txt`, smoke-tests, zips | Visual C++ runtime copy, MSYS2 manifest, copied-header comparison, zip layout |
| `Integration\make_source_offer.py` | `SOURCE.txt` + patched-source tarball | `tar`/`git archive` on Windows |
| `FrontEnd\src\Launcher.cpp` | finds `mednafen-se\mednafen.exe` beside the app; passes `-filesys.path_firmware` | `ExecutablePath()` (ANSI), path handling |
| `FrontEnd\src\App.cpp` | one-time "Before you launch" notice | none known |
| `.github\workflows\saturn-explorer-release.yml` | builds both platforms on a `v*` tag | never run |

Full design: `Integration\DISTRIBUTION.md` ("Cutting a release"). Licence inventory: `Licenses\README.md`.

## Rules

- **Do not push, commit, tag, or create a GitHub release.** Work in a scratch directory; leave the repo
  checkout otherwise unmodified (`git status` should stay clean apart from build output, which is ignored).
- **Do not obtain or distribute a Saturn BIOS or game images.** If the person provides test files, use them
  locally only.
- **Back up and restore** `%APPDATA%\SaturnExplorer\settings.ini` and `imgui.ini` around any run of the GUI.
  Restore them byte-identical afterwards.
- Only stop processes **you** started.
- `install.py --yes` may install build tools through `winget` and `pacman`. That is expected on this task. If
  anything else asks for elevation, a credential, or a payment, **stop and report instead**.
- Record outputs **verbatim**. Never summarise an error message.
- If a step fails, say whether the cause looks like (a) a bug in the repo, (b) your environment, or (c) a wrong
  assumption in this brief. Then continue with the next step where possible.

## Environment to record first

Windows version/build, x64 or ARM, Git, Python, CMake, Visual Studio version and `VCToolsRedistDir`
(run from a "Developer PowerShell for VS 2022"), MSYS2 location, and the active ANSI code page
(`chcp`, and `[System.Text.Encoding]::Default.CodePage`). Note whether Windows Developer Mode / symlink
privilege is on (`os.symlink` from Python).

## Steps

### 1. Get the code
```
git clone https://github.com/NoahSteam/RomHackingTools.git   (or git pull)
git rev-parse HEAD
cd RomHackingTools\SaturnExplorer
```
Record the commit. Everything below is run from `SaturnExplorer\`.

### 2. Offline packaging tests
```
python -I Integration\tests\check_package_release.py
```
Expect `PackageRelease: all checks passed`. Symlink-dependent checks may print "skipped" if symlinks are not
permitted; say which. Anything else is a failure to report with the full output.

### 3. Build SE and the patched Mednafen
```
python Integration\install.py --yes --mednafen-saturn-only
```
Record the whole output. Then collect, and put in the report:

- `build\bin\Release\SaturnExplorerFrontEnd.exe` exists; `build\bin\Release\licenses\` subfolders
  (expect: `imgui`, `fonts`, `zlib`, `nlohmann-json`, `ghidra-decompiler`).
- `_emu\mednafen\mednafen.exe` and **every `.dll` beside it** (names and sizes).
- **Question A:** are `libgcc_s_seh-1.dll`, `libstdc++-6.dll` or other GCC runtime DLLs among them?
- **Question B:** what is `_emu\mednafen\include\mednafen`? Run
  `python -c "import os; p='_emu/mednafen/include/mednafen'; print(os.path.islink(p), os.path.isdir(p), os.listdir(p)[:5])"`
  and, in an MSYS2 MINGW64 shell, `ls -la _emu/mednafen/include/`. It is meant to link to `../src`. State whether it
  is a real symlink, a directory copy, or something else. `package_release.py` compares it and handles the first
  two; anything else is a finding.
- `_emu\mednafen\.se-configure` contents (the recorded configure flags).

### 4. Package
From a normal (non-Developer) prompt first, then again from the Developer PowerShell, so the runtime lookup is
exercised both ways:
```
python Integration\package_release.py --se-app build\bin\Release --mednafen _emu\mednafen --ref wintest --out %TEMP%\se_dist
```
Expected: it either succeeds (`smoke test ok`, `wrote ...zip`) or **refuses with a specific reason**. Both are
informative. For each refusal, record the reason and decide: real mismatch, or a false positive? Likely suspects
and where to look:

| Message mentions | Look at |
|---|---|
| "differs from the pinned base" / "extra" / "missing" for many files | `compare_trees` in `package_release.py`; autotools files regenerated on Windows may need adding to `GENERATED_NAMES`. List the files. |
| `include/mednafen` | the symlink/copy handling in `compare_trees` (see Question B) |
| "not the copy MSYS2 has installed" | `msys2_manifest`; compare `_emu\mednafen\<dll>` with `C:\msys64\mingw64\bin\<dll>` by SHA-256 |
| "no source found for ..." | `msys2_source_url`; find the package in https://repo.msys2.org/mingw/sources/ by hand and record its exact name |
| "Visual C++ runtime DLLs were not found" | `msvc_redist_dirs`; list `…\VC\Redist\MSVC\*\x64\Microsoft.VC*.CRT` |
| "release label" / pacman not found | environment |

Only after recording a false positive, you may re-run with `--allow-mismatch` to see the later stages. Say
clearly which results came from an overridden run.

### 5. Inspect the package
If it was produced, extract the zip to a **new folder whose path contains a space**, and record:

- the top-level listing, and `mednafen-se\` (expect `mednafen.exe`, its DLLs, `firmware\PUT-BIOS-HERE.txt`,
  `README-FORK.txt`);
- `vcruntime140*.dll` / `msvcp140*.dll` beside `SaturnExplorerFrontEnd.exe`, with their file versions
  (`(Get-Item x).VersionInfo.FileVersion`) **and** the version of the toolchain that built SE
  (the `VCToolsRedistDir` folder name). The shipped runtime must not be older than the build tools;
- `licenses\` (single folder, merged): contains both SE's components **and** `mednafen`, `sdl`, `flac`, `ogg`,
  `lgpl`, `gcc-runtime`, `winpthreads`, `zlib`, `saturn-explorer`, plus `README.md`; there must be **no** separate
  `Licenses\` folder beside it (they are the same folder on Windows);
- `SOURCE.txt` in full; every library listed has a version, licence and source URL; the Microsoft runtime entry says
  it is not open source;
- `third-party-source\` (file names, sizes, SHA-256);
- `mednafen-se-source.tar.xz` exists and extracts.

Check **by hand** that each DLL in `mednafen-se\` is listed in `SOURCE.txt` under the right package, and that the
version in `SOURCE.txt` equals `pacman -Q <package>` from the MSYS2 shell.

### 6. Dependency check (the "clean machine" question)
With `dumpbin /dependents` (Developer prompt) on `SaturnExplorerFrontEnd.exe`, `mednafen.exe` and each DLL,
list every dependency that is **not** (a) a Windows system DLL or (b) shipped in the package. Any such DLL is a
defect. Then, if a VM or machine **without Visual Studio** is available, extract the zip there and start both
programs; record any "missing DLL" dialog verbatim. If no such machine exists, say so — do not skip silently.

### 7. Behaviour of the packaged app
Back up settings first. Then, with the extracted package (BIOS **not** present unless the person supplied one):

1. Start `SaturnExplorerFrontEnd.exe`. A dialog titled **"Before you launch"** should appear once. Record its text
   and a screenshot. Press OK, quit, start again: it must not reappear.
2. Open Launch Settings. The Mednafen path should be `<extract dir>\mednafen-se\mednafen.exe`, and the two
   firmware entries (`sega_101.bin`, `mpr-17933.bin`) should show as missing.
3. Inspect `%APPDATA%\SaturnExplorer\settings.ini`: `[emulators] mednafen` should be **empty** (the bundled path is
   deliberately not saved).
4. Move the extracted folder to a different location and start again: Mednafen must still be found.
5. Put the string `Z:\does\not\exist\mednafen.exe` in `[emulators] mednafen`, start SE: that path must be kept
   (not replaced by the bundled one) and Launch Settings must show it missing. Then remove that line.
6. **Only if the person supplied a BIOS and a game:** place the BIOS in `mednafen-se\firmware\`, launch the game
   from SE, and capture the Mednafen command line
   (`Get-CimInstance Win32_Process -Filter "Name='mednafen.exe'" | Select CommandLine`). It must contain
   `-filesys.path_firmware` followed by an **absolute** path ending in `mednafen-se\firmware`, and Mednafen must start the
   game. Stop only the Mednafen you started.
7. **Non-ASCII path:** extract a second copy under a folder containing a non-ASCII character (for example
   `é` and, separately, a Japanese character). Record whether SE finds the bundled Mednafen in each case and what
   the active code page is. This is a **known limitation** for characters outside the ANSI code page; you are
   measuring it, not fixing it.

Restore `settings.ini` and `imgui.ini` afterwards.

### 8. The CI workflow (only if `gh` is installed and authenticated)
```
gh workflow run saturn-explorer-release.yml --repo NoahSteam/RomHackingTools -f label=wincheck
gh run watch
```
Do **not** push a tag. Record the result of both jobs and, for any failure, the failing step's log. A
manual run only uploads artifacts. If `gh` is not available, say so and skip.

### 9. Optional: the SE test suite on Windows
```
cmake -S . -B build
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
```
Report the totals and every failure's name. `SaturnExplorerSh2GeneratedTests` is a known, unrelated failure.

## Known gaps this brief is trying to close

These were never run on Windows. For each, your report should say PASS, FAIL, or NOT RUN, with evidence.

| # | Unverified claim | Step |
|---|---|---|
| W1 | The Visual C++ runtime selection picks the build toolchain's runtime (or the newest installed), never an older one | 4, 5 |
| W2 | Every shipped DLL is byte-identical to MSYS2's installed copy, and its source is found at repo.msys2.org | 4, 5 |
| W3 | Copyleft source for the MSYS2 DLLs is downloaded and hashed; GCC runtime DLLs (if present) are named and offered | 4, 5 |
| W4 | The provenance comparison has no false positives on a Windows checkout (autotools files, `include/mednafen`) | 3, 4 |
| W5 | The Windows smoke test (`mednafen.exe -help` with the packager's environment) passes | 4 |
| W6 | The zip layout is correct, with a single merged `licenses\` folder | 5 |
| W7 | No undeclared dependencies: the package runs on a machine without Visual Studio or MSYS2 | 6 |
| W8 | `ExecutablePath`/`BundledMednafenPath` find `mednafen-se\mednafen.exe`, including after the folder is moved | 7 |
| W9 | A recorded Mednafen path, even a missing one, is never replaced; the bundled path is not written to settings | 7 |
| W10 | `-filesys.path_firmware` is passed as an absolute path and Mednafen reads the BIOS from there | 7 |
| W11 | The first-run notice appears once | 7 |
| W12 | The release workflow succeeds on both runners | 8 |

## Report format

Write the report to a file **outside the repo** (for example `%TEMP%\se_windows_report.md`) and give its path
and contents to the person. Use this shape:

```
# Windows verification report
Commit: <hash>     Date: <date>     Machine: <os, build, code page, VS, MSYS2>

## Summary
<3-6 lines: what works, what does not, the single most important finding>

## Results by claim
| # | Result (PASS/FAIL/NOT RUN) | Evidence (command + verbatim output, or file) |

## Defects found
For each: what, where (file and function if you can tell), exact failing output, your view of the cause
(repo bug / environment / wrong assumption in the brief), and a suggested fix if obvious. Do not apply it.

## Answers to Question A and Question B

## Things that surprised you
Anything the brief did not anticipate.

## Not done
Steps skipped and why.
```
