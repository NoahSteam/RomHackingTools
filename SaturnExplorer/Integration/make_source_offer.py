#!/usr/bin/env python3
"""
make_source_offer.py - the GPL "corresponding source" for a shipped, patched Mednafen.

A release that carries a patched Mednafen binary must come with the source it was built from.
This writes two things next to the archive:

  SOURCE.txt                         what the binary is, which base and patch it was built from,
                                     and how to rebuild it (read from Mednafen/UPSTREAM.json and
                                     the same patch arguments / defines install.py builds with)
  mednafen-se-<ver>-source.tar.xz    (--tarball) the patched tree itself: the pinned fork base with
                                     apply.py already run on it, so the source travels with the
                                     download and no one has to take the offer up

Usage:
  python3 Integration/make_source_offer.py --out dist/SOURCE.txt
  python3 Integration/make_source_offer.py --out dist/SOURCE.txt --tarball dist/mednafen-se-source.tar.xz
  python3 Integration/make_source_offer.py ... --se-ref v1.0 --mednafen-dir /path/to/mednafen-checkout

--mednafen-dir is any git checkout that has the fork's base commit (default: _emu/mednafen, else the
fork is cloned). The fork's tree comes from its committed files, so build products and local edits there
never leak in; the patch itself (apply.py and the files it copies) is read from this working tree, which
package_release.py requires to be clean.
"""
import argparse
import json
import os
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
import urllib.parse

HERE = os.path.dirname(os.path.abspath(__file__))
SE_ROOT = os.path.dirname(HERE)
sys.path.insert(0, HERE)
import install   # noqa: E402  (patch args and defines come from the real build recipe)


def run(cmd, **kw):
    return subprocess.run(cmd, check=True, **kw)


def git_out(args, cwd):
    return subprocess.run(["git", *args], cwd=cwd, check=True, capture_output=True, text=True).stdout.strip()


def load_upstream():
    with open(os.path.join(HERE, "Mednafen", "UPSTREAM.json"), encoding="utf-8") as f:
        return json.load(f)


def se_repo_url():
    """The public URL of this repo: no user, password, query or fragment, whatever the scheme."""
    try:
        url = git_out(["remote", "get-url", "origin"], SE_ROOT)
    except subprocess.CalledProcessError:
        return "(unknown)"
    m = re.match(r"^[\w.-]+@([\w.-]+):(.+)$", url)             # scp-style: git@github.com:org/repo.git
    if m:
        url = f"https://{m.group(1)}/{m.group(2)}"
    else:
        parts = urllib.parse.urlsplit(url)
        if parts.hostname:
            web = parts.scheme in ("http", "https")          # an ssh/git port says nothing about the web address
            host = parts.hostname + (f":{parts.port}" if web and parts.port else "")
            url = f"{parts.scheme if web else 'https'}://{host}{parts.path}"
    return url[:-4] if url.endswith(".git") else url


def source_text(up, se_ref, se_commit, tarball_name, libs=None):
    cfg = " ".join(f"--disable-{c}" for c in install.MEDNAFEN_OTHER_CORES)
    patch_args = " ".join(install.EMULATORS["mednafen"]["patch_args"])
    tarball_line = (f"\nThe patched tree is also included in this release as {tarball_name}\n"
                    "(the base below with apply.py already run on it); building from that needs no other download.\n"
                    if tarball_name else "")
    return f"""Saturn Explorer - corresponding source for the bundled Mednafen
================================================================

The "mednafen" program in this download is a MODIFIED build of Mednafen {up['upstream_version']}
(GNU GPL v2 or later). It is not an official Mednafen release; do not report problems with it to the
Mednafen project. The modification adds a local debugging endpoint that Saturn Explorer connects to.
{tarball_line}
Corresponding source
--------------------
1. Base: Mednafen {up['upstream_version']}, the author's release source, unmodified.
     tarball : {up['upstream_tarball']}
     sha256  : {up['upstream_tarball_sha256']}
     git     : {up['fork_repo']}
               commit {up['fork_base_commit']} (tag {up['fork_base_tag']}), byte-identical to that tarball.
2. Patch: the Saturn Explorer repository, which holds the patcher and the code it injects.
     repo    : {se_repo_url()}
     release : {se_ref}  (commit {se_commit})
     files   : Integration/Mednafen/apply.py, Integration/Mednafen/se_mednafen_glue.c,
               Integration/Common/*   (apply.py copies the last two into the Mednafen tree)

Rebuilding it
-------------
    git clone {up['fork_repo']} mednafen && cd mednafen && git checkout {up['fork_base_commit']}
    python3 <saturn-explorer>/Integration/Mednafen/apply.py . {patch_args}
    autoreconf -i
    CPPFLAGS="{install.MEDNAFEN_DEFINES}" ./configure --enable-debugger --enable-ss {cfg}
    make

(Integration/install.py runs exactly this and adds toolchain-specific flags for Windows/MSYS2.)

{libraries_text(libs)}
Licences for all of the above are in the Licenses folder. The Saturn BIOS and game images are not
included and not distributed by this project.

Written offer: for three years from the date of this release, the corresponding source of the Mednafen
build above and of each open-source library listed above (every entry except the Microsoft runtime, which
is not open source) is available on request through the issue tracker of the repository above.
"""


