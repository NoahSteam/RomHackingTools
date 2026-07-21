#!/usr/bin/env python3
"""
install.py — one-command Windows setup for Saturn Explorer + a patched emulator.

What it does, in order:
  1. Build Saturn Explorer itself (this repo) with CMake + MSVC (Release).
  2. Clone a *pristine* upstream emulator at a pinned revision.
  3. Patch it with the matching Integration/<emu>/apply.py (idempotent).
  4. Build it:
       - Mednafen  -> MSYS2/MinGW autotools (./configure --enable-debugger && make)
       - Yabause   -> CMake + MSVC (+ Qt)
  5. Print where everything landed and the BIOS-is-yours-to-supply reminder.

It is an *orchestrator with assisted install*: it detects the tools it needs
(git, CMake, an MSVC C++ toolchain, MSYS2) and — only with your consent — installs
missing ones via winget / MSYS2 pacman. Nothing is installed or built until you
approve; --dry-run prints the whole plan and touches nothing.

Why download-and-build instead of shipping a patched binary:
  * Mednafen's project discourages redistributing modified "Mednafen" builds, so the
    accepted path is to patch + build locally (this).
  * A Saturn emulator needs a Saturn BIOS, which is copyrighted and cannot be bundled.
    You supply your own BIOS + discs; no installer can remove that step.

Usage (run from a normal Windows terminal; the .bat wrapper finds Python for you):
  python Integration\\install.py                     # SE + Mednafen (default)
  python Integration\\install.py --with-yabause      # also build Yabause (Qt)
  python Integration\\install.py --yabause-variant=kronos --no-mednafen
  python Integration\\install.py --dry-run           # show the plan, do nothing
  python Integration\\install.py --yes               # don't prompt before installs/builds
  python Integration\\install.py --prefix "C:\\SaturnExplorer"   # where to clone/build emus

Pins: the emulator revisions below are the fork-and-pin anchors from DISTRIBUTION.md.
Override per emulator with --mednafen-rev / --yabause-rev (or edit EMULATORS).
"""

import argparse
import configparser
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))          # .../SaturnExplorer/Integration
SE_ROOT = os.path.dirname(HERE)                            # .../SaturnExplorer

# ---------------------------------------------------------------------------
# Emulator registry.
#
# By default we clone OUR OWN FORK (under FORK_OWNER) rather than upstream, per
# "Fork and pin your upstream" in Integration/DISTRIBUTION.md: the fork is a
# stable base you update deliberately, so an upstream rename never breaks a build
# mid-flight. `upstream` is recorded for reference / drift-checks and is what
# --upstream clones instead. `fork_name` is the repo name under FORK_OWNER (it
# can differ from upstream's — e.g. two different Yabause forks can't both be
# "yabause" under one account). `rev` is the pinned commit/tag ("master" is a soft
# default; pin a known-good commit for a reproducible build).
# ---------------------------------------------------------------------------
FORK_OWNER = "NoahSteam"          # our GitHub account/org holding the pinned forks


def fork_url(name):
    return f"https://github.com/{FORK_OWNER}/{name}.git"


EMULATORS = {
    "mednafen": {
        "fork_name": "mednafen-git",
        # STANDARD (standalone) Mednafen, NOT the Beetle Saturn libretro core. The
        # libretro-mirrors org just hosts a *git mirror* of Mednafen's source (whose
        # official repo is Mercurial, so not git-cloneable) — it is the standalone
        # emulator we patch via ss.cpp's Emulate(), not beetle-saturn-libretro.
        "upstream":  "https://github.com/libretro-mirrors/mednafen-git.git",
        "rev":  "master",
        "patch_subdir": os.path.join("Integration", "Mednafen", "apply.py"),
        "patch_args": ["--with-pause"],           # inject the pause/step gate too
        "toolchain": "msys2",
    },
    # Qt-lineage forks share the same patcher (apply.py auto-detects the fork name).
    "yabause": {
        "fork_name": "yabause",
        "upstream":  "https://github.com/Yabause/yabause.git",
        "rev":  "master",
        "patch_subdir": os.path.join("Integration", "Yabause", "apply.py"),
        "patch_args": [],
        "toolchain": "msvc",
    },
    "sanshiro": {
        "fork_name": "yaba-sanshiro",             # can't be "yabause" too under one owner
        "upstream":  "https://github.com/devmiyax/yabause.git",
        "rev":  "master",
        "patch_subdir": os.path.join("Integration", "Yabause", "apply.py"),
        "patch_args": [],
        "toolchain": "msvc",
    },
    "kronos": {
        "fork_name": "Kronos",
        "upstream":  "https://github.com/FCare/Kronos.git",
        "rev":  "master",
        "patch_subdir": os.path.join("Integration", "Yabause", "apply.py"),
        "patch_args": ["--emu-name=Kronos"],
        "toolchain": "msvc",
    },
}


