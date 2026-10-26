#!/usr/bin/env python3
"""
package_release.py - assemble a downloadable Saturn Explorer + patched Mednafen archive.

Takes the two builds that already exist (it builds nothing) and lays them out the way
FrontEnd/src/Launcher.cpp expects to find them:

  macOS                                    Windows
  SaturnExplorer-<ref>-macos/              SaturnExplorer-<ref>-windows/
    SaturnExplorer.app                       SaturnExplorerFrontEnd.exe  (+ licenses/)
    mednafen-se/                             mednafen-se/
      mednafen + lib/*.dylib (relinked)        mednafen.exe + *.dll
      firmware/   (the user adds the BIOS)     firmware/
      README-FORK.txt                          README-FORK.txt
    Licenses/   SOURCE.txt                   (licenses/ beside the exe holds both sets)   SOURCE.txt
    mednafen-se-source.tar.xz  README.txt    mednafen-se-source.tar.xz  README.txt
    third-party-source/ (copyleft libraries'   (+ the MSVC runtime DLLs beside the exe)
    source, macOS)

Before anything is copied it checks that the Mednafen binary really is what the source offer will describe
(pinned commit, patch current, Saturn-only build, clean Integration/, installed library versions); see
verify_provenance. The Mednafen folder sits BESIDE the app, not inside it: a macOS bundle is sealed by its signature
and the user has to be able to add the firmware/ folder. It is then smoke-tested from where it
sits (no dependency on Homebrew or MSYS2 left, the app's --selftest, Mednafen starts) and archived
(.dmg on macOS, .zip elsewhere).

  # macOS: an app built with -DSE_MACOS_BUNDLE_LIBS=ON and installed (see BUILD.md), and the Mednafen build
  python3 Integration/package_release.py --se-app stage/SaturnExplorer.app \\
          --mednafen _emu/mednafen/src/mednafen --ref v1.0 --out dist

  # Windows: build\\bin\\Release (the exe and its licenses\\ folder) and the Mednafen checkout root
  python Integration\\package_release.py --se-app build\\bin\\Release --mednafen _emu\\mednafen --ref v1.0 --out dist

--sign-identity "Developer ID Application: ..." signs the relinked Mednafen (macOS); the default is
ad hoc. Notarization is still a separate step on the finished .dmg (Integration/DISTRIBUTION.md).
"""
import argparse
import glob
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
SE_ROOT = os.path.dirname(HERE)
IS_MAC = sys.platform == "darwin"
IS_WIN = os.name == "nt"

FIRMWARE_FILES = ("sega_101.bin", "mpr-17933.bin")
SHIPPED_PROGRAMS = ("SaturnExplorerFrontEnd.exe",)      # the Windows/Linux build folder's other programs are tests and tools


def run(cmd, **kw):
    return subprocess.run(cmd, check=True, **kw)


def out(cmd):
    return subprocess.run(cmd, check=True, capture_output=True, text=True).stdout


# --------------------------------------------------------------------------- macOS: relink dylibs
def macho_id(path):
    """The install name a dylib declares for itself (LC_ID_DYLIB), or None for an executable."""
    lines = out(["otool", "-D", path]).splitlines()
    return lines[1].strip() if len(lines) > 1 and lines[1].strip() else None


def macho_deps(path):
    """Install names `path` loads (its LC_LOAD_DYLIB-style commands). `otool -L` also lists a dylib's own
    id among them, so that one line is dropped: it is a name, not a dependency."""
    deps = [l.strip().split(" (compatibility")[0] for l in out(["otool", "-L", path]).splitlines()[1:] if l.strip()]
    ident = macho_id(path)
    if ident in deps:
        deps.remove(ident)
    return deps


def macho_rpaths(path):
    return re.findall(r"cmd LC_RPATH\n\s+cmdsize \d+\n\s+path (.+?) \(offset", out(["otool", "-l", path]))


def is_system(dep):
    return dep.startswith(("/usr/lib/", "/System/"))


def resolve_dep(dep, owners, exe):
    """The file `dep` names. `owners` are the ORIGINAL paths of the image that loads it, then of the image
    that loaded that one, and so on: @loader_path is relative to the first, and @rpath is searched through
    the runpaths of every image in the chain and the executable, as dyld does. Anything that does not resolve
    to a file is an error, so a library can never be silently left out."""
    def expand(path, base):
        return (path.replace("@loader_path", os.path.dirname(base))
                    .replace("@executable_path", os.path.dirname(exe)))
    candidates = []
    if dep.startswith("@rpath/"):
        for owner in list(owners) + [exe]:
            candidates += [os.path.join(expand(rp, owner), dep[len("@rpath/"):]) for rp in macho_rpaths(owner)]
    else:
        candidates.append(expand(dep, owners[0]))
    for c in candidates:
        if os.path.exists(c):
            return os.path.realpath(c)
    raise SystemExit(f"cannot resolve {dep} needed by {owners[0]}")


