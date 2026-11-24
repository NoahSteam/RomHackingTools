#!/usr/bin/env python3
"""Link model step 2: the store pins. For each fixture that pins a P2 row of area 7, the store lines whose click target today is not in the
model's set of proven Effects, by class (tests/LinkCensus.h's `store.*` classes), and the ones the model gives no Effect at all.

    p2stores.py LEDGER REPORT     (REPORT: Sh2DecompilerTests' census, SE_LINK_CENSUS=<file>)

Classes: old-is-value / old-is-candidate-value (the lowest token address is an instruction that computed a value, or an engine hint: the
old defect of LINK_MODEL.md section 1 (c)), old-not-linked (an address a pass added, or none), old-is-placement, old-is-candidate-effect (the
old target is a store the model also names but could not prove), no-effect (a store no record leads to: the model's gap).
"""
import collections
import os
import re
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "a7_link1"))
from p2rows import p2_ids  # noqa: E402


def main(argv):
    p2 = p2_ids(argv[1])
    rows = collections.defaultdict(lambda: collections.defaultdict(list))
    for raw in open(argv[2], encoding="utf-8"):
        if not raw.startswith("LABELLED store."):
            continue
        cls, label, where, text = [x.strip() for x in raw[9:].rstrip("\n").split(" | ", 3)]
        m = re.search(r"a7[_ -]?(\d+)", label, re.I)
        if m and int(m.group(1)) in p2:
            rows[int(m.group(1))][cls].append("%s | %s | %s" % (label, where, text))
    total = collections.Counter()
    for n in sorted(rows):
        print("A7-%d" % n)
        for cls, items in sorted(rows[n].items()):
            total[cls] += len(items)
            print("  %s: %d" % (cls, len(items)))
            for it in items[:3]:
                print("      %s" % it)
    print("\nP2-pinned rows, all classes: " + ", ".join("%s %d" % kv for kv in sorted(total.items())))


if __name__ == "__main__":
    main(sys.argv)