def resolve_repo(spec, repo_override, use_upstream):
    """Which git URL to clone: explicit override > upstream (if --upstream) > our fork."""
    if repo_override:
        return repo_override
    if use_upstream:
        return spec["upstream"]
    return fork_url(spec["fork_name"])


def repo_reachable(url):
    """True if `git ls-remote` can read the repo (exists + accessible)."""
    try:
        return subprocess.call(["git", "ls-remote", url],
                               stdout=subprocess.DEVNULL,
                               stderr=subprocess.DEVNULL) == 0
    except Exception:
        return False


def choose_repo(rn, spec, repo_override, use_upstream):
    """Resolve the clone URL, preflighting our-fork reachability. If the fork doesn't
    exist yet, explain clearly and offer the upstream fallback (auto under --yes).
    Returns the URL to clone, or None if the user declines (skip this emulator)."""
    url = resolve_repo(spec, repo_override, use_upstream)
    # An explicit --*-repo / --upstream choice is used as-is; only the default
    # (our fork) is preflighted, since that's the one that may not exist yet.
    if rn.dry_run or repo_override or use_upstream:
        return url
    if repo_reachable(url):
        return url

    upstream = spec["upstream"]
    print(f"  [!] Our fork was not found: {url}")
    print(f"      Create it once (see Integration/INSTALL.md > \"Source: our forks\", or")
    print(f"      Integration/Mednafen/FORK_SETUP.md for Mednafen) — or build from upstream:")
    print(f"        {upstream}")
    if not repo_reachable(upstream):
        print("      (heads up: upstream is also unreachable — check your network / git.)")
    if rn.assume_yes or rn.confirm("      Build from upstream now?"):
        print(f"      -> using upstream: {upstream}")
        return upstream
    return None

# winget package IDs for the tools we may install on the user's behalf.
WINGET = {
    "git":   "Git.Git",
    "cmake": "Kitware.CMake",
    "vs":    "Microsoft.VisualStudio.2022.BuildTools",
    "msys2": "MSYS2.MSYS2",
}
# MSVC Build Tools needs the C++ workload added explicitly.
VS_OVERRIDE = ("--quiet --wait --norestart --nocache "
               "--add Microsoft.VisualStudio.Workload.VCTools --includeRecommended")
# MSYS2 dev packages Mednafen's autotools build needs.
MSYS2_PACKAGES = ("base-devel git autoconf automake libtool make pkgconf "
                  "mingw-w64-x86_64-gcc mingw-w64-x86_64-SDL2 mingw-w64-x86_64-zlib")


class Runner:
    """Executes (or, in dry-run, just prints) shell steps and tracks failure."""

    def __init__(self, dry_run, assume_yes):
        self.dry_run = dry_run
        self.assume_yes = assume_yes

    def confirm(self, prompt):
        if self.assume_yes or self.dry_run:
            return True
        try:
            return input(f"{prompt} [y/N] ").strip().lower() in ("y", "yes")
        except EOFError:
            return False   # non-interactive without --yes: treat as "no"

    def run(self, cmd, cwd=None, shell=False):
        pretty = cmd if isinstance(cmd, str) else " ".join(cmd)
        loc = f"  (in {cwd})" if cwd else ""
        if self.dry_run:
            print(f"    would run: {pretty}{loc}")
            return 0
        print(f"    $ {pretty}{loc}")
        try:
            return subprocess.call(cmd, cwd=cwd, shell=shell)
        except FileNotFoundError:
            # The executable isn't on PATH. The most common cause on Windows is that a
            # tool was just installed (winget) this run — Windows doesn't push the new
            # PATH into an already-running process — so name it and tell the user how
            # to recover instead of dumping a traceback.
            exe = (cmd[0] if isinstance(cmd, (list, tuple)) and cmd else str(cmd).split()[0])
            print(f"    *** '{exe}' was not found on PATH.")
            print(f"        If it was just installed, close this window and run install.bat")
            print(f"        again so the updated PATH takes effect. Otherwise install {exe}")
            print(f"        and make sure it's on PATH (for CMake, tick \"Add to PATH\" in its")
            print(f"        installer), then re-run.")
            return 127


