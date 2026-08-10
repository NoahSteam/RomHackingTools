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
import textwrap

HERE = os.path.dirname(os.path.abspath(__file__))          # .../SaturnExplorer/Integration
SE_ROOT = os.path.dirname(HERE)                            # .../SaturnExplorer

# Host platform. Windows uses winget + MSVC + MSYS2/MinGW (below); macOS uses Homebrew
# + clang + native autotools. The reusable middle of the script (emulator registry,
# clone_and_patch, settings.ini recording, summary) is platform-agnostic.
IS_WIN = os.name == "nt"
IS_MAC = sys.platform == "darwin"

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
# MSYS2 dev packages Mednafen's autotools build needs. libFLAC is a HARD dependency of
# Mednafen's configure (CD-audio / music); SDL2 + zlib are the other required libs.
MSYS2_PACKAGES = ("base-devel git autoconf automake libtool make pkgconf "
                  "mingw-w64-x86_64-gcc mingw-w64-x86_64-SDL2 mingw-w64-x86_64-zlib "
                  "mingw-w64-x86_64-flac")

# The console cores Mednafen builds by default that Saturn Explorer never touches. With
# --mednafen-saturn-only we pass `--disable-<core>` for each at ./configure time, cutting
# most of the compile (PSX/SNES/MD alone dominate it); the Saturn core (`ss`) and the
# shared mednafen core still build. `ssfplay` is the Saturn Sound-Format *player*, a
# separate module from `ss` — safe to drop. The authoritative list is `./configure --help`
# on the checkout; an extra or renamed name here is harmless — autoconf ignores an
# unrecognized `--disable-*` with a warning rather than failing.
MEDNAFEN_OTHER_CORES = ("apple2", "demo", "gb", "gba", "lynx", "md", "nes", "ngp",
                        "pce", "pce-fast", "pcfx", "psx", "sms", "snes", "snes-faust",
                        "ssfplay", "vb", "wswan")


class Runner:
    """Executes shell steps with concise progress output and useful failure details."""

    def __init__(self, dry_run, assume_yes, verbose=False):
        self.dry_run = dry_run
        self.assume_yes = assume_yes
        self.verbose = verbose

    def confirm(self, prompt):
        if self.assume_yes or self.dry_run:
            return True
        try:
            return input(f"{prompt} [y/N] ").strip().lower() in ("y", "yes")
        except EOFError:
            return False   # non-interactive without --yes: treat as "no"

    @staticmethod
    def _print_wrapped_command(pretty, loc=""):
        command = pretty + loc
        lines = textwrap.wrap(command, width=100, break_long_words=False,
                              break_on_hyphens=False) or [command]
        print(f"      {lines[0]}")
        for line in lines[1:]:
            print(f"      {line}")

    def run(self, cmd, cwd=None, shell=False, env=None, description=None):
        pretty = cmd if isinstance(cmd, str) else " ".join(cmd)
        loc = f"  (in {cwd})" if cwd else ""
        detailed = self.verbose or (description is None and len(pretty) <= 160)
        if description is None:
            if isinstance(cmd, (list, tuple)) and cmd:
                description = f"Run {os.path.basename(str(cmd[0]))}"
            else:
                description = "Run command"
        if self.dry_run:
            if detailed:
                print(f"    would run: {pretty}{loc}")
            else:
                print(f"    would run: {description}")
            return 0
        if detailed:
            print(f"    $ {pretty}{loc}")
        else:
            print(f"    -> {description}...")
        try:
            rc = subprocess.call(cmd, cwd=cwd, shell=shell, env=env)
            if rc != 0:
                print(f"    [failed] exit code {rc}")
                if not detailed:
                    print("    Command:")
                    self._print_wrapped_command(pretty, loc)
            return rc
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


# ---------------------------------------------------------------------------
# Homebrew (macOS) — the assisted-install package manager, mirroring winget's role
# on Windows. Apple-silicon installs to /opt/homebrew, Intel to /usr/local; neither
# is on PATH for a fresh process until `brew shellenv` runs, so we locate the binary
# directly and add its bin dir to this process's PATH for the build steps.
# ---------------------------------------------------------------------------
def brew_bin():
    for c in (shutil.which("brew"), "/opt/homebrew/bin/brew", "/usr/local/bin/brew"):
        if c and os.path.isfile(c):
            return c
    return None


