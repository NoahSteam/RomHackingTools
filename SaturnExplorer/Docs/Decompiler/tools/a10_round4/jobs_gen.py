#!/usr/bin/env python3
"""Area 10 round 4, second pass: jobs.json of per-hunk groups for mut.py.
usage: jobs_gen.py <hunk dir> <patch numbers, comma list> > jobs.json
A group is the hunks of one patch that sit in the same function of the same file (the hunk header's function text),
so a mutant is one route of the patch; "gN" numbers the groups in order. Hunks in .hh files are left out, as in mut.py.
No "named" list: a group is killed when any oracle fails (cheapest first)."""
import json
import os
import re
import sys

hd = sys.argv[1]
jobs = []
for num in sys.argv[2].split(","):
    groups = {}
    order = []
    for f in sorted(os.listdir(hd)):
        if not (f.startswith(num + ".h") and f.endswith(".rev.patch")):
            continue
        lines = open(os.path.join(hd, f)).read().splitlines()
        if any(l.startswith("+++ b/") and l.endswith(".hh") for l in lines):
            continue
        path = next(l for l in lines if l.startswith("+++ b/"))
        fn = re.match(r"@@[^@]*@@(.*)", next(l for l in lines if l.startswith("@@"))).group(1).strip()
        key = (path, fn)
        if key not in groups:
            groups[key] = []
            order.append(key)
        groups[key].append(int(f[6:8]))
    spec = any("/processors/" in k[0] for k in order)
    runs = ["unit", "sem", "gen"] if spec else ["pc", "engine", "unit", "gen"]
    for i, k in enumerate(order, 1):
        jobs.append({"id": "%s-g%d" % (num, i), "patch": num, "hunks": groups[k], "named": [], "runs": runs, "where": k[1]})
json.dump(jobs, sys.stdout, indent=0)