def which(name):
    return shutil.which(name)


def refresh_windows_path():
    """Re-read PATH from the Windows registry (machine + user) into this process, so a
    tool winget installed THIS run is found by the build steps without reopening the
    terminal. No-op off Windows. Best-effort: on any failure the existing PATH stands."""
    if os.name != "nt":
        return
    try:
        import winreg
    except Exception:
        return
    parts = []
    for root, sub in ((winreg.HKEY_LOCAL_MACHINE,
                       r"SYSTEM\CurrentControlSet\Control\Session Manager\Environment"),
                      (winreg.HKEY_CURRENT_USER, "Environment")):
        try:
            with winreg.OpenKey(root, sub) as key:
                val, _ = winreg.QueryValueEx(key, "Path")
                if val:
                    parts.append(os.path.expandvars(val))
        except OSError:
            pass
    # Keep the current PATH too (it may hold entries the registry doesn't), de-duped.
    seen, merged = set(), []
    for p in os.pathsep.join(parts + [os.environ.get("PATH", "")]).split(os.pathsep):
        low = p.lower()
        if p and low not in seen:
            seen.add(low)
            merged.append(p)
    os.environ["PATH"] = os.pathsep.join(merged)


def find_vswhere():
    pf = os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")
    p = os.path.join(pf, "Microsoft Visual Studio", "Installer", "vswhere.exe")
    return p if os.path.isfile(p) else None


def detect_msvc():
    """Return the VS install path with the C++ toolset, or None."""
    vswhere = find_vswhere()
    if not vswhere:
        return None
    try:
        out = subprocess.check_output(
            [vswhere, "-latest", "-products", "*",
             "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
             "-property", "installationPath"],
            text=True, stderr=subprocess.DEVNULL).strip()
        return out or None
    except Exception:
        return None


def detect_msys2(explicit):
    for cand in filter(None, (explicit, r"C:\msys64", r"C:\tools\msys64",
                              os.environ.get("MSYS2_ROOT"))):
        bash = os.path.join(cand, "usr", "bin", "bash.exe")
        if os.path.isfile(bash):
            return cand
    return None


def win_to_msys(path):
    """C:\\Users\\x -> /c/Users/x  (MSYS2 path form)."""
    p = os.path.abspath(path).replace("\\", "/")
    if len(p) > 1 and p[1] == ":":
        p = "/" + p[0].lower() + p[2:]
    return p


def ensure_tool(rn, present, label, winget_id, override=None):
    """If `present` is falsy, offer to winget-install `winget_id`. Returns ok."""
    if present:
        print(f"  [ok]      {label}")
        return True
    print(f"  [missing] {label}")
    if not which("winget"):
        print(f"            winget not found — install {label} manually, then re-run.")
        return False
    if not rn.confirm(f"            install {label} via winget ({winget_id})?"):
        print(f"            skipped — install {label} yourself, then re-run.")
        return False
    cmd = ["winget", "install", "--id", winget_id, "-e",
           "--accept-package-agreements", "--accept-source-agreements"]
    if override:
        cmd += ["--override", override]
    return rn.run(cmd) == 0


# ---------------------------------------------------------------------------
# Steps
# ---------------------------------------------------------------------------
def build_saturn_explorer(rn, generator):
    print("\n== Saturn Explorer ==")
    build = os.path.join(SE_ROOT, "build")
    if rn.run(["cmake", "-B", build, "-S", SE_ROOT, "-G", generator, "-A", "x64"]) != 0:
        return False, None
    if rn.run(["cmake", "--build", build, "--config", "Release", "--parallel"]) != 0:
        return False, None
    exe = os.path.join(build, "bin", "Release", "SaturnExplorerFrontEnd.exe")
    return True, exe


