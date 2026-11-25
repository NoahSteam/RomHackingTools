"""The patcher checks' scratch tree must resolve <mednafen/...> to the PATCHED sources, with or without symlinks.

check_migration.make_scratch() lays out a mini Mednafen tree; its include/mednafen used to be link_or_copy(work/src), so
where symlinks are unavailable (Windows without the privilege) the copy was taken BEFORE apply.py ran and the compile
checks saw pristine headers (the F5 patch adds IdleCycleCount to m68k.h; sound.cpp includes it by <mednafen/...>).

This builds a tiny git repo, runs the real make_scratch() with os.symlink working and then forced to fail, applies the
real M68K_IDLE_MEMBER edit through apply.py's process_anchored(), and compiles a TU that includes the header by its
<mednafen/...> path and uses the added member, with the checks' own include flags (check_migration.include_flags).
It also re-edits the header after a first compile, since a mirror must follow every later apply/revert/mutation.
No Mednafen checkout needed. Usage: check_scratch_includes.py <Integration/Mednafen dir> <scratch dir>"""
import importlib.util
import os
import shutil
import subprocess
import sys
from unittest import mock

mdir, scratch = os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2])


def load(name, path):
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


cm = load("se_check_migration", os.path.join(mdir, "check_migration.py"))
ap = load("se_apply_inc", os.path.join(mdir, "apply.py"))

FILES = {
    "src/ss/x.cpp": "int x;\n",
    "src/ss/Makefile.am": "libss_a_SOURCES = ss/vdp1.cpp\n",
    "src/drivers/video.cpp": "int v;\n",
    "src/hw_cpu/m68k/m68k.h": "struct M68K\n{\n uint32 timestamp;\n uint32 XPending;\n};\n",
    "src/hw_cpu/m68k/m68k.cpp": "int m;\n",
    "src/hw_cpu/z80/z80.h": "int z;\n",
    "src/types.h": "typedef unsigned uint32;\n",
    "include/other.h": "int o;\n",
    "configure.ac": "MEDNAFEN_VERSION='1'\n",
}
TU = ("#include <mednafen/types.h>\n#include <mednafen/hw_cpu/m68k/m68k.h>\n"
      "int main() { M68K m; m.IdleCycleCount = 1; return (int)m.IdleCycleCount - 1; }\n")


def make_repo(root):
    shutil.rmtree(root, ignore_errors=True)
    for rel, text in FILES.items():
        p = os.path.join(root, rel)
        os.makedirs(os.path.dirname(p), exist_ok=True)
        with open(p, "w", newline="") as f:
            f.write(text)
    env = dict(os.environ, GIT_AUTHOR_NAME="t", GIT_AUTHOR_EMAIL="t@t", GIT_COMMITTER_NAME="t", GIT_COMMITTER_EMAIL="t@t")
    for cmd in (["init", "-q"], ["add", "-A"], ["commit", "-q", "-m", "x"]):
        subprocess.run(["git", "-C", root] + cmd, check=True, env=env, capture_output=True)


def no_symlink(*a, **k):
    raise OSError("symlinks unavailable (test)")


def compile_tu(work, label):
    tu = os.path.join(scratch, label + "-tu.cpp")
    with open(tu, "w") as f:
        f.write(TU)
    return subprocess.run([os.environ.get("CXX", "g++"), "-std=gnu++11", "-fsyntax-only"] + cm.include_flags(work) + [tu],
                          capture_output=True, text=True)


def case(label, symlinks):
    root = os.path.join(scratch, label + "-repo")
    make_repo(root)
    if symlinks:
        work = cm.make_scratch(root, os.path.join(scratch, label))
    else:
        with mock.patch("os.symlink", no_symlink):
            work = cm.make_scratch(root, os.path.join(scratch, label))
    is_link = os.path.islink(os.path.join(work, "include", "mednafen"))
    if is_link != symlinks:
        return "make_scratch made include/mednafen %s, expected %s" % ("a copy" if not is_link else "a link", "a link" if symlinks else "a copy")
    if compile_tu(work, label).returncode == 0:
        return "the pristine header already has IdleCycleCount (bad test tree)"
    notes = ap.process_anchored(os.path.join(work, "src", "hw_cpu", "m68k"), "m68k.h", "idle",
                                [(ap.M68K_IDLE_MEMBER_ANCHOR, ap.M68K_IDLE_MEMBER, "IdleCycleCount;")], True)
    if "IdleCycleCount" not in open(os.path.join(work, "src/hw_cpu/m68k/m68k.h")).read():
        return "the real M68K_IDLE_MEMBER edit did not land: %r" % notes
    r = compile_tu(work, label)
    if r.returncode:
        return "<mednafen/...> did not resolve to the patched header:\n" + r.stderr[-800:]
    # A later change (a revert, a mutation) must be seen too.
    path = os.path.join(work, "src/hw_cpu/m68k/m68k.h")
    with open(path, "rb") as f:
        data = f.read()
    with open(path, "wb") as f:
        f.write(data.replace(b"IdleCycleCount", b"RenamedCount"))
    if compile_tu(work, label).returncode == 0:
        return "a later edit of work/src was not seen through <mednafen/...> (stale mirror)"
    return None


def symlinks_available():
    """Whether this host can make a symlink at all (Windows without the privilege cannot)."""
    probe = os.path.join(scratch, "symlink-probe")
    try:
        os.symlink(scratch, probe)
    except (OSError, NotImplementedError):
        return False
    os.remove(probe)
    return True


def mirror_keeps_files():
    """sync_tree with a destination spelled un-normalised (as Windows spells one given with "/"): the files it
    has just copied must survive its own clean-up pass, and a stale extra file must still be removed."""
    src, dst = os.path.join(scratch, "sync-src"), os.path.join(scratch, "sync-dst")
    os.makedirs(os.path.join(src, "sub"), exist_ok=True)
    os.makedirs(os.path.join(dst, "sub"), exist_ok=True)
    for rel in ("a.h", os.path.join("sub", "b.h")):
        with open(os.path.join(src, rel), "w") as f:
            f.write("x")
    with open(os.path.join(dst, "sub", "stale.h"), "w") as f:
        f.write("x")
    cm.sync_tree(src, dst + os.sep + "." + os.sep + "sub" + os.sep + "..")
    have = sorted(os.path.relpath(os.path.join(d, n), dst) for d, _s, ns in os.walk(dst) for n in ns)
    want = sorted(["a.h", os.path.join("sub", "b.h")])
    return None if have == want else "sync_tree left %r, expected %r" % (have, want)


os.makedirs(scratch, exist_ok=True)
cxx = os.environ.get("CXX", "g++")
if shutil.which(cxx) is None:
    print("SKIP: no C++ compiler (%s) on PATH; this check compiles against the scratch include tree" % cxx)
    sys.exit(77)
bad = 0
err = mirror_keeps_files()
print("mirror: %s" % ("ok" if err is None else "FAIL " + err))
bad += err is not None
for label, symlinks in (("symlinks", True), ("copies", False)):
    # The link-specific case needs real symlinks; the forced-copy case always runs.
    if symlinks and not symlinks_available():
        print("%s: skipped (this host cannot create symlinks)" % label)
        continue
    err = case(label, symlinks)
    print("%s: %s" % (label, "ok" if err is None else "FAIL " + err))
    bad += err is not None
sys.exit(1 if bad else 0)