def bundle_dylibs(exe, exe_origin, libdir, sign):
    """Copy every non-system dylib `exe` needs (transitively) into libdir, point the load commands at
    the copies, drop the build machine's rpaths, and re-sign: install_name_tool breaks the signature and
    an arm64 binary with a broken one is killed on launch. `exe_origin` is where the executable was built,
    which is what its @loader_path / @rpath entries are relative to. Returns the original paths copied."""
    os.makedirs(libdir, exist_ok=True)
    copied = {}                               # original real path -> file name in libdir
    owners = {exe: [exe_origin]}              # file in the package -> original paths of it and its loaders
    items, queue = [exe], [exe]

    def add(real, name, loader):
        if name in copied.values():
            raise SystemExit(f"two different libraries are both called {name}")
        dest = os.path.join(libdir, name)
        shutil.copy2(real, dest)
        os.chmod(dest, 0o755)
        copied[real] = name
        owners[dest] = [real] + owners[loader]
        queue.append(dest)
        items.append(dest)
        return dest

    while queue:
        cur = queue.pop()
        for dep in macho_deps(cur):
            if is_system(dep):
                continue
            real = resolve_dep(dep, owners[cur], exe_origin)
            if real in copied:
                continue
            add(real, os.path.basename(real), cur)
            # Homebrew's SDL2 is sdl2-compat: a shim that dlopens libSDL3 from its own folder.
            # Load commands never name SDL3, so nothing above finds it; mirror what SE's own
            # bundle does (CMakeLists.txt, SE_MACOS_BUNDLE_LIBS) and copy it in beside libSDL2.
            with open(real, "rb") as f:
                if os.path.basename(real).startswith("libSDL2") and b"libSDL3" in f.read():
                    sdl3 = next((p for p in ("/opt/homebrew/lib/libSDL3.dylib", "/usr/local/lib/libSDL3.dylib")
                                 if os.path.exists(p)), None)
                    if not sdl3:
                        raise SystemExit(f"{real} is sdl2-compat, which loads SDL3 at run time, "
                                         "but no libSDL3.dylib was found to bundle (brew install sdl3)")
                    add(os.path.realpath(sdl3), "libSDL3.dylib", os.path.join(libdir, os.path.basename(real)))
    quiet = {"stderr": subprocess.DEVNULL}   # install_name_tool warns the signature is now invalid; re-signed below
    for item in items:
        in_lib = os.path.dirname(item) == libdir
        if in_lib:
            run(["install_name_tool", "-id", "@loader_path/" + os.path.basename(item), item], **quiet)
        for dep in macho_deps(item):
            if is_system(dep):
                continue
            name = copied[resolve_dep(dep, owners[item], exe_origin)]
            run(["install_name_tool", "-change", dep,
                 ("@loader_path/" if in_lib else "@executable_path/lib/") + name, item], **quiet)
        for rp in macho_rpaths(item):
            run(["install_name_tool", "-delete_rpath", rp, item], **quiet)
    for item in items[1:] + items[:1]:          # libraries first, the executable last
        codesign(item, sign)
    return sorted(copied)


def codesign(path, identity):
    cmd = ["codesign", "--force", "--sign", identity]
    if identity != "-":
        cmd += ["--timestamp", "--options", "runtime"]
    run(cmd + [path], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)


# --------------------------------------------------------------------------- text files
def readme_fork(version, ref):
    return f"""Mednafen {version} - Saturn Explorer build ({ref})
{'=' * 60}

This is NOT an official Mednafen release. It is Mednafen {version} with changes that let Saturn
Explorer read and control the emulator (a local debugging endpoint, breakpoints, pause/step, rewind).
Do not report problems with it to the Mednafen project; report them to Saturn Explorer.

Source: see SOURCE.txt (and mednafen-se-source.tar.xz) in the folder above this one.
Licences: see the Licenses folder in the folder above this one.

Saturn BIOS
-----------
Put your own BIOS images in the firmware/ folder next to this file:
    {FIRMWARE_FILES[0]}
    {FIRMWARE_FILES[1]}
They are copyrighted and are not included. Saturn Explorer launches this emulator and tells it to read
the BIOS from that folder.

Security
--------
While running, this build listens for Saturn Explorer on a local socket (and loopback TCP port 6845).
Anything on the same machine that can connect can read and write the emulated machine's memory. Do not
expose that port to a network.
"""


def readme_top(ref, mac):
    where = "Keep SaturnExplorer.app and the mednafen-se folder side by side (for example both in\nApplications)." if mac else \
            ("Keep SaturnExplorerFrontEnd.exe and the mednafen-se folder side by side, in a folder whose path\n"
             "uses only characters your Windows code page can represent (plain English letters are always safe).")
    return f"""Saturn Explorer {ref}
{'=' * (17 + len(ref))}

1. {where}
2. Put your Saturn BIOS images in mednafen-se/firmware/ (see mednafen-se/README-FORK.txt).
3. Open Saturn Explorer, pick a game and use Launch.

mednafen-se is a modified Mednafen build, not an official release. Its source and the licences of
everything in this download are in SOURCE.txt, mednafen-se-source.tar.xz and Licenses/.
"""