def clone_and_patch(rn, key, spec, dest, rev_override, repo):
    rev = rev_override or spec["rev"]
    if not os.path.isdir(os.path.join(dest, ".git")):
        # Clone with autocrlf forced off: on a Windows box with a global
        # core.autocrlf=true, git would rewrite LF->CRLF in the checkout, which drifts
        # the source and can break apply.py's exact-anchor patching + the MSYS2 build.
        # (A fork with a committed `.gitattributes: * -text` is already protected; this
        # covers forks/upstreams that lack it.)
        if rn.run(["git", "-c", "core.autocrlf=false", "clone", repo, dest]) != 0:
            return False
    if rn.run(["git", "fetch", "--all", "--tags"], cwd=dest) != 0:
        return False
    if rn.run(["git", "checkout", rev], cwd=dest) != 0:
        return False
    patcher = os.path.join(SE_ROOT, spec["patch_subdir"])
    return rn.run([sys.executable, patcher, dest, *spec["patch_args"]]) == 0


def build_mednafen(rn, msys2, dest):
    bash = os.path.join(msys2, "usr", "bin", "bash.exe")
    msdir = win_to_msys(dest)
    ncpu = os.cpu_count() or 4
    # Mednafen ships include/mednafen as a symlink -> ../src, and the build reaches every
    # <mednafen/...> header through it. On Windows (core.symlinks=false) a clone can
    # materialize it as a plain file, breaking the include path.
    #
    # Test -L, not -d: plain MSYS `ln -s` silently makes a *copy* rather than a symlink
    # unless winsymlinks is enabled, and a copy satisfies -d. Guarding on -d would then
    # keep that copy forever, so a re-run whose apply.py re-patched src/ would compile
    # stale duplicates of the patched headers (apply.py installs se_export.h /
    # SeLiveProtocol.h into src/ss). -L is false for a copy, so the copy is rebuilt every
    # run and can never go stale. Prefer a real symlink (needs Developer Mode or admin);
    # fall back to the copy so machines without it still build. rm -rf, not rm -f, since
    # the stale case is a directory. git checkouts also need autotools bootstrapped
    # before ./configure exists.
    relink = ("rm -rf include/mednafen; "
              "MSYS=winsymlinks:nativestrict ln -s ../src include/mednafen 2>/dev/null || "
              "ln -s ../src include/mednafen")
    script = (f"cd '{msdir}' && "
              f"([ -L include/mednafen ] || {{ {relink}; }}) && "
              f"([ -x ./configure ] || (autoreconf -i || ./autogen.sh)) && "
              f"./configure --enable-debugger && make -j{ncpu}")
    env = "MSYSTEM=MINGW64"
    rc = rn.run([bash, "-lc", f"{env}; {script}"])
    exe = os.path.join(dest, "src", "mednafen.exe")
    return rc == 0, exe


def build_yabause(rn, dest, generator, qt_path):
    build = os.path.join(dest, "build")
    cfg = ["cmake", "-B", build, "-S", dest, "-G", generator, "-A", "x64",
           "-DYAB_PORTS=qt"]
    if qt_path:
        cfg.append(f"-DCMAKE_PREFIX_PATH={qt_path}")
    if rn.run(cfg) != 0:
        return False, None
    if rn.run(["cmake", "--build", build, "--config", "Release", "--parallel"]) != 0:
        return False, None
    return True, os.path.join(build, "src", "qt", "Release", "yabause-qt.exe")


def settings_ini_path():
    """Per-user settings.ini the viewer reads — MUST match FrontEnd/src/Settings.cpp:
      Windows : %APPDATA%\\SaturnExplorer\\settings.ini
      else    : $XDG_CONFIG_HOME/SaturnExplorer/settings.ini  (or ~/.config/...)
    """
    if os.name == "nt":
        base = os.environ.get("APPDATA") or os.path.join(
            os.path.expanduser("~"), "AppData", "Roaming")
    else:
        base = os.environ.get("XDG_CONFIG_HOME") or os.path.join(
            os.path.expanduser("~"), ".config")
    return os.path.join(base, "SaturnExplorer", "settings.ini")


