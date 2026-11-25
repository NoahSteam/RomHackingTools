#!/usr/bin/env python3
"""Does re-applying this apply.py to a tree an OLDER apply.py already patched come out right?

Integration/install.py keeps the patched Mednafen checkout and applies again over it, and upsert()
treats a fence whose key is already present as done. So any hook this apply.py drops or renames has to
be removed explicitly (RETIRED_FENCES), or the old text stays: for the v26 elapsed-VDP1 hook that is a
compile error. This script proves the migration against a real Mednafen source:

  1. take the pristine bytes of every file the patch touches from the checkout's git HEAD (the
     checkout is the pinned import commit, so HEAD is the unmodified source);
  2. patch a scratch copy with an OLD apply.py (any number of them: e.g. the one from 4b6cd9e and the
     one from 2352058, `git show <rev>:SaturnExplorer/Integration/Mednafen/apply.py > old.py`);
  3. patch it again with the CURRENT apply.py and check: no ANCHOR MISS, no retired text left, every
     hook of F4/F5/F8 present exactly once, applying a third time changes nothing, and the patched
     vdp1.cpp / sound.cpp / m68k.cpp / ss.cpp still parse (g++ -fsyntax-only, with the real headers);
  4. --revert restores the scratch tree to the pristine bytes: EVERY file, compared as a whole-tree
     snapshot (so a file apply.py touches that this script's lists forgot is caught too, and so is a
     file left behind); applying again afterwards gives exactly the tree the migrating apply gave, and
     a second apply changes nothing; the same with --with-pause.

The Mednafen source is not in this repository (it is a generated, untracked checkout), so this is a
script, not a ctest. The part that needs no source -- the migration logic on a synthetic file -- is
Integration/tests/check_apply_migration.py, which does run in ctest.

Usage: check_migration.py <mednafen root (git checkout)> <scratch dir> <old apply.py> [<old apply.py> ...]
Exit status 0 = all checks passed."""
import importlib.util
import os
import re
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
NEW_APPLY = os.path.join(HERE, "apply.py")

# Files the patch edits, relative to the Mednafen root (for the "third apply changes nothing" and the
# per-file revert checks; --revert is additionally checked over the whole scratch tree, see snapshot()).
EDITED = ["src/ss/vdp1.cpp", "src/ss/vdp2.cpp", "src/ss/ss.cpp", "src/ss/scu.inc", "src/ss/debug.inc",
          "src/ss/sound.cpp", "src/ss/scsp.h", "src/ss/cdb.cpp", "src/ss/smpc.cpp", "src/ss/smpc.h",
          "src/ss/sh7095.inc", "src/ss/sh7095_ops.inc", "src/hw_cpu/m68k/m68k.h", "src/hw_cpu/m68k/m68k.cpp",
          "src/drivers/video.cpp", "src/drivers/input.cpp", "src/ss/Makefile.am"]

# (file, key, how many fences must contain it, apply.py attribute that exists once the item has landed)
# for what this apply.py writes in the F4/F5/F8 areas; an entry whose attribute is absent is skipped.
EXPECT = [
    ("src/ss/vdp1.cpp", "SeSsVdp1Debit", 1, "VDP1_EAT_HOOK"),
    ("src/ss/vdp1.cpp", "SE_PERF_VDP1_DMA_DISCARDED", 1, "VDP1_PERF_DMA_HOOK"),
    ("src/ss/vdp1.cpp", "SE_PERF_VDP1_COMMANDS", 1, "VDP1_EAT_HOOK"),
    ("src/ss/sound.cpp", "se_perf_idle0 =", 1, "M68K_IDLE_STEP"),
    ("src/ss/sound.cpp", "SE_PERF_M68K_IDLE_CYCLES", 1, "M68K_IDLE_STEP"),
    ("src/hw_cpu/m68k/m68k.h", "IdleCycleCount;", 1, "M68K_IDLE_STEP"),
    ("src/hw_cpu/m68k/m68k.cpp", "IdleCycleCount = 0;", 1, "M68K_IDLE_STEP"),
    ("src/hw_cpu/m68k/m68k.cpp", "IdleCycleCount += 4;", 1, "M68K_IDLE_STEP"),
    ("src/ss/ss.cpp", "SeSsCacheMode =", 1, "SS_CACHE_MODE_HOOK"),
    ("src/ss/sh7095.inc", "SE_PERF_BUS_READ", 1, "VDP1_EAT_HOOK"),
    ("src/ss/sh7095.inc", "SE_PERF_BUS_WRITE", 1, "VDP1_EAT_HOOK"),
    ("src/ss/scu.inc", "SE_PERF_DMA_ROUTE_BYTES", 1, "VDP1_EAT_HOOK"),
    ("src/ss/scu.inc", "SE_PERF_DSP_CYCLES", 1, "VDP1_EAT_HOOK"),
]
GONE = [("src/ss/vdp1.cpp", "SE_PERF_VDP1_DRAW_CYCLES"), ("src/ss/sound.cpp", "se_perf_t0", "M68K_IDLE_STEP")]
SYNTAX = ["src/ss/vdp1.cpp", "src/ss/sound.cpp", "src/hw_cpu/m68k/m68k.cpp", "src/ss/ss.cpp"]