# --------------------------------------------------------------------------- assembly
def merge_licenses(stage, mac):
    """The repo's Licenses/ (the bundled-Mednafen case) goes beside the app on macOS, where the app's own
    texts are sealed inside the bundle. Elsewhere it is merged into the licenses/ folder the SE build
    staged beside the exe -- one folder, because that is where the About box says to look and because
    Licenses/ and licenses/ would be the same folder on a case-insensitive filesystem."""
    if mac:
        shutil.copytree(os.path.join(SE_ROOT, "Licenses"), os.path.join(stage, "Licenses"))
        return
    dest = os.path.join(stage, "licenses")
    if not os.path.isdir(dest):
        raise SystemExit("the SE build has no licenses/ folder; build it first (it stages one beside the exe)")
    shutil.copytree(os.path.join(SE_ROOT, "Licenses"), dest, dirs_exist_ok=True)


# --------------------------------------------------------------------------- provenance
def checkout_root(mednafen):
    """The Mednafen checkout a binary was built in: <root>/src/mednafen (Unix) or <root>/mednafen.exe."""
    path = os.path.abspath(mednafen)
    d = path if os.path.isdir(path) else os.path.dirname(path)
    return os.path.dirname(d) if os.path.basename(d) == "src" and os.path.isdir(os.path.join(os.path.dirname(d), ".git")) else d


# Files the autotools bootstrap and ./configure regenerate in a checkout; they differ from the tarball's
# copies without being part of the SE patch, so the source comparison ignores them. Makefile.am, which
# decides what is compiled, is NOT in this list and is compared.
GENERATED_NAMES = {"Makefile.in", "configure", "aclocal.m4", "config.h.in", "config.h", "ltmain.sh", "libtool",
                   "compile", "depcomp", "install-sh", "missing", "config.guess", "config.sub", "stamp-h.in",
                   "stamp-h1", "test-driver"}
