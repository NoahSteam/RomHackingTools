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

  Linux: as Windows, with SaturnExplorerFrontEnd + lib/*.so beside it and mednafen-se/mednafen + lib/*.so,
  each runpath set to its lib/ (patchelf). Libraries every distribution provides (is_linux_system) stay out.

Before anything is copied it checks that the Mednafen binary really is what the source offer will describe
(pinned commit, patch current, Saturn-only build, clean Integration/, installed library versions); see
verify_provenance. The Mednafen folder sits BESIDE the app, not inside it: a macOS bundle is sealed by its signature
and the user has to be able to add the firmware/ folder. It is then smoke-tested from where it
sits (no dependency on Homebrew or MSYS2 left, the app's --selftest, Mednafen starts) and archived
(.dmg on macOS, .tar.gz on Linux, .zip on Windows).

  # macOS: an app built with -DSE_MACOS_BUNDLE_LIBS=ON and installed (see BUILD.md), and the Mednafen build
  python3 Integration/package_release.py --se-app stage/SaturnExplorer.app \\
          --mednafen _emu/mednafen/src/mednafen --ref v1.0 --out dist

  # Linux: build-release/bin (the program and its licenses/) and the folder holding the mednafen binary
  python3 Integration/package_release.py --se-app build-release/bin --mednafen _emu/mednafen/src --ref v1.0 --out dist

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
import urllib.parse
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
SE_ROOT = os.path.dirname(HERE)
IS_MAC = sys.platform == "darwin"
IS_WIN = os.name == "nt"
IS_LINUX = sys.platform.startswith("linux")

FIRMWARE_FILES = ("sega_101.bin", "mpr-17933.bin")
MEDNAFEN_EXE = "mednafen.exe" if IS_WIN else "mednafen"
# Where the licence texts are: macOS keeps the app's own sealed in the bundle and puts these beside it; elsewhere
# they share the build's licenses/ folder (merge_licenses).
LICENSES_DIR = "Licenses" if IS_MAC else "licenses"
SE_PROGRAM = "SaturnExplorerFrontEnd.exe" if IS_WIN else "SaturnExplorerFrontEnd"   # Windows/Linux build folder


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


def copy_lib(real, name, libdir, copied):
    """Copy one library into libdir as `name`, recording it in `copied` (original real path -> name)."""
    if name in copied.values():
        raise SystemExit(f"two different libraries are both called {name}")
    dest = os.path.join(libdir, name)
    shutil.copy2(real, dest)
    os.chmod(dest, 0o755)
    copied[real] = name
    return dest


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
        dest = copy_lib(real, name, libdir, copied)
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


# --------------------------------------------------------------------------- Linux: bundle shared libraries
# Libraries the user's system provides and the package must NOT carry, modelled on AppImage's excludelist:
# the C library and the compiler runtime (the build runs on the oldest supported distribution, so its versions
# are the baseline), the GPU driver stack, the display and audio servers' client libraries, which must match
# the running servers, and a few that every desktop has. Named one by one, so a new dependency is bundled and
# then stops at LINUX_BUNDLED_SOURCES until someone decides about it; only the xcb, Wayland and PipeWire
# families, whose members always come with the server, are matched by prefix.
LINUX_SYSTEM_STEMS = frozenset((
    "libc", "libm", "libmvec", "libdl", "libpthread", "librt", "libresolv", "libutil", "libanl", "libnsl",
    "libBrokenLocale", "libstdc++", "libgcc_s", "linux-vdso",
    "libGL", "libGLX", "libEGL", "libGLESv2", "libGLdispatch", "libOpenGL", "libglapi", "libdrm", "libgbm",
    "libX11", "libX11-xcb", "libXau", "libXdmcp", "libXext", "libXcursor", "libXi", "libXfixes", "libXrandr",
    "libXrender", "libXss", "libXinerama", "libXxf86vm", "libxkbcommon",
    "libasound", "libpulse", "libpulse-simple", "libjack",
    "libdbus-1", "libsystemd", "libudev", "libfontconfig", "libfreetype", "libz", "libexpat", "libuuid"))
LINUX_SYSTEM_PREFIXES = ("ld-linux", "libxcb", "libwayland-", "libpipewire-")

# The reviewed licence of each Debian SOURCE package whose libraries the archive carries, and from which upstream
# version that holds. Debian's copyright file is kept as evidence (licenses/bundled-libraries/) but not parsed for
# the licence: its "Files: *" stanza describes the whole source package (FLAC's says GPL/LGPL, for its tools),
# not the one library shipped. A source package missing here stops the packaging.
LINUX_BUNDLED_SOURCES = {
    "libsdl2": {"license": "Zlib", "since": "2.0"},
    "flac": {"license": "BSD-3-Clause", "since": "1.2",
             "note": "only libFLAC is shipped, which Xiph licenses under BSD-3-Clause; the GPL parts are the tools"},
    "libogg": {"license": "BSD-3-Clause", "since": "1.0"},
    "libsamplerate": {"license": "BSD-2-Clause", "since": "0.1.9", "note": "GPL before 0.1.9"},
    "libdecor-0": {"license": "MIT", "since": "0.1"},
}


def is_linux_system(name):
    stem = os.path.basename(name).partition(".so")[0]
    return stem in LINUX_SYSTEM_STEMS or stem.startswith(LINUX_SYSTEM_PREFIXES)


def tool_env(env=None):
    """The environment for readelf/ldd/dpkg/apt: no LD_* variable can redirect the loader, and messages are in English."""
    return {**{k: v for k, v in (os.environ if env is None else env).items() if not k.startswith("LD_")}, "LC_ALL": "C"}


def elf_dynamic(path):
    r = subprocess.run(["readelf", "-d", path], capture_output=True, text=True, env=tool_env())
    if r.returncode != 0:
        raise SystemExit(f"readelf failed on {path}: {r.stderr.strip()}")
    return r.stdout


def elf_needed(path):
    """The DT_NEEDED names of an ELF file, in order."""
    return re.findall(r"\(NEEDED\)\s+Shared library: \[(.+?)\]", elf_dynamic(path))


def elf_soname(path):
    m = re.search(r"\(SONAME\)\s+Library soname: \[(.+?)\]", elf_dynamic(path))
    return m.group(1) if m else None


def parse_ldd(text):
    """name -> resolved path (None for 'not found'). Paths may contain spaces; the loader address ends the line."""
    found = {}
    for line in text.splitlines():
        m = re.match(r"\s*(\S+) => (?:not found|(.+) \(0x[0-9a-f]+\))\s*$", line)
        if m:
            found[m.group(1)] = m.group(2)
    return found


def ldd_map(path, env=None):
    """parse_ldd of `ldd path`: how the loader resolves every library `path` loads, transitively. Only ever run on
    programs this build produced (ldd may execute the program's interpreter)."""
    r = subprocess.run(["ldd", path], capture_output=True, text=True, env=tool_env(env))
    if r.returncode != 0:
        raise SystemExit(f"ldd failed on {path}: {r.stderr.strip() or r.stdout.strip()}")
    found = parse_ldd(r.stdout)
    # A library already loaded satisfies any later request for its SONAME, which ldd then does not list again.
    for lib in [p for p in found.values() if p]:
        soname = elf_soname(lib)
        if soname:
            found.setdefault(soname, lib)
    return found


def bundle_elf_libs(exe, exe_origin, libdir):
    """Copy every non-system shared library `exe` needs (transitively, through the libraries that are copied)
    into libdir and point the runpaths at the copies: $ORIGIN/<libdir> on the executable, $ORIGIN on each
    library (DT_RUNPATH is not inherited, so every copied library needs its own). Names resolve as they do when
    the ORIGINAL executable runs (`exe_origin`), which also covers a DT_RPATH it passes down to its libraries.
    A dependency of a system library is the system's business and is not followed. A library asked for by a
    second name is linked under that name too. Returns {original real path: file name in libdir}."""
    os.makedirs(libdir, exist_ok=True)
    resolved = ldd_map(exe_origin)
    copied = {}                               # original real path -> file name in libdir
    queue = [exe]
    while queue:
        for name in elf_needed(queue.pop()):
            if is_linux_system(name):
                continue
            real = resolved.get(name)
            if not real:
                raise SystemExit(f"cannot resolve {name}, which {exe_origin} loads")
            real = os.path.realpath(real)
            if real not in copied:            # copied under its SONAME, which every loader request can reach
                queue.append(copy_lib(real, elf_soname(real) or name, libdir, copied))
            if name != copied[real] and not os.path.lexists(os.path.join(libdir, name)):
                os.symlink(copied[real], os.path.join(libdir, name))
    run(["patchelf", "--set-rpath", "$ORIGIN/" + os.path.relpath(libdir, os.path.dirname(exe)), exe])
    for name in copied.values():
        run(["patchelf", "--set-rpath", "$ORIGIN", os.path.join(libdir, name)])
    return copied


def dpkg_owner(path):
    """The Debian package that installed `path`, trying the /lib <-> /usr/lib spelling a merged /usr hides."""
    variants = [path]
    if path.startswith("/usr/lib/"):
        variants.append(path[len("/usr"):])
    elif path.startswith("/lib/"):
        variants.append("/usr" + path)
    for p in variants:
        r = subprocess.run(["dpkg", "-S", p], capture_output=True, text=True, env=tool_env())
        if r.returncode == 0:
            return r.stdout.split(":", 1)[0].strip()
    return None


def parse_print_uris(text):
    """[(url, algorithm, digest)] from `apt-get source --print-uris` lines ('URL' file size SHA512:hash);
    apt names the strongest hash it has, SHA256 or SHA512 depending on its version. A file line in any other
    form is an error: dropping it would leave the source incomplete without anyone noticing."""
    found = []
    for line in text.splitlines():
        if not line.startswith("'"):
            continue
        m = re.fullmatch(r"'(\S+)' \S+ \d+ (SHA256|SHA512):([0-9a-f]+)", line.strip())
        if not m:
            raise SystemExit(f"unrecognised line from apt-get source --print-uris: {line}")
        found.append((m.group(1), m.group(2).lower(), m.group(3)))
    return found


def parse_dsc_members(text):
    """{file name: (size, sha256)} from a Debian .dsc's Checksums-Sha256 field: every file of the source package."""
    m = re.search(r"^Checksums-Sha256:[ \t]*\n((?:[ \t]+\S.*\n?)+)", text, re.M)
    if not m:
        raise SystemExit("the .dsc has no Checksums-Sha256 field")
    members = {}
    for line in m.group(1).splitlines():
        digest, size, name = line.split()
        members[name] = (int(size), digest)
    return members


def version_at_least(version, minimum):
    return subprocess.run(["dpkg", "--compare-versions", version, "ge", minimum], env=tool_env()).returncode == 0


def dpkg_manifest(copied, licenses_dir, allow_mismatch=False):
    """One entry per Debian package behind a copied library (`copied`: original path -> name in the package):
    version, the reviewed licence of its source package (LINUX_BUNDLED_SOURCES), and the source package's files
    with their checksums from apt's deb-src index. The package's copyright file, and any common licence text it
    refers to, go into licenses_dir/<package>/ beside the other licence texts."""
    def problem(msg):
        if not allow_mismatch:
            raise SystemExit(msg)
        print("warning (overridden):", msg)

    by_pkg = {}
    for real, name in sorted(copied.items()):
        pkg = dpkg_owner(real)
        if not pkg:
            raise SystemExit(f"{real} is not from a Debian package; list its licence and source by hand")
        by_pkg.setdefault(pkg, set()).add(name)
    entries = []
    for pkg, files in sorted(by_pkg.items()):
        ver, src, src_ver = out(["dpkg-query", "-W", "-f=${Version}\t${source:Package}\t${source:Version}", pkg]).split("\t")
        upstream = re.sub(r"^\d+:", "", src_ver).split("-")[0]
        decision = LINUX_BUNDLED_SOURCES.get(src)
        if decision is None or not version_at_least(upstream, decision["since"]):
            problem(f"no reviewed licence for {pkg} (source package {src} {src_ver}); read /usr/share/doc/{pkg}/copyright "
                    "and add it to LINUX_BUNDLED_SOURCES in package_release.py")
            decision = {"license": "unknown (not reviewed)"}
        dest = os.path.join(licenses_dir, pkg)
        os.makedirs(dest, exist_ok=True)
        copyright_file = f"/usr/share/doc/{pkg}/copyright"
        with open(copyright_file, encoding="utf-8", errors="replace") as f:
            text = f.read()
        shutil.copy2(copyright_file, os.path.join(dest, "copyright"))
        for common in sorted(set(re.findall(r"/usr/share/common-licenses/([A-Za-z0-9.+_-]+)", text))):
            if os.path.isfile("/usr/share/common-licenses/" + common):
                shutil.copy2("/usr/share/common-licenses/" + common, os.path.join(dest, common))
        r = subprocess.run(["apt-get", "source", "--print-uris", f"{src}={src_ver}"], capture_output=True, text=True,
                           env=tool_env())
        uris = parse_print_uris(r.stdout) if r.returncode == 0 else []
        dsc = next((u for u, _a, _h in uris if u.endswith(".dsc")), None)
        if not dsc:
            problem(f"apt has no source for {src} {src_ver} (enable deb-src and apt-get update): {r.stderr.strip()}")
        entries.append({"name": pkg, "version": ver, "license": decision["license"], "files": sorted(files),
                        "source": dsc, "source_files": uris,
                        "note": f"Debian/Ubuntu package {pkg}, built from source package {src} {src_ver}" +
                                (f"; {decision['note']}" if decision.get("note") else "")})
    return entries


# --------------------------------------------------------------------------- text files
def readme_fork(version, ref, licenses_dir=LICENSES_DIR):
    return f"""Mednafen {version} - Saturn Explorer build ({ref})
{'=' * 60}

This is NOT an official Mednafen release. It is Mednafen {version} with changes that let Saturn
Explorer read and control the emulator (a local debugging endpoint, breakpoints, pause/step, rewind).
Do not report problems with it to the Mednafen project; report them to Saturn Explorer.

Source: see SOURCE.txt (and mednafen-se-source.tar.xz) in the folder above this one.
Licences: see the {licenses_dir} folder in the folder above this one.

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


# Windows builds are not code-signed (see Integration/DISTRIBUTION.md), so say what the user will see.
UNSIGNED_NOTE = """
This download is not code-signed. The first time you run it, Windows SmartScreen may show "Windows
protected your PC": choose "More info", then "Run anyway". Windows 11's Smart App Control, if it is
switched on, may refuse unsigned programs outright. To check that the zip is the one GitHub built,
compare its SHA-256 (certutil -hashfile <zip> SHA256) with SHA256SUMS-windows.txt on the release page.
"""


LINUX_NOTE = """
Linux: the download carries its own libraries except the ones a desktop provides; SYSTEM-REQUIREMENTS.txt
lists those and the newest glibc/libstdc++ symbol versions it needs (built on Ubuntu 22.04, x86_64).
File dialogs use zenity or kdialog; install one if Open/Save does nothing. The update check uses your
system's libcurl (libcurl.so.4) and reports itself unavailable without it. To check the archive, compare
sha256sum <archive> with SHA256SUMS-linux.txt on the release page.
"""


# Per platform: where to keep the files, and the note at the end of the README.
README_PLATFORM = {
    "macos": ("Keep SaturnExplorer.app and the mednafen-se folder side by side (for example both in\nApplications).", ""),
    "windows": ("Keep SaturnExplorerFrontEnd.exe and the mednafen-se folder side by side, in a folder whose path\n"
                "uses only characters your Windows code page can represent (plain English letters are always safe).",
                UNSIGNED_NOTE),
    "linux": ("Keep SaturnExplorerFrontEnd, lib/ and the mednafen-se folder together; run ./SaturnExplorerFrontEnd.",
              LINUX_NOTE),
}


def readme_top(ref, plat, licenses_dir=LICENSES_DIR):
    where, note = README_PLATFORM[plat]
    return f"""Saturn Explorer {ref}
{'=' * (17 + len(ref))}

1. {where}
2. Put your Saturn BIOS images in mednafen-se/firmware/ (see mednafen-se/README-FORK.txt).
3. Open Saturn Explorer, pick a game and use Launch.

mednafen-se is a modified Mednafen build, not an official release. Its source and the licences of
everything in this download are in SOURCE.txt, mednafen-se-source.tar.xz and {licenses_dir}/.
{note}"""


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
    return file_digest(path, "sha256")


def file_digest(path, algo):
    h = hashlib.new(algo)
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
        problems += compare_to_patched_tree(up, root, binary if os.path.isfile(binary) else
                                           os.path.join(binary, MEDNAFEN_EXE), mso)
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
        # A Debian source package is several files (.dsc, upstream tarball, packaging), each with its checksum;
        # a Homebrew or MSYS2 entry is one file, checked against its recorded hash or, without one, recorded.
        files = e.get("source_files") or ([(e["source"], "sha256", e.get("sha256"))]
                                          if e.get("sha256") or e.get("record_hash") else [])
        if not files:
            continue
        if "exception" in e["license"].lower():
            # GPL with a runtime-library exception (libgcc, libstdc++): the source is named and offered, but a
            # multi-hundred-megabyte compiler tarball is not put in every download.
            e["note"] = (e.get("note") or "") + "; source offered, not bundled"
            continue
        names = []
        for url, algo, want in files:
            name = safe_source_name(url)
            if os.path.lexists(os.path.join(dest, name)):
                raise SystemExit(f"two source files are both called {name}")
            got = download_verified(url, os.path.join(dest, name), algo, want)
            names.append(name)
        if "source_files" in e:
            check_dsc_complete(dest, names)
        else:
            e["sha256"] = got
        e["included"] = ", ".join("third-party-source/" + n for n in names)


def safe_source_name(url):
    """The file name a source URL is saved under: decoded first, then refused if it could leave the folder."""
    name = urllib.parse.unquote(urllib.parse.urlsplit(url).path.rsplit("/", 1)[-1])
    if name in ("", ".", "..") or any(c in name for c in "/\\:\0"):
        raise SystemExit(f"refusing the file name {name!r} from {url}")
    return name


def download_verified(url, path, algo, want):
    """Download to a temporary file and move it to `path` only once its digest matches `want` (when known)."""
    tmp = path + ".partial"
    try:
        with urllib.request.urlopen(url, timeout=300) as r, open(tmp, "wb") as f:
            shutil.copyfileobj(r, f)
        got = file_digest(tmp, algo)
        if want and got != want:
            raise SystemExit(f"{os.path.basename(path)}: checksum {got} does not match the recorded {want}")
        os.replace(tmp, path)
        return got
    finally:
        if os.path.exists(tmp):
            os.remove(tmp)


def check_dsc_complete(dest, names):
    """A Debian source package is complete when every file its .dsc lists was downloaded, with that size and SHA-256."""
    dsc = [n for n in names if n.endswith(".dsc")]
    if len(dsc) != 1:
        raise SystemExit(f"expected one .dsc among {names}")
    with open(os.path.join(dest, dsc[0]), encoding="utf-8", errors="replace") as f:
        members = parse_dsc_members(f.read())
    for name, (size, digest) in sorted(members.items()):
        path = os.path.join(dest, name)
        if name not in names or not os.path.isfile(path):
            raise SystemExit(f"{dsc[0]} lists {name}, which apt did not name for download")
        if os.path.getsize(path) != size or file_digest(path, "sha256") != digest:
            raise SystemExit(f"{name} does not match the size and SHA-256 {dsc[0]} gives it")


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


def copy_se_build(build_dir, stage):
    """Windows/Linux: only the program, licenses/ and (Windows) its DLLs. The build folder also holds every test
    executable and the developer tools (se-render, se-sleighc), which the macOS bundle does not ship either."""
    if not os.path.isfile(os.path.join(build_dir, SE_PROGRAM)):
        raise SystemExit(f"{SE_PROGRAM} not found in {build_dir}")
    for f in os.listdir(build_dir):
        src = os.path.join(build_dir, f)
        if f in (SE_PROGRAM, "licenses") or (IS_WIN and f.lower().endswith(".dll")):
            (shutil.copytree if os.path.isdir(src) else shutil.copy2)(src, os.path.join(stage, f))


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--se-app", required=True, help="macOS: the finished SaturnExplorer.app; else: the folder holding the exe")
    ap.add_argument("--mednafen", required=True, help="macOS: the mednafen binary; Linux: the folder holding it (_emu/mednafen/src); Windows: the checkout folder holding mednafen.exe")
    ap.add_argument("--ref", required=True, help="release label, e.g. v1.0 (letters, digits, . _ -)")
    ap.add_argument("--out", required=True, help="output folder (created)")
    ap.add_argument("--sign-identity", default="-", help="macOS codesign identity for mednafen (default: ad hoc)")
    ap.add_argument("--crt-dir", help="Windows: folder holding the app-local MSVC runtime DLLs (default: found under Visual Studio)")
    ap.add_argument("--no-tarball", action="store_true", help="skip the patched-source tarball (SOURCE.txt only)")
    ap.add_argument("--no-third-party-source", action="store_true", help="do not download the copyleft libraries' source into the archive")
    ap.add_argument("--allow-mismatch", action="store_true", help="package although the binary may not match the source offer (local trials only)")
    ap.add_argument("--no-archive", action="store_true", help="leave the staged folder, make no .dmg/.zip/.tar.gz")
    ap.add_argument("--require-window", action="store_true",
                    help="Linux: fail unless the program opens its window on X11 or Wayland (needs a display, e.g. xvfb-run)")
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
        copy_se_build(args.se_app, stage)
        exe = os.path.join(emu, MEDNAFEN_EXE)
        if not os.path.isfile(os.path.join(args.mednafen, MEDNAFEN_EXE)):
            raise SystemExit(f"{MEDNAFEN_EXE} not found in {args.mednafen}")
        if IS_LINUX:
            shutil.copy2(os.path.join(args.mednafen, MEDNAFEN_EXE), exe)
            copied = bundle_elf_libs(os.path.join(stage, SE_PROGRAM),
                                     os.path.join(args.se_app, SE_PROGRAM), os.path.join(stage, "lib"))
            copied.update(bundle_elf_libs(exe, os.path.join(args.mednafen, MEDNAFEN_EXE), os.path.join(emu, "lib")))
            libs = dpkg_manifest(copied, os.path.join(stage, "licenses", "bundled-libraries"), args.allow_mismatch)
            with open(os.path.join(stage, "SYSTEM-REQUIREMENTS.txt"), "w", encoding="utf-8", newline="\n") as f:
                f.write(linux_requirements_text(stage, emu))
        else:
            crt = copy_msvc_runtime(stage, args.crt_dir)
            dlls = [f for f in os.listdir(args.mednafen) if f.lower().endswith(".dll")]
            for f in [MEDNAFEN_EXE] + dlls:
                shutil.copy2(os.path.join(args.mednafen, f), emu)
            libs = msys2_manifest(dlls, args.mednafen)
            libs.append({"name": "Microsoft Visual C++ runtime", "version": "as shipped with the Visual Studio that built SE",
                         "license": "Microsoft redistributable terms (app-local)", "files": sorted(crt),
                         "source": "n/a", "sha256": None, "note": "not open source; redistributed under the Visual Studio licence"})

    with open(os.path.join(emu, "README-FORK.txt"), "w", encoding="utf-8", newline="\n") as f:
        f.write(readme_fork(up["upstream_version"], args.ref))
    with open(os.path.join(emu, "firmware", "PUT-BIOS-HERE.txt"), "w", encoding="utf-8", newline="\n") as f:
        f.write("Place " + " and ".join(FIRMWARE_FILES) + " here. They are not distributed with Saturn Explorer.\n")
    merge_licenses(stage, IS_MAC)
    with open(os.path.join(stage, "README.txt"), "w", encoding="utf-8", newline="\n") as f:
        f.write(readme_top(args.ref, plat))

    if not args.no_third_party_source:
        fetch_sources(libs, os.path.join(stage, "third-party-source"), args.allow_mismatch)
        if not os.listdir(os.path.join(stage, "third-party-source")):
            os.rmdir(os.path.join(stage, "third-party-source"))    # no copyleft library is shipped
    se_commit = mso.git_out(["rev-parse", "HEAD"], SE_ROOT)
    tar_name = None if args.no_tarball else "mednafen-se-source.tar.xz"
    if tar_name:
        mso.build_tarball(up, os.path.join(SE_ROOT, "_emu", "mednafen"), os.path.join(stage, tar_name), args.ref)
    with open(os.path.join(stage, "SOURCE.txt"), "w", encoding="utf-8", newline="\n") as f:
        f.write(mso.source_text(up, args.ref, se_commit, tar_name, libs, LICENSES_DIR))

    if args.no_archive:
        smoke_test(stage, exe, args.require_window)
        print(f"staged: {stage}")
        return
    if not IS_LINUX:
        smoke_test(stage, exe)
    if IS_MAC:
        archive = stage + ".dmg"
        if os.path.exists(archive):
            os.remove(archive)
        run(["hdiutil", "create", "-quiet", "-volname", "Saturn Explorer", "-srcfolder", stage,
             "-format", "UDZO", archive])
        if args.sign_identity != "-":
            codesign(archive, args.sign_identity)      # the image itself is signed too, then notarised as a whole
    else:
        # A tarball on Linux: a zip loses the executable bits. make_archive overwrites an existing archive.
        archive = shutil.make_archive(stage, "gztar" if IS_LINUX else "zip", os.path.dirname(stage), name)
    if IS_LINUX:
        # Test what users download: the finished tarball, unpacked somewhere else.
        unpacked = tempfile.mkdtemp(prefix="se_unpacked_")
        try:
            shutil.unpack_archive(archive, unpacked)
            smoke_test(os.path.join(unpacked, name), os.path.join(unpacked, name, "mednafen-se", MEDNAFEN_EXE),
                       args.require_window)
        finally:
            shutil.rmtree(unpacked, ignore_errors=True)
    print(f"wrote {archive}")


def package_macho_files(emu):
    return [os.path.join(emu, "mednafen")] + [os.path.join(emu, "lib", f) for f in sorted(os.listdir(os.path.join(emu, "lib")))]


def package_elf_files(stage, emu):
    return [os.path.join(stage, SE_PROGRAM), os.path.join(emu, "mednafen")] + \
        [os.path.join(d, f) for d in (os.path.join(stage, "lib"), os.path.join(emu, "lib")) if os.path.isdir(d)
         for f in sorted(os.listdir(d))]


def linux_requirements(stage, emu):
    """What the user's system must provide: the system libraries the packaged files load by name, and the newest
    glibc / libstdc++ symbol versions any of them needs."""
    sonames, newest = set(), {}
    for item in package_elf_files(stage, emu):
        sonames.update(n for n in elf_needed(item) if is_linux_system(n))
        text = subprocess.run(["readelf", "-V", "--wide", item], capture_output=True, text=True, env=tool_env()).stdout
        needs = text.partition("Version needs section")[2]
        for lib, ver in re.findall(r"Name: (GLIBC|GLIBCXX|CXXABI)_([0-9.]+)", needs):
            key = tuple(int(x) for x in ver.split("."))
            if key > newest.get(lib, ()):
                newest[lib] = key
    return sorted(sonames), {lib: ".".join(map(str, v)) for lib, v in sorted(newest.items())}


def linux_requirements_text(stage, emu):
    sonames, newest = linux_requirements(stage, emu)
    return ("What this download needs from your Linux system\n" + "=" * 47 + "\n\n" +
            "Symbol versions (newest needed): " + ", ".join(f"{k}_{v}" for k, v in newest.items()) + "\n\n" +
            "Libraries, by the name the programs load (install your distribution's packages that provide them):\n" +
            "".join(f"  {n}\n" for n in sonames) +
            "\nEverything else is in the lib/ folders. Saturn Explorer also uses zenity or kdialog for file dialogs\n"
            "and libcurl.so.4, if present, for the update check.\n")


def smoke_test_linux(stage, emu, env, require_window):
    """Every library a packaged ELF file loads resolves inside the package or is one the system provides, the
    program starts SDL (--selftest) with nothing from the build machine's library path and, given a display,
    opens its window with an OpenGL context on X11 or Wayland (--selftest-window)."""
    root = os.path.realpath(stage) + os.sep
    for item in package_elf_files(stage, emu):
        resolved = ldd_map(item, env)
        for name in elf_needed(item):
            path = resolved.get(name)
            if is_linux_system(name):
                if not path:
                    raise SystemExit(f"{item} needs the system library {name}, which this machine lacks")
            elif not path or not os.path.realpath(path).startswith(root):
                raise SystemExit(f"{item} depends on {name}, which is not inside the package (resolved: {path})")
    program = os.path.join(stage, SE_PROGRAM)
    run_selftest(program, {**env, "SDL_VIDEODRIVER": "dummy"})
    display = {k: os.environ[k] for k in ("DISPLAY", "WAYLAND_DISPLAY", "XDG_RUNTIME_DIR", "XAUTHORITY") if k in os.environ}
    if not display.keys() & {"DISPLAY", "WAYLAND_DISPLAY"}:
        if require_window:
            raise SystemExit("--require-window: no DISPLAY or WAYLAND_DISPLAY to open the window on (run under xvfb-run)")
        print("window check skipped: no display")
        return
    # Without a usable display SDL falls back to its offscreen driver and succeeds, so the driver is checked.
    text = run_selftest(program, {**env, **display}, "--selftest-window", "selftest-window ok: ")
    if not re.search(r"selftest-window ok: (x11|wayland)\b", text):
        raise SystemExit("the window opened on no real display:\n" + text)


def run_selftest(program, env, flag="--selftest", marker="selftest ok"):
    """Run one of the program's self-tests (WebMain.cpp) from an unrelated folder; it must exit 0 and say so."""
    r = subprocess.run([program, flag], capture_output=True, text=True, timeout=60, env=env, cwd=env.get("HOME"))
    if r.returncode != 0 or marker not in r.stdout:
        raise SystemExit(f"{os.path.basename(program)} {flag} failed (exit {r.returncode}):\n" + r.stdout + r.stderr)
    return r.stdout


def smoke_test(stage, mednafen, require_window=False):
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
            run_selftest(os.path.join(stage, "SaturnExplorer.app", "Contents", "MacOS", "SaturnExplorer"), env)
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
        if IS_LINUX:
            smoke_test_linux(stage, emu, env, require_window)
        # -help prints the banner, the library versions and the usage text, then exits non-zero in this
        # Mednafen. A banner alone also appears before a startup failure, so require the closing usage text.
        r = subprocess.run([mednafen, "-help"], capture_output=True, text=True, timeout=60, env=env, cwd=scratch)
        text = r.stdout + r.stderr
        if "Starting Mednafen" not in text or "do not need to be passed to future invocations" not in text:
            raise SystemExit("mednafen did not run from the package:\n" + text[-800:])
    finally:
        shutil.rmtree(scratch, ignore_errors=True)
    print("smoke test ok")


if __name__ == "__main__":
    main()
