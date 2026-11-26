"""package_release.py / make_source_offer.py: the parts of release packaging that can be checked offline.

  * the MSYS2 manifest (which package owns each bundled DLL, its licence, where its source is) against
    canned `pacman` output, since no MSYS2 is available on the machines the suite runs on;
  * that a shipped copyleft library whose source cannot be found stops the packaging;
  * that a credential in the git remote never reaches SOURCE.txt;
  * (macOS) that a dylib reached through @rpath is copied in, relinked and still runs after the original is gone;
  * the Linux rules: which libraries the system provides, the reviewed licences, ldd and apt output, a Debian source
    package's completeness, safe source file names, and (Linux) that a library chain is copied in, found through the
    new runpaths and still loads after the originals are gone.
Usage: check_package_release.py"""
import os
import shutil
import subprocess
import sys
import tempfile

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
import make_source_offer as mso     # noqa: E402
import package_release as pr        # noqa: E402

failures = 0


class Skip(Exception):
    """A check that needs something this machine cannot do (symlinks without the Windows privilege)."""


def symlink(target, path):
    # Windows makes a link to a directory a file link unless told otherwise, and a file link to a directory does not resolve.
    # And it does not resolve a relative target written with '/': the target takes the native separator.
    target = target.replace("/", os.sep)
    is_dir = os.path.isdir(os.path.join(os.path.dirname(path), target))
    try:
        os.symlink(target, path, target_is_directory=is_dir)
    except (OSError, NotImplementedError):
        raise Skip("symlinks are not available here")


def compare_trees(fresh, root, binary):
    """pr.compare_trees with '/' in its messages: they are built with os.path, so Windows writes '\\', and the checks below match '/'."""
    return [p.replace(os.sep, "/") for p in pr.compare_trees(fresh, root, binary)]


def check(cond, what, got=None):
    global failures
    if not cond:
        print("FAIL:", what)
        if got is not None:
            print("  got:", got)
        failures += 1


# --- pacman parsing and the MSYS2 source lookup
check(pr.parse_pacman_qo("/mingw64/bin/libiconv-2.dll is owned by mingw-w64-x86_64-libiconv 1.18-1\n") ==
      ("mingw-w64-x86_64-libiconv", "1.18-1"), "pacman -Qo is parsed")
check(pr.parse_pacman_qo("error: No package owns /mingw64/bin/x.dll") == (None, None), "an unowned file is reported as such")
check(pr.parse_pacman_field("Name : a\nLicenses      : LGPL-2.1-or-later\nURL : u\n", "Licenses") == "LGPL-2.1-or-later",
      "pacman -Qi fields are parsed")

INDEX = ('<a href="mingw-w64-libiconv-1.18-1.src.tar.zst">x</a><a href="mingw-w64-gettext-1.0-1.src.tar.zst">x</a>'
         '<a href="mingw-w64-winpthreads-13.0.0.r1-1.src.tar.zst">x</a><a href="mingw-w64-flac-1.5.0-2.src.tar.zst">x</a>')
check(pr.msys2_source_url(INDEX, "mingw-w64-x86_64-libiconv", "1.18-1") == pr.MSYS2_SOURCES + "mingw-w64-libiconv-1.18-1.src.tar.zst",
      "a plain package maps to its source archive")
check(pr.msys2_source_url(INDEX, "mingw-w64-x86_64-gettext-runtime", "1.0-1") == pr.MSYS2_SOURCES + "mingw-w64-gettext-1.0-1.src.tar.zst",
      "a split package maps to the source package it is part of")
check(pr.msys2_source_url(INDEX, "mingw-w64-x86_64-libwinpthread", "13.0.0.r1-1") ==
      pr.MSYS2_SOURCES + "mingw-w64-winpthreads-13.0.0.r1-1.src.tar.zst", "libwinpthread maps to winpthreads")
check(pr.msys2_source_url(INDEX, "mingw-w64-x86_64-libiconv", "9.9-9") is None, "a version with no archive is not guessed")

OWNERS = {"libiconv-2.dll": ("mingw-w64-x86_64-libiconv", "1.18-1"),
          "libintl-8.dll": ("mingw-w64-x86_64-gettext-runtime", "1.0-1"),
          "libwinpthread-1.dll": ("mingw-w64-x86_64-libwinpthread", "13.0.0.r1-1"),
          "libFLAC.dll": ("mingw-w64-x86_64-flac", "1.5.0-2")}
LICENSES = {"mingw-w64-x86_64-libiconv": "LGPL-2.1-or-later", "mingw-w64-x86_64-gettext-runtime": "LGPL-2.1-or-later",
            "mingw-w64-x86_64-libwinpthread": "MIT AND BSD-3-Clause", "mingw-w64-x86_64-flac": "BSD-3-Clause AND GPL-2.0-or-later"}


def fake_pacman(*args):
    if args[0] == "-Qo":
        pkg, ver = OWNERS[os.path.basename(args[1])]
        return f"{args[1]} is owned by {pkg} {ver}\n"
    return f"Name : {args[1]}\nLicenses : {LICENSES[args[1]]}\n"


dll_dir = tempfile.mkdtemp(prefix="se_dlls_")
mingw_bin = tempfile.mkdtemp(prefix="se_mingw_")
for name in OWNERS:
    for d in (dll_dir, mingw_bin):
        with open(os.path.join(d, name), "wb") as f:
            f.write(name.encode())