def brew_prefix(brew=None):
    brew = brew or brew_bin()
    if not brew:
        return None
    try:
        return subprocess.check_output([brew, "--prefix"], text=True).strip()
    except Exception:
        return os.path.dirname(os.path.dirname(brew))   # <prefix>/bin/brew -> <prefix>


def add_to_path(d):
    """Prepend a directory to this process's PATH (idempotent), so a tool Homebrew just
    installed is found by later build steps without reopening the terminal."""
    if not d or not os.path.isdir(d):
        return
    parts = os.environ.get("PATH", "").split(os.pathsep)
    if d not in parts:
        os.environ["PATH"] = d + os.pathsep + os.environ.get("PATH", "")


def ensure_homebrew(rn):
    """Return a path to `brew`, offering to run Homebrew's official installer if missing.
    The installer is https://brew.sh's own script; we only run it with explicit consent."""
    brew = brew_bin()
    if brew:
        return brew
    print("  [missing] Homebrew (macOS package manager)")
    if rn.dry_run:
        print("            would install Homebrew from https://brew.sh")
        return None
    if not rn.confirm("            install Homebrew now (runs the official installer from brew.sh)?"):
        print("            skipped — install it from https://brew.sh, then re-run.")
        return None
    installer = ('/bin/bash -c "$(curl -fsSL '
                 'https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"')
    rn.run(installer, shell=True, description="Install Homebrew (brew.sh installer)")
    return brew_bin()


def check_prereqs_macos(rn, do_mednafen, args):
    """macOS prerequisite check + assisted install via Homebrew (parity with the Windows
    winget/MSYS2 block). Resolves CMake into the CMAKE global. Returns ok."""
    global CMAKE
    print("Prerequisites:" + (" (incremental — skipping package installs)" if args.incremental else ""))
    ok = True

    if which("clang") or which("cc"):
        print("  [ok]      C/C++ compiler (clang)")
    else:
        print("  [missing] Xcode command-line tools (clang)")
        print("            Install them with:  xcode-select --install   then re-run.")
        ok = False

    if which("git"):
        print("  [ok]      git")
    else:
        print("  [missing] git  (comes with the Xcode command-line tools: xcode-select --install)")
        ok = False

    brew = brew_bin()
    if not brew and not args.incremental:
        brew = ensure_homebrew(rn)
    if brew:
        add_to_path(os.path.join(brew_prefix(brew), "bin"))
        print(f"  [ok]      Homebrew  ({brew})")
    elif not args.incremental:
        print("  [missing] Homebrew")
        ok = False

    # The package set: CMake always; Mednafen's native autotools build also needs
    # pkg-config + autoconf/automake/libtool (to bootstrap ./configure) and the SDL2 +
    # FLAC libraries (libFLAC is a hard dependency of Mednafen's configure). `brew install`
    # is idempotent, so re-runs on an already-provisioned machine are quick.
    if not args.incremental and brew:
        pkgs = ["cmake"]
        if do_mednafen:
            pkgs += ["autoconf", "automake", "libtool", "pkg-config", "sdl2", "flac"]
        print(f"  Installing build packages via Homebrew: {' '.join(pkgs)}")
        rn.run([brew, "install", *pkgs], description="Install Homebrew build packages")

    # Resolve cmake for the build steps (now on PATH after the brew install above).
    p = shutil.which("cmake")
    if p:
        CMAKE = p
        print("  [ok]      CMake" + (f"  ({p})" if p != "cmake" else ""))
    elif not args.dry_run:
        print("  [missing] CMake — not found after install. Install it (brew install cmake) and re-run.")
        ok = False

    return ok


# Absolute path to the cmake we build with. Resolved in main(); the build steps use
# it (not a bare "cmake") so a CMake that's installed but not on PATH still works.
CMAKE = "cmake"