def load_new():
    spec = importlib.util.spec_from_file_location("se_apply_new", NEW_APPLY)
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def git_show(root, rel):
    return subprocess.run(["git", "-C", root, "show", "HEAD:" + rel], capture_output=True, check=True).stdout


def fences_containing(text, key, mod):
    return sum(1 for m in re.finditer(re.escape(mod.BEGIN) + r".*?" + re.escape(mod.END), text, re.S)
               if key in m.group(0))


def is_checkout_copy(rel):
    """True for a scratch path that is the checkout's own unpatched content (a symlink, or its copy where symlinks
    are unavailable), or the include/mednafen mirror of the patched src: not the scratch tree's own files."""
    parts = rel.replace(os.sep, "/").split("/")
    if parts[0] in ("include", "intl"):
        return True
    if parts[0] == "src" and len(parts) > 1 and parts[1] not in ("ss", "hw_cpu", "drivers"):
        return True
    return parts[:2] == ["src", "hw_cpu"] and len(parts) > 2 and parts[2] != "m68k"


def snapshot(work):
    """Every regular file of the scratch tree's own content (symlinked directories are not entered; copies of the
    checkout's unpatched dirs, made where symlinks are unavailable, are skipped), by relative path."""
    out = {}
    for dirpath, dirnames, filenames in os.walk(work):
        dirnames[:] = [d for d in dirnames if not is_checkout_copy(os.path.relpath(os.path.join(dirpath, d), work))]
        for name in filenames:
            p = os.path.join(dirpath, name)
            if not os.path.islink(p) and not is_checkout_copy(os.path.relpath(p, work)):
                with open(p, "rb") as f:
                    out[os.path.relpath(p, work)] = f.read()
    return out


def diff_snapshots(a, b):
    return sorted(k for k in set(a) | set(b) if a.get(k) != b.get(k))


def run_apply(script, root, *flags):
    p = subprocess.run([sys.executable, "-I", script, root] + list(flags), capture_output=True, text=True)
    return p.returncode, p.stdout + p.stderr


def link_or_copy(src, dst):
    """Symlink src at dst; where that is not allowed (Windows without the symlink privilege raises OSError, some
    platforms NotImplementedError) copy it instead. The copy is read-only use: these are headers and
    unpatched sources the harnesses only include."""
    try:
        os.symlink(src, dst)
    except (OSError, NotImplementedError):
        if os.path.isdir(src):
            shutil.copytree(src, dst)
        else:
            shutil.copy2(src, dst)


PATCHED_DIRS = ("ss", "drivers", "hw_cpu/m68k")   # the src subtrees apply.py writes; the rest of src is never patched


def sync_tree(src, dst):
    """Make dst a byte-for-byte mirror of the directory src (files only; extra files in dst are removed)."""
    os.makedirs(dst, exist_ok=True)
    want = set()
    for dirpath, _dirs, files in os.walk(src):
        rel = os.path.relpath(dirpath, src)
        os.makedirs(os.path.normpath(os.path.join(dst, rel)), exist_ok=True)
        for name in files:
            a, b = os.path.join(dirpath, name), os.path.normpath(os.path.join(dst, rel, name))
            want.add(b)
            with open(a, "rb") as f:
                data = f.read()
            if not os.path.isfile(b) or open(b, "rb").read() != data:
                with open(b, "wb") as f:
                    f.write(data)
    for dirpath, _dirs, files in os.walk(dst):
        for name in files:
            if os.path.join(dirpath, name) not in want:
                os.remove(os.path.join(dirpath, name))