libs = pr.msys2_manifest(list(OWNERS), dll_dir, fake_pacman, INDEX, mingw_bin)
by = {e["name"]: e for e in libs}
check(len(libs) == 4, "one entry per package")
# a package's DLL that is not the installed copy (an MSYS2 upgrade since the build) is refused
with open(os.path.join(dll_dir, "libiconv-2.dll"), "wb") as f:
    f.write(b"an older build")
try:
    pr.msys2_manifest(list(OWNERS), dll_dir, fake_pacman, INDEX, mingw_bin)
    check(False, "a stale DLL must be refused")
except SystemExit:
    pass
shutil.rmtree(dll_dir, ignore_errors=True)
shutil.rmtree(mingw_bin, ignore_errors=True)

# split packages built from GCC
GCC_INDEX = INDEX + '<a href="mingw-w64-gcc-15.2.0-1.src.tar.zst">x</a>'
check(pr.msys2_source_url(GCC_INDEX, "mingw-w64-x86_64-libgcc", "15.2.0-1") == pr.MSYS2_SOURCES + "mingw-w64-gcc-15.2.0-1.src.tar.zst",
      "libgcc maps to the gcc source package")
check(pr.msys2_source_url(GCC_INDEX, "mingw-w64-x86_64-libstdc++", "15.2.0-1") == pr.MSYS2_SOURCES + "mingw-w64-gcc-15.2.0-1.src.tar.zst",
      "libstdc++ maps to the gcc source package")
check(by["mingw-w64-x86_64-libiconv"]["license"] == "LGPL-2.1-or-later", "the licence comes from pacman, not a placeholder")
check(all(e["source"] for e in libs), "every library's source archive was found")

# --- a copyleft library without a findable source stops the packaging; a permissive one does not
scratch = tempfile.mkdtemp(prefix="se_pkgtest_")
try:
    try:
        pr.fetch_sources([{"name": "x", "version": "1", "license": "LGPL-2.1-or-later", "source": None, "files": ["x.dll"]}], scratch)
        check(False, "a shipped LGPL library with no source must be an error")
    except SystemExit:
        pass
    pr.fetch_sources([{"name": "y", "version": "1", "license": "Zlib", "source": None, "files": ["y.dll"]},
                      {"name": "z", "version": "1", "license": "n/a", "source": "n/a", "files": []}], scratch)
    check(True, "permissive libraries need no source")
finally:
    shutil.rmtree(scratch, ignore_errors=True)

# --- one source file named by two entries (split packages) is downloaded once; a file whose name is another's
#     temporary name is still its own; the source decision of an entry outranks its licence text
scratch = tempfile.mkdtemp(prefix="se_fetch_")
try:
    import hashlib
    import functools
    import http.server
    import threading
    srv = os.path.join(scratch, "srv")
    os.makedirs(srv)
    httpd = http.server.ThreadingHTTPServer(("127.0.0.1", 0), functools.partial(
        type("Quiet", (http.server.SimpleHTTPRequestHandler,), {"log_message": lambda *a: None}), directory=srv))
    threading.Thread(target=httpd.serve_forever, daemon=True).start()
    base = f"http://127.0.0.1:{httpd.server_address[1]}/"
    def serve(name, data):
        with open(os.path.join(srv, name), "wb") as f:
            f.write(data)
        return base + name, hashlib.sha256(data).hexdigest()
    url, digest = serve("common.tar.gz", b"shared source")
    a = {"name": "a", "version": "1", "license": "GPL-2.0", "source": url, "sha256": digest, "files": ["a.dll"]}
    b = dict(a, name="b", files=["b.dll"])
    out_dir = os.path.join(scratch, "out")
    pr.fetch_sources([a, b], out_dir)
    check(a.get("included") == b.get("included") == "third-party-source/common.tar.gz", "a shared source is listed for both entries")
    check(sorted(os.listdir(out_dir)) == ["common.tar.gz"] and sorted(os.listdir(scratch)) == ["out", "srv"],
          "...downloaded once, and the staging folder is gone", os.listdir(scratch))
    member_url, member_sha = serve("pkg_1.orig.tar.gz", b"member")
    dsc_text = f"Checksums-Sha256:\n {member_sha} 6 pkg_1.orig.tar.gz\n".encode()
    dsc_url, _ = serve("pkg_1.dsc", dsc_text)
    files = [(dsc_url, "sha512", hashlib.sha512(dsc_text).hexdigest()), (member_url, "sha256", member_sha)]
    d1 = {"name": "d1", "version": "1", "license": "GPL-2.0", "source": dsc_url, "files": [], "source_files": files}
    pr.fetch_sources([d1, dict(d1, name="d2")], os.path.join(scratch, "out5"))
    check(True, "two entries naming one Debian source package with the same SHA-512 share it")
    try:
        pr.fetch_sources([d1, dict(d1, name="d3", source_files=[(dsc_url, "sha512", "0" * 128), files[1]])],
                         os.path.join(scratch, "out6"))
        check(False, "a shared source must match every entry's digest, SHA-512 included")
    except SystemExit as e:
        check("both called" in str(e), "...and the digest clash is what stops it", str(e))
    try:
        pr.fetch_sources([a, dict(a, name="c", source=base + "mirror/common.tar.gz")], os.path.join(scratch, "out2"))
        check(False, "two different source URLs under one file name must be an error")
    except SystemExit:
        pass
    u1, d1 = serve("example.partial", b"one")
    u2, d2 = serve("example", b"two")
    out3 = os.path.join(scratch, "out3")
    pr.fetch_sources([{"name": "p", "version": "1", "license": "GPL-2.0", "source": u1, "sha256": d1, "files": []},
                      {"name": "q", "version": "1", "license": "GPL-2.0", "source": u2, "sha256": d2, "files": []}], out3)
    check(sorted(os.listdir(out3)) == ["example", "example.partial"], "a file called like a temporary file survives", os.listdir(out3))
    out4 = os.path.join(scratch, "out4")
    pr.fetch_sources([{"name": "m", "version": "1", "license": "MPL-2.0", "source": u2, "sha256": d2, "files": [], "ship_source": True},
                      {"name": "g", "version": "1", "license": "GPL-2.0", "source": u1, "sha256": d1, "files": [], "ship_source": False}], out4)
    check(os.listdir(out4) == ["example"], "ship_source decides, not the licence name", os.listdir(out4))
