#!/usr/bin/env python3
"""Link model step 1: the disagreements of the fixtures that pin P2 rows.

    p2rows.py LEDGER REPORT

REPORT is Sh2DecompilerTests' census (SE_LINK_CENSUS=<file>, tests/LinkCensus.h): its `LABELLED class | label | line | text` lines list every
line whose first proven Effect is not its lowest token address, for each function a fixture audited under a label. A label that names a ledger
row (`a7_19`, `a7_23 piece`) belongs to that row; the rows whose severity is P2 are the ones LINK_MODEL.md section 4 holds to "zero unexplained".
Prints each P2 row's disagreements by class, so each can be explained or fixed.
"""
import collections
import re
import sys


def p2_ids(ledger):
    ids = set()
    for line in open(ledger):
        cells = [c.strip() for c in line.split("|")]
        if len(cells) > 4 and re.fullmatch(r"A7-\d+", cells[1]) and cells[3] == "P2":
            ids.add(int(cells[1][3:]))
    return ids


def main(argv):
    p2 = p2_ids(argv[1])
    rows = collections.defaultdict(lambda: collections.defaultdict(list))
    other = collections.Counter()
    for raw in open(argv[2]):
        if not raw.startswith("LABELLED "):
            continue
        cls, label, where, text = [x.strip() for x in raw[9:].rstrip("\n").split(" | ", 3)]
        m = re.search(r"a7[_ -]?(\d+)", label, re.I)
        if m and int(m.group(1)) in p2:
            rows[int(m.group(1))][cls].append("%s | %s | %s" % (label, where, text))
        else:
            other[cls] += 1
    print("P2 rows with a fixture label: %d of %d P2 rows; disagreements on others: %d" % (len(rows), len(p2), sum(other.values())))
    for n in sorted(rows):
        print("A7-%d" % n)
        for cls, items in sorted(rows[n].items()):
            print("  %s: %d" % (cls, len(items)))
            for it in items[:1]:
                print("      %s" % it)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