def refresh_includes(work):
    """Make <mednafen/...> resolve to the CURRENT bytes of work/src. With the include/mednafen symlink that is
    automatic; where symlinks are unavailable include/mednafen is a real directory mirroring the patched subtrees
    of work/src (the only places apply.py writes), re-synced here. Every compile goes through include_flags(),
    which calls this, so no apply / revert / mutation can leave a stale header behind."""
    link = os.path.join(work, "include", "mednafen")
    if os.path.islink(link) or not os.path.isdir(link):
        return
    for sub in PATCHED_DIRS:
        if os.path.isdir(os.path.join(work, "src", sub)):
            sync_tree(os.path.join(work, "src", sub), os.path.join(link, sub))


def include_flags(work):
    """The -I flags every check compiles with to reach headers by <mednafen/...> and <intl/...>."""
    refresh_includes(work)
    return ["-I" + os.path.join(work, "include"), "-I" + os.path.join(work, "intl")]


def make_scratch(root, scratch):
    """A mini Mednafen tree: pristine text of everything the includes need, plus the real headers by
    symlink. Returns the work root."""
    work = os.path.join(scratch, "work")
    if os.path.isdir(work):
        shutil.rmtree(work)
    for sub in ("src/ss", "src/hw_cpu/m68k", "src/drivers"):
        os.makedirs(os.path.join(work, sub), exist_ok=True)
        for name in os.listdir(os.path.join(root, sub)):
            if name.endswith((".cpp", ".h", ".inc", ".am", ".hpp")):
                rel = sub + "/" + name
                try:
                    data = git_show(root, rel)
                except subprocess.CalledProcessError:
                    continue   # untracked (a patched tree's added files): not pristine
                with open(os.path.join(work, rel), "wb") as f:
                    f.write(data)
    shutil.copy(os.path.join(root, "configure.ac"), work) if os.path.isfile(os.path.join(root, "configure.ac")) else None
    # headers, config.h and the rest of src/ that the translation units reach by <mednafen/...>.
    if os.path.exists(os.path.join(root, "intl")):
        link_or_copy(os.path.abspath(os.path.join(root, "intl")), os.path.join(work, "intl"))
    # include/: entry by entry, so that <mednafen/...> resolves into THIS tree's src (the scratch copy the
    # current apply.py patches), not the checkout's -- whose own patch state may be older or absent.
    inc = os.path.join(root, "include")
    if os.path.isdir(inc):
        os.makedirs(os.path.join(work, "include"), exist_ok=True)
        for entry in os.listdir(inc):
            if entry != "mednafen":
                link_or_copy(os.path.abspath(os.path.join(inc, entry)), os.path.join(work, "include", entry))
    for entry in os.listdir(os.path.join(root, "src")):
        if entry not in ("ss", "hw_cpu", "drivers") and not os.path.exists(os.path.join(work, "src", entry)):
            link_or_copy(os.path.abspath(os.path.join(root, "src", entry)), os.path.join(work, "src", entry))
    hw = os.path.join(root, "src", "hw_cpu")
    for entry in os.listdir(hw):
        if entry != "m68k" and not os.path.exists(os.path.join(work, "src/hw_cpu", entry)):
            link_or_copy(os.path.abspath(os.path.join(hw, entry)), os.path.join(work, "src/hw_cpu", entry))
    if os.path.isdir(inc):
        link = os.path.join(work, "include", "mednafen")
        try:
            os.symlink(os.path.abspath(os.path.join(work, "src")), link)
        except (OSError, NotImplementedError):
            # No symlinks: a real directory, mirrored from work/src (see refresh_includes). Its unpatched
            # entries are copied from the checkout once; the patched subtrees are synced before each compile.
            os.makedirs(link)
            for entry in os.listdir(os.path.join(root, "src")):
                if entry not in ("ss", "drivers", "hw_cpu"):
                    link_or_copy(os.path.abspath(os.path.join(root, "src", entry)), os.path.join(link, entry))
            for entry in os.listdir(hw):
                if entry != "m68k":
                    os.makedirs(os.path.join(link, "hw_cpu"), exist_ok=True)
                    link_or_copy(os.path.abspath(os.path.join(hw, entry)), os.path.join(link, "hw_cpu", entry))
            refresh_includes(work)
    return work


def syntax_check(work, rel, cxx):
    d = os.path.dirname(os.path.join(work, rel))
    cmd = [cxx, "-std=gnu++11", "-fsyntax-only", "-fsigned-char", "-fwrapv", "-fexceptions", "-DHAVE_CONFIG_H"] + include_flags(work) + [
           "-I" + os.path.join(work, "src", "ss"), os.path.join(work, rel)]
    return subprocess.run(cmd, cwd=d, capture_output=True, text=True)