def cmake_path():
    """Absolute path to cmake.exe, or None. Searches PATH first, then the common
    Windows install locations: CMake's own installer, the winget Links shim, and
    Visual Studio's bundled copy (none of which are on PATH by default)."""
    p = shutil.which("cmake")
    if p:
        return p
    if os.name != "nt":
        return None
    import glob
    cands = []
    for base in filter(None, (os.environ.get("ProgramW6432"),
                              os.environ.get("ProgramFiles"),
                              os.environ.get("ProgramFiles(x86)"))):
        cands.append(os.path.join(base, "CMake", "bin", "cmake.exe"))
    la = os.environ.get("LOCALAPPDATA")
    if la:
        cands.append(os.path.join(la, "Microsoft", "WinGet", "Links", "cmake.exe"))
    for pf in filter(None, (os.environ.get("ProgramFiles"), os.environ.get("ProgramFiles(x86)"))):
        cands += glob.glob(os.path.join(pf, "Microsoft Visual Studio", "*", "*", "Common7",
                                        "IDE", "CommonExtensions", "Microsoft", "CMake",
                                        "CMake", "bin", "cmake.exe"))
    for c in cands:
        if os.path.isfile(c):
            return c
    return None


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


def ensure_cmake(rn):
    """Resolve CMake robustly and record it in the CMAKE global the build uses. Handles
    the common Windows case where CMake IS installed but not on PATH: we locate its exe
    directly (cmake_path) rather than trusting PATH or winget's exit code (winget returns
    non-zero for 'already installed / no upgrade')."""
    global CMAKE
    p = cmake_path()
    if p:
        CMAKE = p
        print("  [ok]      CMake" + ("" if p == "cmake" else f"  ({p})"))
        return True
    print("  [missing] CMake")
    if rn.dry_run:
        return True
    if which("winget") and rn.confirm("            install CMake via winget (Kitware.CMake)?"):
        # Ignore winget's exit code (it's non-zero when already installed); re-resolve
        # the exe directly afterwards, refreshing PATH in case winget added it there.
        rn.run(["winget", "install", "--id", WINGET["cmake"], "-e",
                "--accept-package-agreements", "--accept-source-agreements"])
        refresh_windows_path()
        p = cmake_path()
        if p:
            CMAKE = p
            print(f"  [ok]      CMake  ({p})")
            return True
    print("            CMake wasn't found. Install it from https://cmake.org/download/")
    print("            (tick \"Add CMake to the system PATH\"), or if you just installed it,")
    print("            close this window and run install.bat again.")
    return False


# ---------------------------------------------------------------------------
# Steps
# ---------------------------------------------------------------------------
def build_saturn_explorer(rn, generator):
    print("\n== Saturn Explorer ==")
    build = os.path.join(SE_ROOT, "build")
    if IS_WIN:
        # Multi-config MSVC generator: pick the config at build time, not configure time.
        configure = [CMAKE, "-B", build, "-S", SE_ROOT, "-G", generator, "-A", "x64"]
        compile_ = [CMAKE, "--build", build, "--config", "Release", "--parallel"]
        exe = os.path.join(build, "bin", "Release", "SaturnExplorerFrontEnd.exe")
    else:
        # Ninja/Makefiles are single-config: bake the type in at configure time.
        configure = [CMAKE, "-B", build, "-S", SE_ROOT, "-DCMAKE_BUILD_TYPE=Release"]
        compile_ = [CMAKE, "--build", build, "--parallel"]
        # macOS builds a double-clickable SaturnExplorer.app (see CMakeLists APPLE branch);
        # Linux builds a bare SaturnExplorerFrontEnd executable.
        exe = (os.path.join(build, "bin", "SaturnExplorer.app") if IS_MAC
               else os.path.join(build, "bin", "SaturnExplorerFrontEnd"))
    if rn.run(configure, description="Configure Saturn Explorer") != 0:
        return False, None
    if rn.run(compile_, description="Build Saturn Explorer (Release)") != 0:
        return False, None
    return True, exe


