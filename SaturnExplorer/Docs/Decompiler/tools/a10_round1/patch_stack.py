#!/usr/bin/env python3
"""Replay patches/ one commit per patch on top of pinned upstream and report, per patch:
the files and functions it touches, how many of its added lines survive in the final tree,
and which later patches remove or rewrite lines it added (a patch a later one undoes).

    python3 -I patch_stack.py <ghidra-checkout> <work-dir> [--json out.json]

<work-dir> must not exist; it gets a git repository with one commit for upstream and one per
patch, so `git -C <work-dir> log -p` shows any patch applied to its true predecessor.
"""
import importlib.util
import json
import os
import re
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
VENDOR_DIR = os.path.normpath(os.path.join(HERE, "..", "..", "..", "..", "FrontEnd",
                                           "third_party", "ghidra-decompiler"))


def load_vendor():
    spec = importlib.util.spec_from_file_location("vendor_py", os.path.join(VENDOR_DIR, "vendor.py"))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


def git(work, *args, check=True, inp=None):
    r = subprocess.run(["git", "-C", work] + list(args), stdout=subprocess.PIPE,
                       stderr=subprocess.PIPE, input=inp)
    if check and r.returncode != 0:
        raise SystemExit("git %s failed: %s" % (" ".join(args), r.stderr.decode(errors="replace")))
    return r.stdout.decode(errors="replace")


def parse_diff(text):
    """Yield (file, [hunks]) where a hunk is dict(old_start, new_start, ctx, removed=[old line numbers],
    added=[new line numbers])."""
    files = []
    cur = None
    hunk = None
    old = new = 0
    for line in text.split("\n"):
        if line.startswith("diff --git "):
            m = re.match(r"diff --git a/(\S+) b/(\S+)", line)
            cur = {"file": m.group(2), "hunks": []}
            files.append(cur)
            hunk = None
        elif line.startswith("@@") and cur is not None:
            m = re.match(r"@@ -(\d+)(?:,(\d+))? \+(\d+)(?:,(\d+))? @@ ?(.*)", line)
            old, new = int(m.group(1)), int(m.group(3))
            hunk = {"old_start": old, "new_start": new, "ctx": m.group(5).strip(), "removed": [], "added": []}
            cur["hunks"].append(hunk)
        elif hunk is not None:
            if line.startswith("+"):
                hunk["added"].append(new)
                new += 1
            elif line.startswith("-"):
                hunk["removed"].append(old)
                old += 1
            elif line.startswith(" "):
                old += 1
                new += 1
    return files


def blame_map(work, rev, path):
    """line number (1-based) -> commit hash, for path at rev."""
    out = git(work, "blame", "--line-porcelain", "-s", rev, "--", path, check=False)
    res = {}
    lineno = 0
    for ln in out.split("\n"):
        m = re.match(r"^([0-9a-f]{40}) \d+ (\d+)", ln)
        if m:
            res[int(m.group(2))] = m.group(1)
    return res


