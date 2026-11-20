#!/usr/bin/env python3
"""Area 10 round 4: results.jsonl of mut.py as a table. usage: summ.py <results.jsonl> > results.tsv
One row per mutant: id, hunks or edits reverted, verdict (named, other, survived, build-failed, nothing), and per oracle the
test functions (or printed-C pins) that failed."""
import json
import sys

last = {}
for line in open(sys.argv[1]):
    r = json.loads(line)
    last[r["id"]] = r
print("id\treverted\tverdict\tfailing (oracle: function=checks)")
for k in sorted(last):
    r = last[k]
    rv = ",".join(str(x) for x in r.get("applied", [])) or "-"
    if r.get("note"):
        print("%s\t%s\t%s\t" % (k, rv, r["note"]))
        continue
    fails = []
    for o in r["runs"]:
        if o["rc"] not in (0, 77):
            what = ",".join("%s=%d" % kv for kv in list(o["fns"].items())[:5]) or o["first"][:60].replace("\t", " ")
            if o["findings"]:
                what += " " + ";".join(f[1][:30] for f in o["findings"][:3])
            fails.append("%s(rc%d): %s" % (o["oracle"], o["rc"], what))
    v = "named" if r["named_hit"] else ("other" if fails else "survived")
    print("%s\t%s\t%s\t%s" % (k, rv, v, " | ".join(fails)))