SOURCE_SUFFIXES = (".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".inc", ".s", ".S", ".m", ".mm", ".def", ".am")
SKIP_DIRS = {".git", ".deps", ".libs", "autom4te.cache"}


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def compare_trees(fresh, root, binary):
    """Compare a build checkout (`root`) against the tree it should be (`fresh`: the pinned commit with
    apply.py run on it). Reports: a source file that differs or is missing; a source file in the checkout that
    the pinned base plus the patch does not have (it could be compiled in); a symlink that points somewhere
    else (include/mednafen redirects every #include "mednafen/..."); and any compared file newer than the
    binary. Autotools-generated files are not compared, so this shows the sources match, not the whole tree."""
    problems = []
    known = set()
    newest = 0.0

    def dir_problems(want, have, label):
        """A symlinked directory in `fresh` against a real directory in `root` (a checkout that copies what Unix
        links): every nested source file must match the link's target, and no extra source may sit in the copy."""
        found = []
        wanted = set()
        for dp, dns, fns in os.walk(want):
            for f in fns:
                src = os.path.join(dp, f)
                rel = os.path.relpath(src, want)
                if f in GENERATED_NAMES or f.endswith(".m4") or os.path.islink(src):
                    continue
                wanted.add(rel)
                dst = os.path.join(have, rel)
                if not os.path.isfile(dst):
                    found.append(f"{label}/{rel} is missing from the copy")
                elif sha256_file(dst) != sha256_file(src):
                    found.append(f"{label}/{rel} differs from the link's target in the pinned base")
        for dp, dns, fns in os.walk(have):
            dns[:] = [d for d in dns if d not in SKIP_DIRS]
            for f in fns:
                rel = os.path.relpath(os.path.join(dp, f), have)
                if (f.endswith(SOURCE_SUFFIXES) and f not in GENERATED_NAMES and rel not in wanted
                        and not os.path.islink(os.path.join(dp, f))):
                    found.append(f"{label}/{rel} is in the copy but not in the link's target in the pinned base")
        return found

    for dirpath, dirs, files in os.walk(fresh):
        for name in list(dirs) + files:
            want = os.path.join(dirpath, name)
            if not os.path.islink(want):
                continue
            rel = os.path.relpath(want, fresh)
            have = os.path.join(root, rel)
            if os.path.islink(have):
                if os.readlink(have) != os.readlink(want):
                    problems.append(f"{rel} points to {os.readlink(have)}, not {os.readlink(want)}")
            elif os.path.isdir(have) and os.path.isdir(want):
                problems += dir_problems(os.path.realpath(want), have, rel)
            else:
                problems.append(f"{rel} is missing or is not the link the pinned base has")
            dirs[:] = [d for d in dirs if d != name]      # do not walk through the link
        for f in files:
            want = os.path.join(dirpath, f)
            if os.path.islink(want) or f in GENERATED_NAMES or f.endswith(".m4"):
                continue
            rel = os.path.relpath(want, fresh)
            known.add(rel)
            have = os.path.join(root, rel)
            if not os.path.isfile(have):
                problems.append(f"{rel} is missing from the build checkout")
            elif sha256_file(have) != sha256_file(want):
                problems.append(f"{rel} differs from the pinned base plus the current patch")
            else:
                newest = max(newest, os.path.getmtime(have))
    # config.status links include/trio/*.h and friends to the sources they name; where ln -s fails (MSYS2
    # without symlink privilege) it copies instead. A copy of a file the pinned tree has holds no code the
    # pinned tree lacks, so an extra file is only reported when no file of that name has its contents.
    pinned = {}                  # file name -> its paths in `fresh`, filled on first use

    def is_copy_of_pinned(have, name):
        if not pinned:
            for dp, _dns, fns in os.walk(fresh):
                for g in fns:
                    pinned.setdefault(g, []).append(os.path.join(dp, g))
        digest = sha256_file(have)
        return any(sha256_file(p) == digest for p in pinned.get(name, ()) if os.path.isfile(p))

    for dirpath, dirs, files in os.walk(root):
        dirs[:] = [d for d in dirs if d not in SKIP_DIRS]
        for f in files:
            have = os.path.join(dirpath, f)
            rel = os.path.relpath(have, root)
            if (f.endswith(SOURCE_SUFFIXES) and f not in GENERATED_NAMES and not os.path.islink(have)
                    and rel not in known and not os.path.exists(os.path.join(fresh, rel))
                    and not is_copy_of_pinned(have, f)):
                problems.append(f"{rel} is in the build checkout but not in the pinned base plus the patch")
    if os.path.exists(binary) and newest > os.path.getmtime(binary):
        problems.append("a source file is newer than the binary, so the binary may predate it; rebuild")
    return problems


def compare_to_patched_tree(up, root, binary, mso):
    work = tempfile.mkdtemp(prefix="se_prov_")
    try:
        fresh = os.path.join(work, "tree")
        mso.make_patched_tree(up, root, fresh, work)
        return compare_trees(fresh, root, binary)
    finally:
        shutil.rmtree(work, ignore_errors=True)


def verify_provenance(up, mednafen, mso, allow):
    """Refuse to label freshly regenerated source as the source of a binary it may not belong to.
    The source offer is rebuilt from UPSTREAM.json and the patch files in this checkout, so the binary
    must have come from exactly those: the pinned fork commit, source files identical to the pinned base
    plus the current patch (and none besides), the build options SOURCE.txt describes, and a clean
    Integration/ folder. It cannot show that the binary was compiled from that tree, nor compare the
    autotools-generated files; it shows no source is newer, different or extra beside the binary."""
    root = checkout_root(mednafen)
    binary = os.path.abspath(mednafen)
    problems = []
    try:
        head = mso.git_out(["rev-parse", "HEAD"], root)
        if head != up["fork_base_commit"]:
            problems.append(f"the Mednafen checkout is at {head[:10]}, not the pinned {up['fork_base_commit'][:10]} (--mednafen-rev?)")
    except (subprocess.CalledProcessError, OSError):
        problems.append(f"{root} is not a git checkout of the Mednafen fork")
    if not problems:
        problems += compare_to_patched_tree(up, root, binary if os.path.isfile(binary) else os.path.join(binary, "mednafen.exe"), mso)
    stamp = os.path.join(root, mso.install.MEDNAFEN_STAMP)
    try:
        with open(stamp, encoding="utf-8") as f:
            built = f.read()
        wanted = [f"--disable-{c}" for c in mso.install.MEDNAFEN_OTHER_CORES] + [mso.install.MEDNAFEN_DEFINES]
        missing = [w for w in wanted if w not in built]
        if missing:
            problems.append("it was not built the way SOURCE.txt says (Saturn-only, rewind): missing " + ", ".join(missing[:4]))
    except OSError:
        problems.append(f"no build record ({mso.install.MEDNAFEN_STAMP}) in {root}; build it with install.py --mednafen-saturn-only")
    dirty = subprocess.run(["git", "status", "--porcelain", "--", "Integration"], cwd=SE_ROOT,
                           capture_output=True, text=True).stdout.strip()
    if dirty:
        problems.append("Integration/ has uncommitted changes, so the source offer would not match any commit")
    if problems and not allow:
        shown = problems[:12]
        raise SystemExit("cannot package, the binary and the source offer may not correspond:\n  - " +
                         "\n  - ".join(shown) + (f"\n  ... and {len(problems) - 12} more" if len(problems) > 12 else "") +
                         "\n(--allow-mismatch overrides, for a local trial only)")
    for p in problems:
        print("warning (overridden):", p)


# --------------------------------------------------------------------------- bundled third-party libraries
SOURCE_FORMULA_LICENSE = re.compile(r"GPL")      # copyleft libraries: the source goes with the download


def brew_manifest(copied, allow_mismatch=False):
    """One entry per Homebrew formula behind a copied library: version, licence, the exact source tarball
    and its checksum, as Homebrew records them. `brew info` describes the NEWEST version, so it is only
    used when that is the version that was copied (the Cellar path says which); an older install would
    otherwise get a source offer for code it does not contain."""
    by_formula, installed = {}, {}
    for real in copied:
        m = re.search(r"/Cellar/([^/]+)/([^/]+)/", real)
        if not m:
            raise SystemExit(f"{real} is not from a Homebrew formula; list its licence and source by hand")
        by_formula.setdefault(m.group(1), []).append(os.path.basename(real))
        installed[m.group(1)] = m.group(2)                      # as installed, with Homebrew's _N revision if any
    entries = []
    for formula, files in sorted(by_formula.items()):
        info = json.loads(out(["brew", "info", "--json=v2", formula]))["formulae"][0]
        stable = info["urls"]["stable"]
        if re.sub(r"_\d+$", "", installed[formula]) != info["versions"]["stable"]:
            msg = (f"{formula} {installed[formula]} is installed but Homebrew's current is {info['versions']['stable']}; "
                   "the source offer would name the wrong source (brew upgrade, then rebuild Mednafen)")
            if not allow_mismatch:
                raise SystemExit(msg)
            print("warning (overridden):", msg)
            stable = {"url": "n/a (the installed version is not the current Homebrew one)", "checksum": None}
        entries.append({"name": formula, "version": installed[formula], "license": info.get("license") or "unknown",
                        "files": sorted(files), "source": stable["url"], "sha256": stable.get("checksum"),
                        "note": "built by Homebrew formula " + formula + "; the formula, including any patches it applies, is at " +
                                f"https://github.com/Homebrew/homebrew-core/blob/{info.get('tap_git_head', 'HEAD')}/{info.get('ruby_source_path', 'Formula/' + formula[0] + '/' + formula + '.rb')}" +
                                " (as of the local Homebrew checkout)"})
    return entries


MSYS2_SOURCES = "https://repo.msys2.org/mingw/sources/"
# Binary packages whose source package has another name (split packages).
MSYS2_SOURCE_ALIASES = {"libwinpthread": "winpthreads", "libgcc": "gcc", "libstdc++": "gcc", "gcc-libs": "gcc",
                        "libgfortran": "gcc", "libgomp": "gcc"}


def parse_pacman_qo(text):
    """('mingw-w64-x86_64-libiconv', '1.18-1') from `pacman -Qo` output, or (None, None)."""
    m = re.search(r"is owned by (\S+) (\S+)", text)
    return (m.group(1), m.group(2)) if m else (None, None)


def parse_pacman_field(text, field):
    m = re.search(r"^" + re.escape(field) + r"\s*:\s*(.+?)\s*$", text, re.M)
    return m.group(1) if m else None


def msys2_source_url(index_html, pkg, ver):
    """The MSYS2 source archive (PKGBUILD, patches and the upstream tarball) for an installed package:
    https://repo.msys2.org/mingw/sources/mingw-w64-<source name>-<version>.src.tar.zst. A split package
    (gettext-runtime) is built from the source package it is a part of (gettext), so shorter names are tried."""
    name = re.sub(r"^mingw-w64-(x86_64|i686|ucrt-x86_64|clang-x86_64)-", "", pkg)
    candidates = [MSYS2_SOURCE_ALIASES.get(name, name)]
    while "-" in name:
        name = name.rsplit("-", 1)[0]
        candidates.append(name)
    for cand in candidates:
        fname = f"mingw-w64-{cand}-{ver}.src.tar.zst"
        if fname in index_html:
            return MSYS2_SOURCES + fname
    return None


def msys2_manifest(dlls, dll_dir, run_pacman=None, source_index=None, mingw_bin=None):
    """Windows: the MSYS2 package that owns each bundled DLL, its licence and its source archive. `pacman`
    is asked about the INSTALLED copy in `mingw_bin`, so the copy that is actually packaged (in `dll_dir`) must be
    byte-identical to it: after an MSYS2 upgrade install.py keeps the DLLs it copied earlier, and the source
    named here would then not be the source of what ships. `run_pacman`, `source_index` and `mingw_bin` are
    injectable so this can be tested without MSYS2; not exercised on a runner yet."""
    if run_pacman is None:
        exe = next((p for p in (r"C:\msys64\usr\bin\pacman.exe",) if os.path.exists(p)), None)
        if not exe:
            raise SystemExit(r"MSYS2's pacman was not found (C:\msys64); it is needed to name the bundled DLLs' packages")
        mingw_bin = mingw_bin or os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(exe))), "mingw64", "bin")
        env = {**os.environ, "MSYSTEM": "MINGW64"}

        def run_pacman(*args):
            return subprocess.run([exe, *args], capture_output=True, text=True, env=env).stdout
    if source_index is None:
        with urllib.request.urlopen(MSYS2_SOURCES, timeout=120) as r:
            source_index = r.read().decode("utf-8", "replace")
    entries = {}
    for dll in dlls:
        installed = os.path.join(mingw_bin or "", dll)
        if not os.path.isfile(installed) or sha256_file(installed) != sha256_file(os.path.join(dll_dir, dll)):
            raise SystemExit(f"{dll} in the package is not the copy MSYS2 has installed (an MSYS2 upgrade since "
                             "Mednafen was built?); delete the stale DLLs beside mednafen.exe and rebuild")
        pkg, ver = parse_pacman_qo(run_pacman("-Qo", "/mingw64/bin/" + dll))
        if not pkg:
            raise SystemExit(f"{dll} is not owned by any MSYS2 package; list its licence and source by hand")
        if pkg not in entries:
            info = run_pacman("-Qi", pkg)
            entries[pkg] = {"name": pkg, "version": ver, "license": parse_pacman_field(info, "Licenses") or "unknown",
                            "files": [], "source": msys2_source_url(source_index, pkg, ver),
                            "sha256": None, "record_hash": True,
                            "note": "MSYS2 package (build recipe and patches are in the source archive)"}
        entries[pkg]["files"].append(dll)
    return sorted(entries.values(), key=lambda e: e["name"])