def clone_and_patch(rn, key, spec, dest, rev_override, repo, skip_git=False):
    rev = rev_override or spec["rev"]
    have_checkout = os.path.isdir(os.path.join(dest, ".git"))
    # Incremental re-install with the tree already present: keep the existing checkout
    # untouched (no clone / fetch / checkout — that's network + could reset local edits)
    # and just re-run the patcher, which is idempotent and content-aware, so only the
    # sources that actually changed get their mtime bumped. If the tree isn't there yet,
    # fall through to a normal clone so --incremental still works on a first run.
    if skip_git and have_checkout:
        print("  (incremental: keeping the existing checkout; re-applying the patch)")
    else:
        if not have_checkout:
            # Clone with autocrlf forced off: on a Windows box with a global
            # core.autocrlf=true, git would rewrite LF->CRLF in the checkout, which drifts
            # the source and can break apply.py's exact-anchor patching + the MSYS2 build.
            # (A fork with a committed `.gitattributes: * -text` is already protected; this
            # covers forks/upstreams that lack it.)
            if rn.run(["git", "-c", "core.autocrlf=false", "clone", repo, dest],
                      description=f"Clone {key} sources") != 0:
                return False
        if rn.run(["git", "fetch", "--all", "--tags"], cwd=dest) != 0:
            return False
        if rn.run(["git", "checkout", rev], cwd=dest) != 0:
            return False
    patcher = os.path.join(SE_ROOT, spec["patch_subdir"])
    return rn.run([sys.executable, patcher, dest, *spec["patch_args"]],
                  description=f"Apply Saturn Explorer integration to {key}") == 0


def build_mednafen(rn, msys2, dest, configure_flags="", reconfigure=True):
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
    # ./configure normally re-runs every invocation (only the autotools bootstrap is
    # guarded), so a changed flag set is picked up without a manual `make distclean` — the
    # Makefiles regenerate and `make` rebuilds only what changed. (Switching an already-built
    # tree to saturn-only leaves the other cores' stale .o on disk; that's wasted disk, not
    # compile time, and `make distclean` reclaims it if wanted.)
    # A Windows *NT* build must define UNICODE/_UNICODE. Mednafen treats a non-UNICODE
    # Windows build as its special Windows 98/98SE/Me variant: src/drivers/main.cpp does
    #     #if defined(WIN32) && !defined(UNICODE)
    #     if(!(GetVersion() & 0x80000000)) { ...error...; return -1; }
    # so on any NT-based Windows it pops "This special build of Mednafen is intended for
    # use on Windows 98..." and exits before doing anything. Upstream's own Windows build
    # script (mswin/build-mednafen.sh) passes exactly these two defines.
    cppflags = "-DUNICODE=1 -D_UNICODE=1"
    configure = ("./configure --enable-debugger" +
                 (f" {configure_flags}" if configure_flags else "") +
                 f' CPPFLAGS="{cppflags}"')
    bootstrap = "([ -x ./configure ] || (autoreconf -i || ./autogen.sh))"
    if reconfigure:
        # `make` tracks source timestamps, NOT compiler-flag changes: after a reconfigure
        # that changes defines (e.g. adding -DUNICODE), every .o still looks "up to date"
        # against its .cpp, so make would relink stale objects built with the OLD flags and
        # report success while the new flags had no effect. Clean so they actually rebuild.
        cfg = f"{bootstrap} && {configure} && make clean && "
    else:
        # Incremental: only bootstrap + configure when the tree isn't configured yet (no
        # config.status); otherwise skip straight to `make` so only the objects whose
        # sources changed are recompiled and relinked. Combined with apply.py's
        # content-aware copy, an unchanged Integration/ folder means `make` finds nothing
        # to do; a single edited file rebuilds just that object.
        cfg = f"([ -f config.status ] || {{ {bootstrap} && {configure}; }}) && "
    # Link-time fixes for building this (older, win9x-era) mednafen against a current
    # MSYS2/mingw-w64 toolchain. Both are needed for a *working* exe, and both append
    # after mednafen's own AM_LDFLAGS (-no-pie), so nothing it sets is clobbered:
    #   * -Wl,--defsym,mingw_app_type=__mingw_app_type
    #       mednafen's src/drivers/main.cpp references the CRT global `mingw_app_type`
    #       (console/GUI toggle). Modern mingw-w64 renamed it to `__mingw_app_type`, so
    #       the old name is undefined at link ("undefined reference to `mingw_app_type'").
    #       Aliasing the old name to the new one resolves it without patching mednafen.
    #   * -static-libstdc++ -static-libgcc
    #       Otherwise C++ exception RTTI is auto-imported as *data* from libstdc++-6.dll.
    #       With ASLR loading that DLL >4 GB from the exe, the 32-bit auto-import
    #       pseudo-relocation overflows and the program aborts at startup ("Mingw-w64
    #       runtime failure: 32 bit pseudo relocation ... out of range"). Linking the C++
    #       runtime statically removes the cross-DLL data import.
    ldflags = ("-static-libstdc++ -static-libgcc "
               "-Wl,--defsym,mingw_app_type=__mingw_app_type")
    # mednafen.exe is a MinGW build, so it needs the MinGW runtime DLLs (SDL2, FLAC,
    # ogg, iconv, intl, zlib, winpthread). Those live in C:\msys64\mingw64\bin, which is
    # NOT on PATH for a normal Windows process — so double-clicking it, or launching it
    # from Saturn Explorer's "Launch" button, fails with "libFLAC.dll was not found" etc.
    # Copy the real dependency closure (per `ldd`) next to the exe; Windows searches the
    # exe's own directory first, so it then runs anywhere with no MSYS2 on PATH.
    # Several passes so dependencies-of-dependencies are picked up as well.
    bundle = (
        'cd src && for i in 1 2 3 4; do '
        'for b in mednafen.exe *.dll; do '
        '[ -e "$b" ] && ldd "$b" 2>/dev/null '
        r"| grep -io '/mingw64/bin/[^ ]*\.dll' "
        '| while read d; do [ -e "$(basename "$d")" ] || cp "$d" .; done; '
        'done; done; '
        # Autotools links in src/, but Saturn Explorer treats the checkout root as
        # the portable Mednafen install directory (firmware/, config, saves). Keep
        # the build product in src/ for make, and publish the runnable exe + DLL
        # closure one level up for users and the frontend launcher.
        'for b in mednafen.exe *.dll; do if [ -e "$b" ]; then cp -f "$b" .. || exit 1; fi; done; '
        'test -f ../mednafen.exe && mkdir -p ../firmware'
    )
    script = (f"cd '{msdir}' && "
              f"([ -L include/mednafen ] || {{ {relink}; }}) && "
              f"{cfg}make -j{ncpu} LDFLAGS='{ldflags}' && {bundle}")
    # MSYSTEM must be in the ENVIRONMENT before bash's login profile (-l) runs, so the
    # MINGW64 setup is applied — putting /mingw64/bin (gcc) on PATH and setting
    # PKG_CONFIG_PATH so ./configure finds SDL2/zlib. Setting it as the first command
    # inside `-lc` (the old code) was too late: the profile had already run, so no C
    # compiler was on PATH. Also export /mingw64/bin explicitly as a belt-and-suspenders
    # fallback for a stripped-down profile.
    env = {**os.environ, "MSYSTEM": "MINGW64", "CHERE_INVOKING": "1"}
    rc = rn.run([bash, "-lc", f"export PATH=/mingw64/bin:$PATH && {script}"], env=env,
                description="Compile and package Mednafen")
    exe = os.path.join(dest, "mednafen.exe")
    return rc == 0, exe