finally:
    httpd.shutdown()
    shutil.rmtree(scratch, ignore_errors=True)

# --- GPL with the runtime-library exception: the source must be findable, but it is not downloaded into the archive
scratch = tempfile.mkdtemp(prefix="se_pkgtest_")
try:
    rle = {"name": "libgcc", "version": "15", "license": "GPL-3.0-or-later WITH GCC-exception-3.1", "files": ["libgcc_s_seh-1.dll"],
           "source": "http://127.0.0.1:9/never-fetched.src.tar.zst", "sha256": None, "record_hash": True}
    pr.fetch_sources([rle], scratch)
    check("included" not in rle and not os.listdir(scratch), "a runtime-exception library's source is named, not downloaded")
    try:
        pr.fetch_sources([dict(rle, source=None)], scratch)
        check(False, "a runtime-exception library with no findable source must still be an error")
    except SystemExit:
        pass
finally:
    shutil.rmtree(scratch, ignore_errors=True)

# --- the Visual C++ runtime: the build toolchain's, then the newest installed, never an older one by directory order
root = tempfile.mkdtemp(prefix="se_crt_")
try:
    def crt(base, ver, vc="143"):
        d = os.path.join(base, "Microsoft Visual Studio", "2022", "Community", "VC", "Redist", "MSVC", ver, "x64", f"Microsoft.VC{vc}.CRT")
        os.makedirs(d)
        for n in ("vcruntime140.dll", "msvcp140.dll"):
            with open(os.path.join(d, n), "w") as f:
                f.write(ver)
        return d
    pf, pf86 = os.path.join(root, "pf"), os.path.join(root, "pf86")
    d_new = crt(pf, "14.38.33135")
    d_old = crt(pf86, "14.29.30133", "142")
    saved = {k: os.environ.get(k) for k in ("ProgramFiles", "ProgramFiles(x86)", "VCToolsRedistDir")}
    os.environ.update({"ProgramFiles": pf, "ProgramFiles(x86)": pf86})
    os.environ.pop("VCToolsRedistDir", None)
    check(pr.msvc_redist_dirs()[0] == d_new, "with no build toolchain named, the newest installed runtime comes first")
    os.environ["VCToolsRedistDir"] = os.path.join(pf, "Microsoft Visual Studio", "2022", "Community", "VC", "Redist", "MSVC", "14.38.33135")
    check(pr.msvc_redist_dirs() == [d_new], "VCToolsRedistDir (the toolchain that built SE) wins and nothing older is appended")
    out_dir = os.path.join(root, "out")
    os.makedirs(out_dir)
    pr.copy_msvc_runtime(out_dir, None)
    check(open(os.path.join(out_dir, "msvcp140.dll")).read() == "14.38.33135", "the copied runtime is the toolchain's")
    for k, v in saved.items():
        if v is None:
            os.environ.pop(k, None)
        else:
            os.environ[k] = v
finally:
    shutil.rmtree(root, ignore_errors=True)

# --- the source comparison: differing, missing, extra and redirected inputs are all reported
cmp_root = tempfile.mkdtemp(prefix="se_cmp_")
try:
    def put(base, rel, text, link=None):
        path = os.path.join(base, rel)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        if link:
            symlink(link, path)
        else:
            with open(path, "w") as f:
                f.write(text)
    fresh, co = os.path.join(cmp_root, "fresh"), os.path.join(cmp_root, "co")
    for base in (fresh, co):
        put(base, "src/ss/a.cpp", "a")
        put(base, "src/ss/Makefile.am", "m")
        put(base, "src/header.h", "h")
    put(fresh, "include/mednafen", "", link="../src")
    put(co, "include/mednafen", "", link="../src")
    put(fresh, "src/Makefile.in", "generated-in-fresh")
    put(co, "src/Makefile.in", "regenerated-by-autoreconf")
    put(co, "include/config.h", "generated by configure")
    open(os.path.join(co, "src", "mednafen"), "w").write("binary")
    now = os.path.getmtime(os.path.join(co, "src", "mednafen"))
    for rel in ("src/ss/a.cpp", "src/ss/Makefile.am", "src/header.h"):
        os.utime(os.path.join(co, rel), (now - 100, now - 100))
    binary = os.path.join(co, "src", "mednafen")
    check(compare_trees(fresh, co, binary) == [], "an identical checkout, with only generated files differing, passes")
    put(co, "src/ss/extra.cpp", "compiled in")
    check(any("extra.cpp" in p for p in compare_trees(fresh, co, binary)), "an extra source file is reported")
    os.remove(os.path.join(co, "src/ss/extra.cpp"))
    put(co, "src/ss/a.cpp", "edited")
    check(any("a.cpp" in p and "differs" in p for p in compare_trees(fresh, co, binary)), "an edited source file is reported")
    put(co, "src/ss/a.cpp", "a")
    os.utime(os.path.join(co, "src/ss/a.cpp"), (now - 100, now - 100))
    os.remove(os.path.join(co, "include/mednafen"))
    symlink("../elsewhere", os.path.join(co, "include/mednafen"))
    check(any("include/mednafen" in p for p in compare_trees(fresh, co, binary)), "a redirected symlink is reported")
    os.remove(os.path.join(co, "include/mednafen"))
    symlink("../src", os.path.join(co, "include/mednafen"))
    os.utime(os.path.join(co, "src/header.h"), (now + 50, now + 50))
    check(any("newer than the binary" in p for p in compare_trees(fresh, co, binary)), "a source newer than the binary is reported, with no grace period")