def fetch_sources(entries, dest, allow_missing=False):
    """Download the source of each copyleft library into dest. Homebrew gives a checksum to verify against;
    the MSYS2 archives have none, so their hash is recorded as a baseline instead. A copyleft library whose
    source cannot be found stops the packaging: the offer must be able to name it."""
    os.makedirs(dest, exist_ok=True)
    for e in entries:
        if not SOURCE_FORMULA_LICENSE.search(e["license"]):
            continue
        if not e["source"] or not e["source"].startswith("http"):
            msg = f"no source found for {e['name']} {e['version']} ({e['license']}), which is shipped"
            if not allow_missing:
                raise SystemExit(msg + "; add it by hand or pass --allow-mismatch")
            print("warning (overridden):", msg)
            e["source"] = e["source"] or "unknown"
            continue
        if not e.get("sha256") and not e.get("record_hash"):
            continue
        if "exception" in e["license"].lower():
            # GPL with a runtime-library exception (libgcc, libstdc++): the source is named and offered, but a
            # multi-hundred-megabyte compiler tarball is not put in every download.
            e["note"] = (e.get("note") or "") + "; source offered, not bundled"
            continue
        name = os.path.basename(e["source"].split("?")[0])
        path = os.path.join(dest, name)
        with urllib.request.urlopen(e["source"], timeout=300) as r, open(path, "wb") as f:
            shutil.copyfileobj(r, f)
        got = sha256_file(path)
        if e.get("sha256") and got != e["sha256"]:
            raise SystemExit(f"{name}: checksum {got} does not match the recorded {e['sha256']}")
        e["sha256"] = got
        e["included"] = "third-party-source/" + name