def build_mednafen_unix(rn, dest, configure_flags="", reconfigure=True):
    """Native Mednafen build on macOS/Linux: plain autotools, no MSYS2/MinGW. None of the
    Windows-specific handling (UNICODE defines, mingw_app_type aliasing, static libstdc++,
    DLL bundling) applies — clang links the system/Homebrew dylibs directly, and macOS finds
    them at runtime via their install names. The binary lands at <dest>/src/mednafen."""
    ncpu = os.cpu_count() or 4
    env = {**os.environ}
    prefix = brew_prefix() if IS_MAC else None
    if prefix:
        # Point configure at Homebrew's headers/libs (SDL2, FLAC) and .pc files. Needed on
        # Apple-silicon (/opt/homebrew) and Intel (/usr/local) since neither is a default
        # compiler/pkg-config search path.
        env["CPPFLAGS"] = (f"-I{prefix}/include " + env.get("CPPFLAGS", "")).strip()
        env["LDFLAGS"] = (f"-L{prefix}/lib " + env.get("LDFLAGS", "")).strip()
        env["PKG_CONFIG_PATH"] = os.pathsep.join(
            filter(None, [f"{prefix}/lib/pkgconfig", env.get("PKG_CONFIG_PATH", "")]))
    configure = ("./configure --enable-debugger" +
                 (f" {configure_flags}" if configure_flags else ""))
    # A git checkout ships no generated ./configure; bootstrap it first. glibtoolize (from
    # Homebrew's libtool) is what autoreconf calls on macOS.
    bootstrap = "([ -x ./configure ] || autoreconf -i || ./autogen.sh)"
    if reconfigure:
        # `make` tracks source mtimes, not flag changes, so a changed --disable-* set would
        # otherwise relink stale objects; clean to force the rebuild (mirrors the Win path).
        cfg = f"{bootstrap} && {configure} && make clean && "
    else:
        # Incremental: configure only when the tree isn't configured yet; else straight to
        # make, so only the objects whose sources apply.py touched get recompiled.
        cfg = f"([ -f config.status ] || {{ {bootstrap} && {configure}; }}) && "
    script = f"cd '{dest}' && {cfg}make -j{ncpu}"
    rc = rn.run(["/bin/sh", "-c", script], env=env,
                description="Configure and build Mednafen")
    exe = os.path.join(dest, "src", "mednafen")
    return rc == 0, exe