def main(argv):
    import argparse
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("ghidra")
    ap.add_argument("work")
    ap.add_argument("--json")
    ns = ap.parse_args(argv[1:])
    ghidra, work, jpath = os.path.abspath(ns.ghidra), os.path.abspath(ns.work), ns.json
    if os.path.exists(work):
        raise SystemExit("%s exists" % work)
    v = load_vendor()
    src_cpp = os.path.join(ghidra, v.UPSTREAM_CPP)
    src_spec = os.path.join(ghidra, v.UPSTREAM_SPEC)
    lib_cc = [n + ".cc" for g in (v.CORE, v.DECCORE, v.SLEIGH, v.GLUE) for n in g.split()]
    comp_cc = [n + ".cc" for n in v.SLACOMP.split()]
    headers = v.header_closure(src_cpp, lib_cc + comp_cc)
    os.makedirs(os.path.join(work, v.UPSTREAM_CPP))
    os.makedirs(os.path.join(work, v.UPSTREAM_SPEC))
    for f in lib_cc + comp_cc + headers:
        shutil.copy2(os.path.join(src_cpp, f), os.path.join(work, v.UPSTREAM_CPP, f))
    for f in v.SPEC_FILES:
        shutil.copy2(os.path.join(src_spec, f), os.path.join(work, v.UPSTREAM_SPEC, f))
    git(work, "init", "-q")
    git(work, "config", "user.email", "a@b")
    git(work, "config", "user.name", "x")
    git(work, "config", "core.autocrlf", "false")
    git(work, "add", "-A")
    git(work, "commit", "-q", "-m", "upstream")
    pdir = os.path.join(VENDOR_DIR, "patches")
    patches = sorted(p for p in os.listdir(pdir) if p.endswith(".patch"))
    info = {}
    commit_of = {}
    for p in patches:
        text = open(os.path.join(pdir, p), encoding="utf-8", errors="replace").read()
        git(work, "apply", "--whitespace=nowarn", os.path.join(pdir, p))
        git(work, "add", "-A")
        git(work, "commit", "-q", "-m", p)
        h = git(work, "rev-parse", "HEAD").strip()
        commit_of[h] = p
        files = parse_diff(text)
        info[p] = {"commit": h, "files": [f["file"] for f in files], "hunks": files,
                   "added": sum(len(h_["added"]) for f in files for h_ in f["hunks"]),
                   "removed": sum(len(h_["removed"]) for f in files for h_ in f["hunks"])}
    # undone[N][M]: lines patch N added that patch M removed
    undone = {}
    for i, p in enumerate(patches):
        parent = info[p]["commit"] + "^"
        for f in info[p]["hunks"]:
            rem = [l for h_ in f["hunks"] for l in h_["removed"]]
            if not rem:
                continue
            bm = blame_map(work, parent, f["file"])
            for l in rem:
                c = bm.get(l)
                q = commit_of.get(c)
                if q and q != p:
                    undone.setdefault(q, {}).setdefault(p, 0)
                    undone[q][p] += 1
    # survival of each hunk's added lines in the final tree
    head = git(work, "rev-parse", "HEAD").strip()
    final_blame = {}
    for p in patches:
        for f in info[p]["hunks"]:
            if f["file"] not in final_blame:
                final_blame[f["file"]] = blame_map(work, head, f["file"]) if os.path.exists(
                    os.path.join(work, f["file"])) else {}
    # final line numbers of a patch's added lines are not its own new-side numbers once later
    # patches shift them, so count by blame instead
    surv = {p: 0 for p in patches}
    for fpath, bm in final_blame.items():
        for l, c in bm.items():
            q = commit_of.get(c)
            if q:
                surv[q] += 1
    rows = []
    for p in patches:
        funcs = sorted({"%s: %s" % (os.path.basename(f["file"]), h_["ctx"]) for f in info[p]["hunks"] for h_ in f["hunks"] if h_["ctx"]})
        rows.append({"patch": p, "files": info[p]["files"], "added": info[p]["added"], "removed": info[p]["removed"],
                     "surviving_added": surv[p], "undone_by": undone.get(p, {}), "contexts": funcs})
    # function-context overlap with later patches
    ctx_index = {}
    for r in rows:
        for c in r["contexts"]:
            ctx_index.setdefault(c, []).append(r["patch"][:4])
    for r in rows:
        r["shared_contexts"] = {c: ctx_index[c] for c in r["contexts"] if len(ctx_index[c]) > 1}
    if jpath:
        json.dump(rows, open(jpath, "w"), indent=1)
    for r in rows:
        print("%s files=%d +%d -%d surviving=%d undone_by=%s" % (
            r["patch"][:4], len(r["files"]), r["added"], r["removed"], r["surviving_added"],
            ",".join("%s(%d)" % (k[:4], n) for k, n in sorted(r["undone_by"].items())) or "-"))


if __name__ == "__main__":
    main(sys.argv)