# --------------------------------------------------------------------------- Windows runtime
def msvc_redist_dirs():
    """Candidate x64 runtime folders, best first: the toolchain that built this program (VCToolsRedistDir, which
    the Visual Studio developer shell sets), else installed Visual Studios newest runtime version first. A runtime
    older than the build tools can lack functions the program imports."""
    redist = os.environ.get("VCToolsRedistDir")
    if redist:
        found = sorted(glob.glob(os.path.join(redist, "x64", "Microsoft.VC*.CRT")))
        if found:
            return found
    pf = [os.environ.get("ProgramFiles", r"C:\Program Files"), os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")]
    found = []
    for base in pf:
        for d in glob.glob(os.path.join(base, "Microsoft Visual Studio", "*", "*", "VC", "Redist", "MSVC", "*", "x64", "Microsoft.VC*.CRT")):
            m = re.search(r"MSVC[\\/](\d+(?:\.\d+)*)[\\/]x64", d)
            found.append((tuple(int(x) for x in m.group(1).split(".")) if m else (0,), d))
    return [d for _v, d in sorted(found, reverse=True)]


def copy_msvc_runtime(dest, override):
    """The frontend is built against the dynamic MSVC runtime, which a clean Windows machine may not have.
    Ship the app-local redistributable DLLs (vcruntime140*.dll, msvcp140*.dll) beside the exe."""
    required = ("vcruntime140.dll", "msvcp140.dll")
    for d in ([override] if override else msvc_redist_dirs()):
        dlls = glob.glob(os.path.join(d, "*.dll"))
        if all(os.path.exists(os.path.join(d, r)) for r in required):
            for f in dlls:
                shutil.copy2(f, dest)
            return [os.path.basename(f) for f in dlls]
        if override:
            raise SystemExit(f"{override} lacks {', '.join(required)}")
    raise SystemExit("the Microsoft Visual C++ runtime DLLs were not found; install the VS C++ workload or pass --crt-dir")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--se-app", required=True, help="macOS: the finished SaturnExplorer.app; else: the folder holding the exe")
    ap.add_argument("--mednafen", required=True, help="macOS: the mednafen binary; else: the checkout folder holding mednafen.exe")
    ap.add_argument("--ref", required=True, help="release label, e.g. v1.0 (letters, digits, . _ -)")
    ap.add_argument("--out", required=True, help="output folder (created)")
    ap.add_argument("--sign-identity", default="-", help="macOS codesign identity for mednafen (default: ad hoc)")
    ap.add_argument("--crt-dir", help="Windows: folder holding the app-local MSVC runtime DLLs (default: found under Visual Studio)")
    ap.add_argument("--no-tarball", action="store_true", help="skip the patched-source tarball (SOURCE.txt only)")
    ap.add_argument("--no-third-party-source", action="store_true", help="do not download the copyleft libraries' source into the archive")
    ap.add_argument("--allow-mismatch", action="store_true", help="package although the binary may not match the source offer (local trials only)")
    ap.add_argument("--no-archive", action="store_true", help="leave the staged folder, make no .dmg/.zip")
    args = ap.parse_args()
    if not re.fullmatch(r"[A-Za-z0-9._-]+", args.ref):
        raise SystemExit("--ref may only contain letters, digits, '.', '_' and '-'")

    sys.path.insert(0, HERE)
    import make_source_offer as mso
    up = mso.load_upstream()
    verify_provenance(up, args.mednafen, mso, args.allow_mismatch)

    plat = "macos" if IS_MAC else "windows" if IS_WIN else "linux"
    name = f"SaturnExplorer-{args.ref}-{plat}"
    stage = os.path.join(os.path.abspath(args.out), name)
    if os.path.exists(stage):
        shutil.rmtree(stage)
    emu = os.path.join(stage, "mednafen-se")
    os.makedirs(os.path.join(emu, "firmware"))

    if IS_MAC:
        app = os.path.join(stage, "SaturnExplorer.app")
        shutil.copytree(args.se_app, app, symlinks=True)
        exe = os.path.join(emu, "mednafen")
        shutil.copy2(args.mednafen, exe)
        os.chmod(exe, 0o755)
        copied = bundle_dylibs(exe, os.path.abspath(args.mednafen), os.path.join(emu, "lib"), args.sign_identity)
        libs = brew_manifest(copied, args.allow_mismatch)
    else:
        # Only the program: the build folder also holds every test executable and the developer tools
        # (se-render, se-sleighc), which the macOS bundle does not ship either.
        for f in os.listdir(args.se_app):
            src = os.path.join(args.se_app, f)
            if f in SHIPPED_PROGRAMS or f.lower().endswith(".dll") or f == "licenses":
                (shutil.copytree if os.path.isdir(src) else shutil.copy2)(src, os.path.join(stage, f))
        crt = copy_msvc_runtime(stage, args.crt_dir) if IS_WIN else []
        exe_name = "mednafen.exe" if IS_WIN else "mednafen"
        dlls = [f for f in os.listdir(args.mednafen) if f.lower().endswith(".dll")]
        for f in os.listdir(args.mednafen):
            if f == exe_name or f in dlls:
                shutil.copy2(os.path.join(args.mednafen, f), emu)
        exe = os.path.join(emu, exe_name)
        if not os.path.isfile(exe):
            raise SystemExit(f"{exe_name} not found in {args.mednafen}")
        libs = msys2_manifest(dlls, args.mednafen) if IS_WIN else []
        if crt:
            libs.append({"name": "Microsoft Visual C++ runtime", "version": "as shipped with the Visual Studio that built SE",
                         "license": "Microsoft redistributable terms (app-local)", "files": sorted(crt),
                         "source": "n/a", "sha256": None, "note": "not open source; redistributed under the Visual Studio licence"})

    with open(os.path.join(emu, "README-FORK.txt"), "w", encoding="utf-8", newline="\n") as f:
        f.write(readme_fork(up["upstream_version"], args.ref))
    with open(os.path.join(emu, "firmware", "PUT-BIOS-HERE.txt"), "w", encoding="utf-8", newline="\n") as f:
        f.write("Place " + " and ".join(FIRMWARE_FILES) + " here. They are not distributed with Saturn Explorer.\n")
    merge_licenses(stage, IS_MAC)
    with open(os.path.join(stage, "README.txt"), "w", encoding="utf-8", newline="\n") as f:
        f.write(readme_top(args.ref, IS_MAC))

    if not args.no_third_party_source:
        fetch_sources(libs, os.path.join(stage, "third-party-source"), args.allow_mismatch)
    se_commit = mso.git_out(["rev-parse", "HEAD"], SE_ROOT)
    tar_name = None if args.no_tarball else "mednafen-se-source.tar.xz"
    if tar_name:
        mso.build_tarball(up, os.path.join(SE_ROOT, "_emu", "mednafen"), os.path.join(stage, tar_name), args.ref)
    with open(os.path.join(stage, "SOURCE.txt"), "w", encoding="utf-8", newline="\n") as f:
        f.write(mso.source_text(up, args.ref, se_commit, tar_name, libs))

    smoke_test(stage, exe)

    if args.no_archive:
        print(f"staged: {stage}")
        return
    archive = os.path.join(os.path.abspath(args.out), name + (".dmg" if IS_MAC else ".zip"))
    if os.path.exists(archive):
        os.remove(archive)
    if IS_MAC:
        run(["hdiutil", "create", "-quiet", "-volname", "Saturn Explorer", "-srcfolder", stage,
             "-format", "UDZO", archive])
    else:
        shutil.make_archive(archive[:-4], "zip", os.path.dirname(stage), name)
    print(f"wrote {archive}")


def package_macho_files(emu):
    return [os.path.join(emu, "mednafen")] + [os.path.join(emu, "lib", f) for f in sorted(os.listdir(os.path.join(emu, "lib")))]


def smoke_test(stage, mednafen):
    """Check the layout from where it sits: every load command resolves inside the package, the app and the
    emulator start, and SDL (which loads SDL3 at run time) initialises. Fails the packaging run rather than
    shipping a download that cannot start."""
    emu = os.path.dirname(mednafen)
    scratch = tempfile.mkdtemp(prefix="se_smoke_")          # Mednafen writes its config and lock under HOME
    # macOS: a bare environment, so nothing from Homebrew can leak in through the search paths. Windows needs
    # its own variables (SystemRoot and friends) for any program to start, so it inherits them.
    # MEDNAFEN_NOSTDREDIR: on Windows Mednafen sends stdout and stderr to stdout.txt/stderr.txt beside the exe,
    # so without it the pipes below stay empty and the staged folder gains two files that would be zipped.
    env = ({**os.environ, "HOME": scratch, "MEDNAFEN_HOME": scratch, "MEDNAFEN_NOSTDREDIR": "1"} if IS_WIN
           else {"PATH": "/usr/bin:/bin", "HOME": scratch, "MEDNAFEN_HOME": scratch})
    try:
        if IS_MAC:
            for item in package_macho_files(emu):
                for dep in macho_deps(item):
                    if is_system(dep):
                        continue
                    base = os.path.dirname(item)
                    target = (dep.replace("@loader_path", base).replace("@executable_path", emu)
                              if dep.startswith(("@loader_path", "@executable_path")) else None)
                    if target is None or not os.path.exists(target):
                        raise SystemExit(f"{item} depends on {dep}, which is not inside the package")
            r = subprocess.run([os.path.join(stage, "SaturnExplorer.app", "Contents", "MacOS", "SaturnExplorer"),
                                "--selftest"], capture_output=True, text=True, timeout=60, env=env)
            if "selftest ok" not in r.stdout:
                raise SystemExit("SaturnExplorer --selftest failed:\n" + r.stdout + r.stderr)
            # sdl2-compat loads SDL3 on first use and, when it cannot, shows a blocking dialog and never
            # returns: so a timeout here is the failure signal.
            probe = ("import ctypes,sys\nl=ctypes.CDLL(sys.argv[1])\nsys.exit(0 if l.SDL_Init(0)==0 else 3)\n")
            try:
                r = subprocess.run([sys.executable, "-I", "-c", probe, os.path.join(emu, "lib", "libSDL2-2.0.0.dylib")],
                                   capture_output=True, text=True, timeout=30, env=env)
            except subprocess.TimeoutExpired:
                raise SystemExit("the bundled SDL did not initialise (is libSDL3.dylib beside libSDL2?)")
            if r.returncode != 0:
                raise SystemExit("the bundled SDL did not initialise:\n" + r.stdout + r.stderr)
        # -help prints the banner, the library versions and the usage text, then exits non-zero in this
        # Mednafen. A banner alone also appears before a startup failure, so require the closing usage text.
        r = subprocess.run([mednafen, "-help"], capture_output=True, text=True, timeout=60, env=env)
        text = r.stdout + r.stderr
        if "Starting Mednafen" not in text or "do not need to be passed to future invocations" not in text:
            raise SystemExit("mednafen did not run from the package:\n" + text[-800:])
    finally:
        shutil.rmtree(scratch, ignore_errors=True)
    print("smoke test ok")


if __name__ == "__main__":
    main()