except Skip as e:
    print("skipped the source-comparison checks:", e)
finally:
    shutil.rmtree(cmp_root, ignore_errors=True)

# --- a checkout that COPIES the linked directory (as a Windows one may) is compared all the way down
cp_root = tempfile.mkdtemp(prefix="se_cmpdir_")
try:
    def put2(base, rel, text):
        path = os.path.join(base, rel)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w") as f:
            f.write(text)
    fresh, co = os.path.join(cp_root, "fresh"), os.path.join(cp_root, "co")
    put2(fresh, "src/ss/state.h", "current")
    put2(fresh, "src/top.h", "top")
    os.makedirs(os.path.join(fresh, "include"))
    symlink("../src", os.path.join(fresh, "include", "mednafen"))
    put2(co, "src/ss/state.h", "current")
    put2(co, "src/top.h", "top")
    put2(co, "include/mednafen/ss/state.h", "current")      # the copy, in place of the link
    put2(co, "include/mednafen/top.h", "top")
    put2(co, "src/mednafen", "binary")
    binary = os.path.join(co, "src", "mednafen")
    now = os.path.getmtime(binary)
    for dp, _d, fs in os.walk(co):
        for f in fs:
            if f != "mednafen":
                os.utime(os.path.join(dp, f), (now - 100, now - 100))
    got = compare_trees(fresh, co, binary)
    check(got == [], "a faithful nested copy of the linked directory passes", got)
    put2(co, "include/mednafen/ss/state.h", "stale")
    got = compare_trees(fresh, co, binary)
    check(any("ss/state.h" in p and "differs" in p for p in got), "a stale header deep inside the copied directory is reported", got)
    put2(co, "include/mednafen/ss/state.h", "current")
    put2(co, "include/mednafen/ss/extra.h", "extra")
    check(any("extra.h" in p for p in compare_trees(fresh, co, binary)), "an extra header inside the copy is reported")
    os.remove(os.path.join(co, "include/mednafen/ss/extra.h"))
    os.remove(os.path.join(co, "include/mednafen/top.h"))
    got = compare_trees(fresh, co, binary)
    check(any("top.h" in p and "missing" in p for p in got), "a header missing from the copy is reported", got)
except Skip as e:
    print("skipped the copied-directory checks:", e)
finally:
    shutil.rmtree(cp_root, ignore_errors=True)

# --- a copy of a pinned source (config.status copies where it cannot link, as under MSYS2) is not an extra source
cc_root = tempfile.mkdtemp(prefix="se_cmpcopy_")
try:
    def put3(base, rel, text):
        path = os.path.join(base, rel)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "w") as f:
            f.write(text)
    fresh, co = os.path.join(cc_root, "fresh"), os.path.join(cc_root, "co")
    for base in (fresh, co):
        put3(base, "src/trio/trio.h", "trio")
    put3(co, "src/mednafen", "binary")
    binary = os.path.join(co, "src", "mednafen")
    now = os.path.getmtime(binary)
    os.utime(os.path.join(co, "src/trio/trio.h"), (now - 100, now - 100))
    put3(co, "include/trio/trio.h", "trio")
    check(compare_trees(fresh, co, binary) == [], "an identical copy of a pinned header, elsewhere in the checkout, passes", compare_trees(fresh, co, binary))
    put3(co, "include/trio/trio.h", "trio, edited")
    check(any("include/trio/trio.h" in p for p in compare_trees(fresh, co, binary)), "an edited copy of a pinned header is reported")
    put3(co, "include/trio/other.h", "trio")
    os.remove(os.path.join(co, "include/trio/trio.h"))
    check(any("include/trio/other.h" in p for p in compare_trees(fresh, co, binary)), "a copy under a name the pinned tree lacks is reported")
finally:
    shutil.rmtree(cc_root, ignore_errors=True)

# --- the patched tree is built with LF endings even where the user's git converts to CRLF on output
crlf_root = tempfile.mkdtemp(prefix="se_crlf_")
try:
    repo = os.path.join(crlf_root, "repo")
    os.makedirs(repo)
    gitconf = os.path.join(crlf_root, "gitconfig")
    with open(gitconf, "w") as f:
        f.write("[core]\n\tautocrlf = true\n[user]\n\tname = t\n\temail = t@example.com\n")
    env = {**os.environ, "GIT_CONFIG_GLOBAL": gitconf, "GIT_CONFIG_NOSYSTEM": "1"}
    git = lambda *a: subprocess.run(["git", *a], cwd=repo, env=env, check=True, capture_output=True)
    git("-c", "core.autocrlf=false", "init", "-q")
    with open(os.path.join(repo, "a.txt"), "wb") as f:
        f.write(b"one\ntwo\n")
    git("add", "a.txt")
    git("-c", "core.autocrlf=false", "commit", "-q", "-m", "x")
    sha = subprocess.run(["git", "rev-parse", "HEAD"], cwd=repo, env=env, check=True, capture_output=True, text=True).stdout.strip()
    probe = subprocess.run(["git", "archive", sha], cwd=repo, env=env, check=True, capture_output=True).stdout
    if b"\r\n" not in probe:
        print("skipped the line-ending check: this git does not convert on archive")
    else:
        os.environ.update(GIT_CONFIG_GLOBAL=gitconf, GIT_CONFIG_NOSYSTEM="1")
        real_run = mso.run
        mso.run = lambda cmd, **kw: None if any("apply.py" in str(c) for c in cmd) else real_run(cmd, **kw)
        try:
            tree = os.path.join(crlf_root, "tree")
            mso.make_patched_tree({"fork_base_commit": sha}, repo, tree, crlf_root)
            with open(os.path.join(tree, "a.txt"), "rb") as f:
                got = f.read()
            check(got == b"one\ntwo\n", "the patched tree keeps the commit's LF endings under core.autocrlf=true", got)
        finally:
            mso.run = real_run
            for k in ("GIT_CONFIG_GLOBAL", "GIT_CONFIG_NOSYSTEM"):
                os.environ.pop(k, None)
