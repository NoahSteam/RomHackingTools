#!/usr/bin/env python3
"""Print the per-patch review table (markdown) for the area 10 record.

    python3 -I patch_stack.py <ghidra> <work> --json stack.json
    python3 -I review_table.py stack.json > table.md

Joins the replay of the stack (lines each patch added that survive in the final tree, lines of it
that later patches rewrite, functions several patches touch) with review_notes.py.
"""
import json
import os
import sys

sys.dont_write_bytecode = True
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from review_notes import N   # noqa: E402


def esc(s):
    return s.replace("|", "\\|")


def main(path):
    rows = json.load(open(path))
    print("| Patch | Changes | Stated reason against pinned upstream | Pinned by | Overlap with other patches | Upstreamable |")
    print("|---|---|---|---|---|---|")
    for r in rows:
        n = r["patch"][:4]
        e = N[n]
        ov = []
        ov.append("+%d lines, %d in the final tree" % (r["added"], r["surviving_added"]))
        if r["undone_by"]:
            ov.append("lines rewritten later by " + ", ".join("%s (%d)" % (k[:4], v) for k, v in sorted(r["undone_by"].items())))
        shared = {}
        for c, ps in r["shared_contexts"].items():
            for q in ps:
                if q != n:
                    shared[q] = shared.get(q, 0) + 1
        if shared:
            ov.append("same functions as " + ", ".join(sorted(shared)))
        if e["overlap"]:
            ov.append(e["overlap"])
        print("| `%s` | %s | %s | %s | %s | %s |" % (r["patch"][:-6], esc(e["what"]), esc(e["holds"]), esc(e["test"]),
                                                   esc("; ".join(ov)), esc(e["up"])))
    missing = [r["patch"][:4] for r in rows if r["patch"][:4] not in N]
    assert not missing, missing


if __name__ == "__main__":
    main(sys.argv[1])
