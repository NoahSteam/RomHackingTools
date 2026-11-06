#!/usr/bin/env python3
"""Area 11 round 2: each vendored engine patch reverted alone, as a mutant.

usage: patch_mutants.py <patches dir> <out dir> [--check <repo root>]

For every patches/NNNN-*.patch writes <out>/NNNN.rev.patch: the patch's hunks with the paths moved
from the upstream layout to the vendored one (cpp/, processors/SuperH/), without the hunks that
only add a "Modified for SaturnExplorer" licence line (a comment: reverting it changes nothing,
and in a header it would rebuild the whole engine). Applying the file with `git apply -R` from the
repository root reverts the patch's code in the final tree. With --check, says for each patch
whether that applies alone (a later patch may have changed the same lines: then the mutant is
"overlapped" and needs a hand-made revert, or is not a single-patch mutant).
"""
import os
import re
import subprocess
import sys

VENDOR = "SaturnExplorer/FrontEnd/third_party/ghidra-decompiler"
MAP = [("Ghidra/Features/Decompiler/src/decompile/cpp/", VENDOR + "/cpp/"),
       ("Ghidra/Processors/SuperH/data/languages/", VENDOR + "/processors/SuperH/")]


def remap(path):
    for a, b in MAP:
        if path.startswith(a):
            return b + path[len(a):]
    return path


def split(text):
    """[(file header lines, [hunk lines...]), ...]"""
    files = []
    cur = None
    hunk = None
    for line in text.splitlines(keepends=True):
        if line.startswith("diff --git "):
            cur = [[line], []]
            files.append(cur)
            hunk = None
        elif cur is None:
            continue
        elif line.startswith("@@"):
            hunk = [line]
            cur[1].append(hunk)
        elif hunk is None:
            cur[0].append(line)
        else:
            hunk.append(line)
    return files


def comment_only(hunk):
    """A hunk of the licence header that only adds the notice (and the ' *' line before it)."""
    changed = [l[1:].rstrip("\n") for l in hunk[1:] if l[:1] in "+-"]
    start = int(re.match(r"@@ -(\d+)", hunk[0]).group(1))
    return bool(changed) and start < 40 and all(re.match(r"^\s*\*", l) for l in changed) \
        and any("Modified for SaturnExplorer" in l for l in changed)


def hunk_applies(root, header, hunk, tmp):
    """Does this one hunk revert in the final tree (git apply -R, context cut to one line if need be)?"""
    with open(tmp, "w", encoding="utf-8") as f:
        f.writelines(rewrite_header(header) + hunk)
    for ctx in ("-C3", "-C2", "-C1"):
        r = subprocess.run(["git", "apply", "-R", "--check", "--recount", ctx, tmp], cwd=root,
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        if r.returncode == 0:
            return ctx
    return None


def rewrite_header(lines):
    out = []
    for l in lines:
        m = re.match(r"diff --git a/(\S+) b/(\S+)", l)
        if m:
            out.append("diff --git a/%s b/%s\n" % (remap(m.group(1)), remap(m.group(2))))
        elif l.startswith("--- a/") or l.startswith("+++ b/"):
            out.append(l[:6] + remap(l[6:].rstrip("\n")) + "\n")
        elif l.startswith("index "):
            continue   # the blob ids are upstream's, not this repository's
        else:
            out.append(l)
    return out


def main(argv):
    pdir, odir = argv[1], argv[2]
    root = argv[argv.index("--check") + 1] if "--check" in argv else None
    os.makedirs(odir, exist_ok=True)
    for name in sorted(os.listdir(pdir)):
        if not name.endswith(".patch"):
            continue
        num = name[:4]
        files = split(open(os.path.join(pdir, name), encoding="utf-8", errors="replace").read())
        kinds = set()
        nh = ok = 0
        worst = "-C3"
        # One file per hunk that reverts (NNNN.hK.rev.patch), applied in order by the harness: a hunk
        # a later patch rewrote is left out and counted, so the mutant is "k of n hunks reverted".
        for old in os.listdir(odir):
            if old.startswith(num + "."):
                os.remove(os.path.join(odir, old))
        for header, hunks in files:
            path = re.match(r"diff --git a/(\S+)", header[0]).group(1)
            for h in hunks:
                if comment_only(h):
                    continue
                nh += 1
                kinds.add("spec" if "/languages/" in path else "hh" if path.endswith(".hh") else "cc")
                out = os.path.join(odir, "%s.h%02d.rev.patch" % (num, nh))
                if root:
                    ctx = hunk_applies(root, header, h, out)
                    if not ctx:
                        os.remove(out)
                        continue
                    worst = max(worst, ctx, key=lambda c: -int(c[2:]))
                    with open(out + ".ctx", "w") as f:
                        f.write(ctx)
                else:
                    with open(out, "w", encoding="utf-8") as f:
                        f.writelines(rewrite_header(header) + h)
                ok += 1
        print("%s reverts=%d/%d context=%s kinds=%s %s" % (num, ok, nh, worst, ",".join(sorted(kinds)), name))


if __name__ == "__main__":
    main(sys.argv)