finally:
    shutil.rmtree(crlf_root, ignore_errors=True)

# --- no credentials in SOURCE.txt, whatever the remote's scheme
for remote, want in (("http://user:FAKE_TOKEN@server.example/org/repo.git", "http://server.example/org/repo"),
                     ("https://NoahSteam@github.com/NoahSteam/RomHackingTools.git", "https://github.com/NoahSteam/RomHackingTools"),
                     ("git@github.com:org/repo.git", "https://github.com/org/repo"),
                     ("https://tok:x@host/p?token=abc#frag", "https://host/p")):
    mso.git_out = lambda args, cwd, r=remote: r
    got = mso.se_repo_url()
    check(got == want and "FAKE_TOKEN" not in got and "token" not in got, f"remote {remote!r} -> {got!r}")

# --- the Windows archive's README says it is unsigned and how to check it; the macOS one does not say that
win_readme, mac_readme, linux_readme = (pr.readme_top("v1.0", p) for p in ("windows", "macos", "linux"))
check("not code-signed" in win_readme and "SmartScreen" in win_readme and "SHA256SUMS-windows.txt" in win_readme,
      "the Windows README warns that the build is unsigned and names the checksum file")
check("SmartScreen" not in mac_readme and win_readme.startswith("Saturn Explorer v1.0"), "the macOS README does not carry the Windows note")
check("SHA256SUMS-linux.txt" in linux_readme and "zenity" in linux_readme and "SmartScreen" not in linux_readme,
      "the Linux README names its checksum file and the dialog helpers")
check("and licenses/." in pr.readme_top("v1.0", "linux", "licenses") and "the licenses folder" in pr.readme_fork("1.32", "v1.0", "licenses"),
      "the READMEs name the licence folder the archive has")

# --- Linux: what the system provides stays out of the package, everything else goes in
for name in ("libc.so.6", "ld-linux-x86-64.so.2", "libstdc++.so.6", "libGL.so.1", "libGLX.so.0", "libEGL.so.1", "libX11.so.6",
             "libXss.so.1", "libxcb-shm.so.0", "libwayland-client.so.0", "libasound.so.2", "libpulse.so.0", "libz.so.1", "libdrm.so.2"):
    check(pr.is_linux_system(name), f"{name} is a system library")
for name in ("libSDL2-2.0.so.0", "libFLAC.so.12", "libogg.so.0", "libdecor-0.so.0", "libsamplerate.so.0", "libzstd.so.1", "libGLEW.so.2.2"):
    check(not pr.is_linux_system(name), f"{name} is bundled")

# --- Linux: apt's source listing (either hash it names) and the licence of a Debian copyright file
uris = pr.parse_print_uris("Need to get 790 kB of source archives.\n"
                           "'http://archive.ubuntu.com/ubuntu/pool/main/f/flac/flac_1.4.3%2bds-2.dsc' flac_1.4.3+ds-2.dsc 2502 SHA512:" + "ab" * 64 + "\n"
                           "'http://archive.ubuntu.com/ubuntu/pool/main/f/flac/flac_1.4.3%2bds.orig.tar.xz' flac_1.4.3+ds.orig.tar.xz 7694 SHA256:" + "cd" * 32 + "\n")
check(uris == [("http://archive.ubuntu.com/ubuntu/pool/main/f/flac/flac_1.4.3%2bds-2.dsc", "sha512", "ab" * 64),
               ("http://archive.ubuntu.com/ubuntu/pool/main/f/flac/flac_1.4.3%2bds.orig.tar.xz", "sha256", "cd" * 32)], "apt-get --print-uris is parsed", uris)
for bad in ("'http://x/flac_1.dsc' flac_1.dsc 2502 MD5Sum:" + "ab" * 16 + "\n", "'http://x/a b.dsc' a 1 SHA256:" + "ab" * 32 + "\n"):
    try:
        pr.parse_print_uris(bad)
        check(False, "an apt file line in another form must be an error, not dropped", bad)
    except SystemExit:
        pass
check(pr.parse_print_uris("Reading package lists...\nNeed to get 1 kB\n") == [], "apt's other output is not a file line")

# --- Linux: every library a bundled Debian source package names is reviewed, from the version that licence holds
check(set(pr.LINUX_BUNDLED_SOURCES) >= {"libsdl2", "flac", "libogg", "libsamplerate", "libdecor-0"}, "the libraries the archive carries have decisions")
check(all(d.get("license") and len(d.get("versions", ())) == 2 and d.get("sonames") and "ship_source" in d
          for d in pr.LINUX_BUNDLED_SOURCES.values()), "each decision names a licence, a version range, its SONAMEs and a source decision")