def main():
    if len(sys.argv) < 4:
        print(__doc__)
        return 2
    root, scratch, olds = os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2]), sys.argv[3:]
    cxx = os.environ.get("CXX", "g++")
    os.makedirs(scratch, exist_ok=True)
    mod = load_new()
    bad = 0

    def fail(msg):
        nonlocal bad
        print("FAIL: " + msg)
        bad += 1

    for old in olds:
        print("== migrating from %s" % old)
        work = make_scratch(root, scratch)
        pristine = {rel: git_show(root, rel) for rel in EDITED if os.path.isfile(os.path.join(work, rel))}
        whole_pristine = snapshot(work)
        rc, out = run_apply(os.path.abspath(old), work)
        if rc not in (0, 3):
            fail("the old apply.py did not run (status %d):\n%s" % (rc, out[-600:]))
            continue
        for g in GONE:
            rel, key = g[0], g[1]
            # Only informative: a 2352058-style tree never had the VDP1 hook, a 4b6cd9e one did.
            has = key in open(os.path.join(work, rel), errors="replace").read()
            print("   old tree %s %s %s" % (rel, "contains" if has else "lacks", key))
        rc, out = run_apply(NEW_APPLY, work)
        if rc != 0 or "ANCHOR MISS" in out or "MISSING" in out:
            fail("current apply.py reported a miss (status %d):\n%s" % (rc, out[-800:]))
            continue
        for g in GONE:
            rel, key = g[0], g[1]
            if len(g) > 2 and not hasattr(mod, g[2]):
                continue
            if key in open(os.path.join(work, rel), errors="replace").read():
                fail("%s still contains the retired %s" % (rel, key))
        for rel, key, n, attr in EXPECT:
            if not hasattr(mod, attr):
                continue
            c = fences_containing(open(os.path.join(work, rel), errors="replace").read(), key, mod)
            if c != n:
                fail("%s: %d fence(s) contain %r, expected %d" % (rel, c, key, n))
        snap = {rel: open(os.path.join(work, rel), "rb").read() for rel in pristine}
        rc, out = run_apply(NEW_APPLY, work)
        if rc != 0 or "applied" in out or "updated" in out or "retired" in out:
            fail("a third apply was not a no-op:\n%s" % out[-600:])
        for rel in pristine:
            if open(os.path.join(work, rel), "rb").read() != snap[rel]:
                fail("a third apply changed %s" % rel)
        for rel in SYNTAX:
            r = syntax_check(work, rel, cxx)
            if r.returncode:
                fail("%s does not compile after the migration:\n%s" % (rel, r.stderr[-1500:]))
            else:
                print("   compiles: " + rel)
        migrated = snapshot(work)

        def revert_is_exact(what):
            run_apply(NEW_APPLY, work, "--revert")
            for rel, data in pristine.items():
                if open(os.path.join(work, rel), "rb").read() != data:
                    fail("%s: --revert did not restore %s byte for byte" % (what, rel))
            changed = diff_snapshots(whole_pristine, snapshot(work))
            if changed:
                fail("%s: --revert left the tree different from the pristine one in: %s" % (what, ", ".join(changed)))
            else:
                print("   %s: --revert restored all %d files of the tree byte for byte" % (what, len(whole_pristine)))

        revert_is_exact("migrated tree")
        rc, out = run_apply(NEW_APPLY, work)
        if rc != 0 or "ANCHOR MISS" in out:
            fail("applying again after --revert failed:\n%s" % out[-600:])
        again = diff_snapshots(migrated, snapshot(work))
        if again:
            fail("apply after --revert is not the tree the migrating apply gave; differs in: %s" % ", ".join(again))
        else:
            print("   apply after --revert equals the migrated tree")
        revert_is_exact("re-applied tree")
        for flag_set in (["--with-pause"],):
            rc, out = run_apply(NEW_APPLY, work, *flag_set)
            if rc != 0 or "ANCHOR MISS" in out:
                fail("apply %s failed:\n%s" % (flag_set, out[-600:]))
            first = snapshot(work)
            rc, out = run_apply(NEW_APPLY, work, *flag_set)
            if "applied" in out or "updated" in out or diff_snapshots(first, snapshot(work)):
                fail("a second apply %s was not a no-op:\n%s" % (flag_set, out[-600:]))
            revert_is_exact("apply " + " ".join(flag_set))
    print("OK" if not bad else "%d check(s) failed" % bad)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