def record_emulator_path(rn, name, exe):
    """Record a built emulator exe under [emulators] <name> in the viewer's
    settings.ini (read-modify-write so panel/data settings are preserved), so the
    viewer's 'Launch <emu>' button can start it. Best-effort: a failure here never
    fails the install."""
    if not exe:
        return
    exe = os.path.abspath(exe)
    path = settings_ini_path()
    if rn.dry_run:
        print(f"    would record {name} -> {exe}\n      in {path}")
        return
    if not os.path.isfile(exe):
        # The build reported success but the exe isn't where expected: skip quietly
        # rather than record a path that won't launch.
        print(f"    [!] {name} exe not found at {exe}; not recorded.")
        return
    try:
        cp = configparser.ConfigParser()
        cp.optionxform = str                      # preserve key case
        if os.path.exists(path):
            cp.read(path)
        if not cp.has_section("emulators"):
            cp.add_section("emulators")
        cp.set("emulators", name, exe)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w") as f:
            cp.write(f)
        print(f"    recorded {name} path for the viewer's Launch button:\n      {path}")
    except Exception as e:
        print(f"    (could not record {name} path: {e})")


def main():
    global FORK_OWNER
    ap = argparse.ArgumentParser(description="Saturn Explorer + patched-emulator setup (Windows).")
    ap.add_argument("--prefix", default=os.path.join(SE_ROOT, "_emu"),
                    help="where to clone+build emulators (default: <repo>/_emu)")
    ap.add_argument("--no-mednafen", action="store_true", help="skip Mednafen")
    ap.add_argument("--with-yabause", action="store_true", help="also build a Yabause fork")
    ap.add_argument("--yabause-variant", choices=("yabause", "sanshiro", "kronos"),
                    default="yabause", help="which Yabause-lineage fork (default: yabause)")
    ap.add_argument("--mednafen-rev", help="override the pinned Mednafen revision")
    ap.add_argument("--yabause-rev", help="override the pinned Yabause-fork revision")
    ap.add_argument("--fork-owner", default=FORK_OWNER,
                    help=f"GitHub owner holding our emulator forks (default: {FORK_OWNER})")
    ap.add_argument("--upstream", action="store_true",
                    help="clone the original upstream repos instead of our forks")
    ap.add_argument("--mednafen-repo", help="explicit Mednafen git URL (overrides fork/upstream)")
    ap.add_argument("--yabause-repo", help="explicit Yabause-fork git URL (overrides fork/upstream)")
    ap.add_argument("--se-only", action="store_true", help="build only Saturn Explorer")
    ap.add_argument("--msys2", help="path to an existing MSYS2 install (e.g. C:\\msys64)")
    ap.add_argument("--qt-path", help="Qt install dir for the Yabause build (CMAKE_PREFIX_PATH)")
    ap.add_argument("--generator", default="Visual Studio 17 2022",
                    help='CMake generator (default "Visual Studio 17 2022"; use "...16 2019")')
    ap.add_argument("--dry-run", action="store_true", help="print the plan; change nothing")
    ap.add_argument("--yes", action="store_true", help="don't prompt before installs/builds")
    args = ap.parse_args()
    FORK_OWNER = args.fork_owner

    if os.name != "nt" and not args.dry_run:
        print("This installer targets Windows. Use --dry-run to preview the plan elsewhere.")
        return 2

    rn = Runner(args.dry_run, args.yes)
    do_mednafen = not args.no_mednafen and not args.se_only
    do_yabause = args.with_yabause and not args.se_only

    print("Saturn Explorer installer (Windows)\n" + "=" * 36)
    if args.dry_run:
        print("(dry-run: nothing will be installed, cloned, or built)\n")

    # --- prerequisite check + assisted install -----------------------------
    print("Prerequisites:")
    ok = True
    ok &= ensure_tool(rn, which("git"), "git", WINGET["git"])
    ok &= ensure_tool(rn, which("cmake"), "CMake", WINGET["cmake"])
    ok &= ensure_tool(rn, detect_msvc(), "Visual Studio C++ toolset",
                      WINGET["vs"], override=VS_OVERRIDE)

    msys2 = None
    if do_mednafen:
        msys2 = detect_msys2(args.msys2)
        if ensure_tool(rn, msys2, "MSYS2 (for Mednafen)", WINGET["msys2"]):
            bash = os.path.join(msys2, "usr", "bin", "bash.exe") if msys2 else None
            if bash:
                print("  Installing MSYS2 build packages (SDL2, zlib, gcc, autotools)...")
                rn.run([bash, "-lc",
                        f"pacman -Syu --noconfirm && pacman -S --needed --noconfirm {MSYS2_PACKAGES}"])
        else:
            ok = False
        # After a fresh winget install (or in --dry-run) detection can't see it yet;
        # fall back to the default root so the build step still has a path to use.
        msys2 = msys2 or args.msys2 or r"C:\msys64"

    if not ok and not args.dry_run:
        print("\nSome prerequisites are missing. Install them (above) and re-run.")
        return 3

    # Anything winget installed above is now in the registry PATH but not this
    # process's; pull it in so the build steps can find cmake/git without a restart.
    if not args.dry_run:
        refresh_windows_path()
        if os.name == "nt" and not which("cmake"):
            print("\n  [!] 'cmake' still isn't on PATH. If you just installed it, close this")
            print("      window and run install.bat again so the new PATH takes effect.")
            print("      (CMake's installer has an \"Add CMake to the system PATH\" option;")
            print("      Visual Studio's bundled CMake is not on PATH by default.)")
            return 3

    # --- build Saturn Explorer ---------------------------------------------
    results = []
    se_ok, se_exe = build_saturn_explorer(rn, args.generator)
    results.append(("Saturn Explorer", se_ok, se_exe))

    # --- Mednafen -----------------------------------------------------------
    if do_mednafen:
        print("\n== Mednafen ==")
        dest = os.path.join(args.prefix, "mednafen")
        repo = choose_repo(rn, EMULATORS["mednafen"], args.mednafen_repo, args.upstream)
        if repo is None:
            print("  Skipped Mednafen (no source selected).")
            results.append(("Mednafen (skipped)", False, None))
        else:
            print(f"  source: {repo}")
            if clone_and_patch(rn, "mednafen", EMULATORS["mednafen"], dest, args.mednafen_rev, repo):
                m_ok, m_exe = build_mednafen(rn, msys2, dest)
            else:
                m_ok, m_exe = False, None
            if m_ok:
                record_emulator_path(rn, "mednafen", m_exe)
            results.append(("Mednafen (patched)", m_ok, m_exe))

    # --- Yabause fork -------------------------------------------------------
    if do_yabause:
        key = args.yabause_variant
        spec = EMULATORS[key]
        print(f"\n== {key} ==")
        dest = os.path.join(args.prefix, key)
        repo = choose_repo(rn, spec, args.yabause_repo, args.upstream)
        if repo is None:
            print(f"  Skipped {key} (no source selected).")
            results.append((f"{key} (skipped)", False, None))
        else:
            print(f"  source: {repo}")
            if clone_and_patch(rn, key, spec, dest, args.yabause_rev, repo):
                y_ok, y_exe = build_yabause(rn, dest, args.generator, args.qt_path)
            else:
                y_ok, y_exe = False, None
            if y_ok:
                # All Qt-lineage variants build yabause-qt.exe; record under one key
                # so the viewer's "Launch Yabause" button finds whichever was built.
                record_emulator_path(rn, "yabause", y_exe)
            results.append((f"{key} (patched)", y_ok, y_exe))

    # --- summary ------------------------------------------------------------
    print("\n" + "=" * 36 + "\nSummary:")
    for name, good, path in results:
        mark = "OK " if good else "FAIL"
        print(f"  [{mark}] {name}" + (f"\n         {path}" if path else ""))
    print("\nNext:")
    print("  * Supply a Saturn BIOS + disc images to the emulator (NOT included — copyrighted).")
    print("  * Launch the patched emulator, then Saturn Explorer with --live,")
    print("    or File -> Connect to emulator (live).")
    return 0 if all(g for _, g, _ in results) else 1


if __name__ == "__main__":
    sys.exit(main())