# --- the common licence texts a Debian copyright file points to, read past prose punctuation
avail = {"Apache-2.0", "GPL-2", "GPL-3", "GPL", "LGPL-2.1"}
refs = pr.common_license_refs("see /usr/share/common-licenses/Apache-2.0.\nand /usr/share/common-licenses/GPL-3.\n"
                              "(/usr/share/common-licenses/LGPL-2.1), /usr/share/common-licenses/GPL-3.0, "
                              "/usr/share/common-licenses/GPL-2.99. /usr/share/common-licenses/../../../etc/passwd "
                              "/usr/share/common-licenses/Nope.", avail)
check(refs == (["Apache-2.0", "GPL-3", "LGPL-2.1"], ["../../../etc/passwd", "GPL-2.99", "Nope"]),
      "exact names past punctuation and known aliases are found; a near miss or a path is reported, never guessed", refs)

# --- Linux: ldd output, including a path with a space in it
lddout = ("\tlinux-vdso.so.1 (0x00007ffd)\n"
          "\tlibext.so.1 => /tmp/Saturn Explorer/lib/libext.so.1 (0x00007f1234)\n"
          "\tlibmissing.so.2 => not found\n"
          "\tlibc.so.6 => /lib/x86_64-linux-gnu/libc.so.6 (0x00007f5678)\n")
check(pr.parse_ldd("\t/lib64/ld-linux-x86-64.so.2 (0x00007f00)\n") == {"ld-linux-x86-64.so.2": "/lib64/ld-linux-x86-64.so.2"},
      "the program interpreter's bare-path line is indexed by its name")
check(pr.parse_ldd(lddout) == {"libext.so.1": "/tmp/Saturn Explorer/lib/libext.so.1", "libmissing.so.2": None,
                               "libc.so.6": "/lib/x86_64-linux-gnu/libc.so.6"}, "ldd lines parse, spaces and 'not found' included", pr.parse_ldd(lddout))

# --- Debian source: the .dsc lists every file; a missing or altered one stops the packaging
dsc = ("Format: 3.0 (quilt)\nSource: flac\nChecksums-Sha256:\n"
       " " + "0" * 64 + " 5 flac_1.orig.tar.xz\n"
       " " + "1" * 64 + " 3 flac_1-1.debian.tar.xz\nFiles:\n abc 5 flac_1.orig.tar.xz\n")
check(pr.parse_dsc_members(dsc) == {"flac_1.orig.tar.xz": (5, "0" * 64), "flac_1-1.debian.tar.xz": (3, "1" * 64)},
      "the .dsc's Checksums-Sha256 members are read", pr.parse_dsc_members(dsc))
scratch = tempfile.mkdtemp(prefix="se_dsc_")
try:
    import hashlib
    files = {"flac_1.orig.tar.xz": b"orig!", "flac_1-1.debian.tar.xz": b"deb"}
    for n, b in files.items():
        with open(os.path.join(scratch, n), "wb") as f:
            f.write(b)
    good = "Checksums-Sha256:\n" + "".join(f" {hashlib.sha256(b).hexdigest()} {len(b)} {n}\n" for n, b in files.items())
    with open(os.path.join(scratch, "flac_1-1.dsc"), "w") as f:
        f.write(good)
    pr.check_dsc_complete(scratch, ["flac_1-1.dsc"] + list(files))
    check(True, "a complete source package passes")
    for names, what in ((["flac_1-1.dsc", "flac_1.orig.tar.xz"], "a member apt did not name"),
                        (["flac_1.orig.tar.xz"], "no .dsc")):
        try:
            pr.check_dsc_complete(scratch, names)
            check(False, f"{what} must be an error")
        except SystemExit:
            pass
    with open(os.path.join(scratch, "flac_1-1.debian.tar.xz"), "wb") as f:
        f.write(b"DEB")
    try:
        pr.check_dsc_complete(scratch, ["flac_1-1.dsc"] + list(files))
        check(False, "a member whose digest differs from the .dsc must be an error")
    except SystemExit:
        pass
finally:
    shutil.rmtree(scratch, ignore_errors=True)

# --- a source URL's file name is decoded first, then refused if it could leave the folder
check(pr.safe_source_name("http://archive/pool/f/flac/flac_1.4.3%2bds.orig.tar.xz") == "flac_1.4.3+ds.orig.tar.xz", "a %-encoded name is decoded")
for url in ("http://x/a%2f..%2f..%2fevil", "http://x/%2e%2e", "http://x/", "http://x/a%5cb", "http://x/c%3a"):
    try:
        pr.safe_source_name(url)
        check(False, f"{url} must be refused")
    except SystemExit:
        pass

# --- the written offer names the Microsoft runtime only where it ships
check(mso.closed_source_note([{"name": "Microsoft Visual C++ runtime", "note": "not open source; redistributed"}]) ==
      " (every entry except Microsoft Visual C++ runtime, which is not open source)", "the Windows offer excepts the runtime")
check(mso.closed_source_note([{"name": "libsdl2-2.0-0", "note": "Debian/Ubuntu package"}]) == "" and mso.closed_source_note(None) == "",
      "a Linux or macOS offer has no exception")

# --- the library section names what was copied
text = mso.libraries_text([{"name": "gettext", "version": "1.0", "license": "LGPL-2.1-or-later", "files": ["libintl.8.dylib"],
                            "source": "https://example/gettext-1.0.tar.gz", "sha256": "ab", "included": "third-party-source/gettext-1.0.tar.gz"}])