def libraries_text(libs):
    """The third-party libraries that were copied into the package, from the actual binaries."""
    if not libs:
        return "No third-party libraries are shipped beside it.\n"
    lines = ["Third-party libraries shipped with it", "-------------------------------------"]
    for e in libs:
        lines.append(f"{e['name']} {e['version']} ({e['license']}): {', '.join(e['files'])}")
        lines.append(f"    source : {e['source']}" + (f"  (sha256 {e['sha256']})" if e.get("sha256") else ""))
        if e.get("included"):
            lines.append(f"    in this download: {e['included']}")
        if e.get("note"):
            lines.append(f"    note   : {e['note']}")
    return "\n".join(lines) + "\n"


def fork_repo(up, mednafen_dir, work):
    """A git repo that has the fork's base commit: `mednafen_dir` if it does, else a fresh clone in `work`."""
    commit = up["fork_base_commit"]
    if mednafen_dir and os.path.isdir(os.path.join(mednafen_dir, ".git")) and subprocess.run(
            ["git", "cat-file", "-e", commit + "^{commit}"], cwd=mednafen_dir, capture_output=True).returncode == 0:
        return mednafen_dir
    repo = os.path.join(work, "clone")
    run(["git", "-c", "core.autocrlf=false", "clone", "--quiet", up["fork_repo"], repo])
    return repo


def make_patched_tree(up, mednafen_dir, tree, work):
    """`tree` = the fork's base commit (committed files only) with apply.py run on it: the source the SE
    build is made from. The one definition of that tree, used for the source tarball and for checking that a
    binary's checkout matches it."""
    repo = fork_repo(up, mednafen_dir, work)
    os.makedirs(tree)
    # autocrlf off: `git archive` applies the checkout's line-ending conversion, and a Windows user's global
    # core.autocrlf=true would turn the whole tree CRLF, so it would match neither the LF build checkout
    # (install.py clones with it off) nor the sources the binary was built from.
    archive = subprocess.Popen(["git", "-c", "core.autocrlf=false", "archive", up["fork_base_commit"]],
                               cwd=repo, stdout=subprocess.PIPE)
    run(["tar", "-x", "-C", tree], stdin=archive.stdout)
    archive.stdout.close()
    if archive.wait() != 0:
        raise RuntimeError("git archive failed")
    run([sys.executable, os.path.join(HERE, "Mednafen", "apply.py"), tree,
         *install.EMULATORS["mednafen"]["patch_args"]], stdout=subprocess.DEVNULL)


def build_tarball(up, mednafen_dir, out_path, se_ref):
    """The patched tree as a reproducible tar.xz."""
    work = tempfile.mkdtemp(prefix="se_src_")
    try:
        name = f"mednafen-se-{up['upstream_version']}-{se_ref}"
        tree = os.path.join(work, name)
        make_patched_tree(up, mednafen_dir, tree, work)
        with tarfile.open(out_path, "w:xz") as tf:
            def scrub(ti):          # stable owner, time and modes, so the archive is the same under any umask
                ti.uid = ti.gid = 0
                ti.uname = ti.gname = ""
                ti.mtime = 0
                ti.mode = 0o755 if ti.isdir() or ti.mode & 0o111 else 0o644
                return ti
            tf.add(tree, arcname=name, filter=scrub)
    finally:
        shutil.rmtree(work, ignore_errors=True)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--out", required=True, help="where to write SOURCE.txt")
    ap.add_argument("--tarball", help="also write the patched source tree to this .tar.xz")
    ap.add_argument("--se-ref", help="release tag or label (default: the current commit)")
    ap.add_argument("--mednafen-dir", default=os.path.join(SE_ROOT, "_emu", "mednafen"),
                    help="git checkout holding the fork base commit (default: _emu/mednafen; else cloned)")
    args = ap.parse_args()

    up = load_upstream()
    se_commit = git_out(["rev-parse", "HEAD"], SE_ROOT)
    se_ref = args.se_ref or se_commit[:10]
    if args.tarball:
        build_tarball(up, args.mednafen_dir, args.tarball, se_ref)
    os.makedirs(os.path.dirname(os.path.abspath(args.out)), exist_ok=True)
    with open(args.out, "w", encoding="utf-8", newline="\n") as f:
        f.write(source_text(up, se_ref, se_commit, os.path.basename(args.tarball) if args.tarball else None))
    print(f"wrote {args.out}" + (f" and {args.tarball}" if args.tarball else ""))


if __name__ == "__main__":
    main()