def build_yabause(rn, dest, generator, qt_path):
    build = os.path.join(dest, "build")
    cfg = [CMAKE, "-B", build, "-S", dest, "-G", generator, "-A", "x64",
           "-DYAB_PORTS=qt"]
    if qt_path:
        cfg.append(f"-DCMAKE_PREFIX_PATH={qt_path}")
    if rn.run(cfg, description="Configure Yabause") != 0:
        return False, None
    if rn.run([CMAKE, "--build", build, "--config", "Release", "--parallel"],
              description="Build Yabause (Release)") != 0:
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
    ap = argparse.ArgumentParser(description="Saturn Explorer + patched-emulator setup (Windows/macOS).")
    ap.add_argument("--prefix", default=os.path.join(SE_ROOT, "_emu"),
                    help="where to clone+build emulators (default: <repo>/_emu)")
    ap.add_argument("--no-mednafen", action="store_true", help="skip Mednafen")
    ap.add_argument("--mednafen-saturn-only", action="store_true",
                    help="build only the Saturn core (--disable-* every other console) "
                         "for a much faster Mednafen compile")
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
    ap.add_argument("--incremental", "--update", dest="incremental", action="store_true",
                    help="iterative rebuild: keep the existing emulator checkout, re-apply "
                         "the idempotent content-aware patch, and rebuild only what changed "
                         "(skips prerequisite package installs and skips ./configure when the "
                         "tree is already configured). Use after editing the Integration/ folder.")
    ap.add_argument("--msys2", help="path to an existing MSYS2 install (e.g. C:\\msys64)")
    ap.add_argument("--qt-path", help="Qt install dir for the Yabause build (CMAKE_PREFIX_PATH)")
    ap.add_argument("--generator", default="Visual Studio 17 2022",
                    help='CMake generator (default "Visual Studio 17 2022"; use "...16 2019")')
    ap.add_argument("--dry-run", action="store_true", help="print the plan; change nothing")
    ap.add_argument("--verbose", action="store_true",
                    help="print complete commands (including generated MSYS2 scripts)")
    ap.add_argument("--yes", action="store_true", help="don't prompt before installs/builds")
    args = ap.parse_args()
    FORK_OWNER = args.fork_owner

    if not IS_WIN and not IS_MAC and not args.dry_run:
        print("This installer supports Windows and macOS. Use --dry-run to preview the plan elsewhere.")
        return 2

    rn = Runner(args.dry_run, args.yes, args.verbose)
    do_mednafen = not args.no_mednafen and not args.se_only
    do_yabause = args.with_yabause and not args.se_only

    host = "Windows" if IS_WIN else ("macOS" if IS_MAC else "this platform")
    print(f"Saturn Explorer installer ({host})\n" + "=" * 36)
    if args.dry_run:
        print("(dry-run: nothing will be installed, cloned, or built)\n")

    # --- prerequisite check + assisted install -----------------------------
    # Incremental re-installs assume the toolchain is already in place: we still resolve
    # the tool PATHs the build needs, but skip the winget/brew/pacman package installs (the
    # slow, network-bound steps) so a re-run after an Integration/ edit is fast.
    msys2 = None
    if IS_WIN:
        print("Prerequisites:" + (" (incremental — skipping package installs)" if args.incremental else ""))
        ok = True
        ok &= ensure_tool(rn, which("git"), "git", WINGET["git"])
        ok &= ensure_cmake(rn)   # resolves an off-PATH CMake too; sets the CMAKE global
        ok &= ensure_tool(rn, detect_msvc(), "Visual Studio C++ toolset",
                          WINGET["vs"], override=VS_OVERRIDE)
        if do_mednafen:
            msys2 = detect_msys2(args.msys2)
            if ensure_tool(rn, msys2, "MSYS2 (for Mednafen)", WINGET["msys2"]):
                bash = os.path.join(msys2, "usr", "bin", "bash.exe") if msys2 else None
                if bash and not args.incremental:
                    print("  Installing MSYS2 build packages (SDL2, zlib, FLAC, gcc, autotools)...")
                    rn.run([bash, "-lc",
                            f"pacman -Syu --noconfirm && pacman -S --needed --noconfirm {MSYS2_PACKAGES}"],
                           description="Install MSYS2 build packages")
            else:
                ok = False
            # After a fresh winget install (or in --dry-run) detection can't see it yet;
            # fall back to the default root so the build step still has a path to use.
            msys2 = msys2 or args.msys2 or r"C:\msys64"
    else:
        # macOS (and, best-effort, other Unix): Homebrew + clang + native autotools.
        ok = check_prereqs_macos(rn, do_mednafen, args)

    if not ok and not args.dry_run:
        print("\nSome prerequisites are missing. Install them (above) and re-run.")
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
            cfg_flags = ""
            if args.mednafen_saturn_only:
                cfg_flags = " ".join(f"--disable-{c}" for c in MEDNAFEN_OTHER_CORES)
                print("  (Saturn-only: disabling every other console core for a faster build)")
            # Incremental keeps the checkout and skips ./configure — but if the caller
            # also passed --mednafen-saturn-only, force a reconfigure so the new
            # --disable-* flags actually take effect (they only matter at configure time).
            reconfigure = (not args.incremental) or bool(cfg_flags)
            if clone_and_patch(rn, "mednafen", EMULATORS["mednafen"], dest,
                               args.mednafen_rev, repo, skip_git=args.incremental):
                if IS_WIN:
                    m_ok, m_exe = build_mednafen(rn, msys2, dest, cfg_flags, reconfigure=reconfigure)
                else:
                    m_ok, m_exe = build_mednafen_unix(rn, dest, cfg_flags, reconfigure=reconfigure)
            else:
                m_ok, m_exe = False, None
            if m_ok:
                record_emulator_path(rn, "mednafen", m_exe)
            results.append(("Mednafen (patched)", m_ok, m_exe))

    # --- Yabause fork -------------------------------------------------------
    if do_yabause and not IS_WIN:
        # The Yabause (Qt) build path is Windows-only for now; the macOS/Linux toolchain
        # branch here hasn't been written or verified. Skip it explicitly rather than run
        # the MSVC-shaped build steps.
        print("\n== Yabause ==")
        print("  Yabause is not yet supported by this installer on macOS/Linux; skipping.")
        print("  (Saturn Explorer + Mednafen build normally.)")
        results.append(("Yabause (unsupported on this OS)", False, None))
    elif do_yabause:
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
            # CMake is already incremental (cmake --build only rebuilds changed objects);
            # incremental just keeps the checkout and re-applies the content-aware patch.
            if clone_and_patch(rn, key, spec, dest, args.yabause_rev, repo,
                               skip_git=args.incremental):
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
    fw = ("~/.mednafen/firmware" if IS_MAC else
          "~/.mednafen/firmware" if not IS_WIN else "Mednafen's firmware folder")
    print("\nNext:")
    print(f"  * Put sega_101.bin + mpr-17933.bin in {fw} (NOT included — copyrighted).")
    print("  * Launch the patched emulator, then Saturn Explorer with --live,")
    print("    or File -> Connect to emulator (live).")
    return 0 if all(g for _, g, _ in results) else 1


if __name__ == "__main__":
    sys.exit(main())