check("gettext 1.0" in text and "third-party-source/gettext-1.0.tar.gz" in text and "libintl.8.dylib" in text, "libraries_text lists version, file and source")

# --- macOS: an @rpath dependency is bundled, and the package stands alone
if sys.platform == "darwin" and shutil.which("clang"):
    work = tempfile.mkdtemp(prefix="se_rpath_")
    try:
        os.makedirs(os.path.join(work, "ext"))
        os.makedirs(os.path.join(work, "app"))
        with open(os.path.join(work, "ext", "ext.c"), "w") as f:
            f.write("int ext_fn(void){return 7;}\n")
        with open(os.path.join(work, "app", "main.c"), "w") as f:
            f.write("int ext_fn(void); int main(void){return ext_fn()==7?0:1;}\n")
        subprocess.run(["clang", "-dynamiclib", "-install_name", "@rpath/libext.dylib", os.path.join(work, "ext", "ext.c"),
                        "-o", os.path.join(work, "ext", "libext.dylib")], check=True)
        subprocess.run(["clang", os.path.join(work, "app", "main.c"), "-L" + os.path.join(work, "ext"), "-lext",
                        "-Wl,-rpath," + os.path.join(work, "ext"), "-o", os.path.join(work, "app", "main")], check=True)
        pkg = os.path.join(work, "pkg")
        os.makedirs(pkg)
        exe = os.path.join(pkg, "main")
        shutil.copy2(os.path.join(work, "app", "main"), exe)
        copied = pr.bundle_dylibs(exe, os.path.join(work, "app", "main"), os.path.join(pkg, "lib"), "-")
        check([os.path.basename(c) for c in copied] == ["libext.dylib"], "the @rpath library was found and copied")
        check(os.path.exists(os.path.join(pkg, "lib", "libext.dylib")), "...into the package")
        check(pr.macho_rpaths(exe) == [], "the build machine's rpath was removed")
        shutil.rmtree(os.path.join(work, "ext"))
        check(subprocess.run([exe]).returncode == 0, "the packaged program runs with the original library gone")
        try:
            os.makedirs(os.path.join(work, "bad"))
            bad = os.path.join(work, "bad", "main")
            shutil.copy2(os.path.join(work, "app", "main"), bad)     # its @rpath target no longer exists
            pr.bundle_dylibs(bad, bad, os.path.join(work, "bad", "lib"), "-")
            check(False, "an unresolvable @rpath dependency must be an error")
        except SystemExit:
            pass
    finally:
        shutil.rmtree(work, ignore_errors=True)

if sys.platform == "darwin" and shutil.which("clang"):
    work = tempfile.mkdtemp(prefix="se_chain_")
    try:
        def cc(*a):
            subprocess.run(["clang", *a], check=True)
        for sub in ("c", "b", "a", "app"):
            os.makedirs(os.path.join(work, sub))
        def w(rel, text):
            with open(os.path.join(work, rel), "w") as f:
                f.write(text)
        # exe -> A -> B -> C. B finds C only through a runpath that A supplies, as dyld allows.
        w("c/c.c", "int c_fn(void){return 3;}\n")
        cc("-dynamiclib", "-install_name", "@rpath/libc.dylib", os.path.join(work, "c/c.c"), "-o", os.path.join(work, "c/libc.dylib"))
        w("b/b.c", "int c_fn(void); int b_fn(void){return c_fn()+4;}\n")
        cc("-dynamiclib", "-install_name", "@rpath/libb.dylib", os.path.join(work, "b/b.c"), "-L" + os.path.join(work, "c"), "-lc",
           "-o", os.path.join(work, "b/libb.dylib"))                       # no rpath of its own for libc
        w("a/a.c", "int b_fn(void); int a_fn(void){return b_fn();}\n")
        cc("-dynamiclib", "-install_name", "@rpath/liba.dylib", os.path.join(work, "a/a.c"), "-L" + os.path.join(work, "b"), "-lb",
           "-Wl,-rpath," + os.path.join(work, "b"), "-Wl,-rpath," + os.path.join(work, "c"), "-o", os.path.join(work, "a/liba.dylib"))
        w("app/main.c", "int a_fn(void); int main(void){return a_fn()==7?0:1;}\n")
        cc(os.path.join(work, "app/main.c"), "-L" + os.path.join(work, "a"), "-la", "-Wl,-rpath," + os.path.join(work, "a"),
           "-Wl,-rpath," + os.path.join(work, "b"), "-o", os.path.join(work, "app/main"))
        pkg = os.path.join(work, "pkg")
        os.makedirs(pkg)
        exe = os.path.join(pkg, "main")
        shutil.copy2(os.path.join(work, "app/main"), exe)
        copied = pr.bundle_dylibs(exe, os.path.join(work, "app/main"), os.path.join(pkg, "lib"), "-")
        check(sorted(os.path.basename(c) for c in copied) == ["liba.dylib", "libb.dylib", "libc.dylib"], "a runpath inherited down the chain finds every library")
        for sub in ("a", "b", "c"):
            shutil.rmtree(os.path.join(work, sub))
        check(subprocess.run([exe]).returncode == 0, "the chain runs from the package alone")
        # a dylib's own install id is not a dependency, even when it names a file that does not exist
        os.makedirs(os.path.join(work, "d"))
        w("d/d.c", "int d_fn(void){return 1;}\n")
        cc("-dynamiclib", "-install_name", "/nonexistent/old/libd.dylib", os.path.join(work, "d/d.c"), "-o", os.path.join(work, "d/libd.dylib"))
        check("/nonexistent/old/libd.dylib" not in pr.macho_deps(os.path.join(work, "d/libd.dylib")), "the install id is not reported as a dependency")
        check(pr.macho_id(os.path.join(work, "d/libd.dylib")) == "/nonexistent/old/libd.dylib", "...and is still readable as the id")
    finally:
        shutil.rmtree(work, ignore_errors=True)

# --- Linux: libraries are copied in by soname and found through the new runpaths, under a path with a space in it.
# The chain exe -> libmid -> libleaf resolves libleaf only through the DT_RPATH the executable passes down, and
# libmid is also asked for under a second name, which the package keeps as a link.
if sys.platform.startswith("linux") and shutil.which("cc") and shutil.which("patchelf"):
    work = tempfile.mkdtemp(prefix="se elf ")
    try:
        ext = os.path.join(work, "ext dir")
        for sub in (ext, os.path.join(work, "app"), os.path.join(work, "pkg")):
            os.makedirs(sub)
        def w(name, text):
            with open(os.path.join(work, name), "w") as f:
                f.write(text)
        def cc(*a):
            subprocess.run(["cc", *a], check=True)
        w("leaf.c", "int leaf_fn(void){return 3;}\n")
        w("mid.c", "int leaf_fn(void); int mid_fn(void){return leaf_fn()+4;}\n")
        w("main.c", "int mid_fn(void); int main(void){return mid_fn()==7?0:1;}\n")
        cc("-shared", "-fPIC", "-Wl,-soname,libleaf.so.1", os.path.join(work, "leaf.c"), "-o", os.path.join(ext, "libleaf.so.1"))
        cc("-shared", "-fPIC", "-Wl,-soname,libmid.so.1", os.path.join(work, "mid.c"), "-L" + ext, "-l:libleaf.so.1",
           "-o", os.path.join(ext, "libmid.so.1"))                     # no runpath of its own for libleaf
        os.symlink("libmid.so.1", os.path.join(ext, "libmidalias.so.1"))
        # DT_RPATH (--disable-new-dtags), which the loader also applies to the executable's libraries
        cc(os.path.join(work, "main.c"), "-L" + ext, "-l:libmid.so.1", "-Wl,--disable-new-dtags",
           "-Wl,-rpath," + ext, "-o", os.path.join(work, "app", "main"))
        subprocess.run(["patchelf", "--add-needed", "libmidalias.so.1", os.path.join(work, "app", "main")], check=True)
        exe = os.path.join(work, "pkg", "main")
        shutil.copy2(os.path.join(work, "app", "main"), exe)
        copied = pr.bundle_elf_libs(exe, os.path.join(work, "app", "main"), os.path.join(work, "pkg", "lib"))
        check(sorted(copied.values()) == ["libleaf.so.1", "libmid.so.1"], "the chain was copied under its sonames", copied)
        check(os.path.islink(os.path.join(work, "pkg", "lib", "libmidalias.so.1")), "the second name is kept as a link")
        check("libc.so.6" not in copied.values(), "the C library stays out")
        shutil.rmtree(ext)
        check(subprocess.run([exe]).returncode == 0, "the packaged program runs with the originals gone")
        check(pr.ldd_map(exe).get("libleaf.so.1", "").startswith(os.path.join(work, "pkg", "lib")), "...loading the packaged copies")
        # Two libraries whose names cross: one asked for as alias.so (SONAME canonical.so), the other as second.so
        # (SONAME alias.so). A link for the first under alias.so must not be overwritten by the second's copy.
        cross = os.path.join(work, "cross")
        os.makedirs(cross)
        w("one.c", "int one_fn(void){return 3;}\n")
        w("two.c", "int two_fn(void){return 4;}\n")
        w("main2.c", "int one_fn(void); int two_fn(void); int main(void){return one_fn()+two_fn()==7?0:1;}\n")
        cc("-shared", "-fPIC", "-Wl,-soname,canonical.so", os.path.join(work, "one.c"), "-o", os.path.join(cross, "canonical.so"))
        os.symlink("canonical.so", os.path.join(cross, "alias.so"))
        cc("-shared", "-fPIC", "-Wl,-soname,alias.so", os.path.join(work, "two.c"), "-o", os.path.join(cross, "second.so"))
        cc(os.path.join(work, "main2.c"), os.path.join(cross, "canonical.so"), os.path.join(cross, "second.so"),
           "-Wl,-rpath," + cross, "-o", os.path.join(work, "main2"))
        subprocess.run(["patchelf", "--replace-needed", "canonical.so", "alias.so", "--replace-needed", "alias.so", "second.so",
                        os.path.join(work, "main2")], check=True)
        if subprocess.run([os.path.join(work, "main2")]).returncode == 0:
            try:
                pr.bundle_elf_libs(os.path.join(work, "main2"), os.path.join(work, "main2"), os.path.join(work, "crosslib"))
                check(False, "a name standing for two different libraries must be an error")
            except SystemExit as e:
                check("would name both" in str(e) or "both called" in str(e), "...naming the clash", str(e))
        else:
            print("skip: could not build the crossed-names fixture")
        try:
            bad = os.path.join(work, "bad")
            shutil.copy2(os.path.join(work, "app", "main"), bad)     # its libraries no longer exist
            pr.bundle_elf_libs(bad, bad, os.path.join(work, "badlib"))
            check(False, "an unresolvable dependency must be an error")
        except SystemExit:
            pass
    finally:
        shutil.rmtree(work, ignore_errors=True)

print("PackageRelease: all checks passed" if failures == 0 else f"{failures} check(s) failed")
sys.exit(0 if failures == 0 else 1)
