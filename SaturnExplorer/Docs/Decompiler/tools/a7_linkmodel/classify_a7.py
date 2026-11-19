#!/usr/bin/env python3
"""Classify the area 7 rows of REVIEW_LEDGER.md for LINK_MODEL.md (section 5).

usage: python3 -I classify_a7.py <REVIEW_LEDGER.md> [--rows]

Each row is put in ONE class by the first oracle tag its finding names (`map-...`), else by key words:
  effect   the statement is addressed where the engine put its op, not at the instruction that did its effect
           (a folded store on no line, a store of another value named, a copy at a branch, call or return, a delay slot)
  value    which instruction contributed the value shown (a number at a join, a register merge, a hoisted conversion)
  click    the default target and the order of the way back (lowest address first, materialised copies, hover text)
  other    case labels, bad data, coverage counts, features, rows routed to other areas
and by side: 'oracle' (the finding is a false alarm or a gap of the checker: "(Oracle", "Instrument", or closed by an
oracle rule), 'product', or 'unanalysed'. A heuristic count for the design document, not a re-triage.
"""
import re
import sys
from collections import Counter

TAG_CLASS = [
    ("map-copy-store", "effect"), ("map-frame-store", "effect"), ("map-store-after", "effect"),
    ("map-store-twin", "effect"), ("map-store-dead", "effect"), ("map-store-value", "effect"),
    ("map-access-at-branch", "effect"), ("map-slot-number", "effect"),
    ("map-const-join", "value"), ("map-number-store", "value"), ("map-copy-merge", "value"),
    ("map-conversion-branch", "value"),
    ("map-way-back", "click"), ("map-no-instruction", "click"),
]
WORDS = [
    ("effect", r"on no line|store of another value|delay slot|names? (a|the|no) store|folded store|frame local is assigned|at the end of a block"),
    ("value", r"join|merged? (from|under)|makers?|number line|conversion|constant the engine"),
    ("click", r"way back|firstAddr|hover|lowest|round.trip|materialised|click"),
]


def classify(finding):
    first = None
    for tag, cls in TAG_CLASS:
        at = finding.find(tag)
        if at >= 0 and (first is None or at < first[0]):
            first = (at, cls)
    if first:
        return first[1]
    for cls, pattern in WORDS:
        if re.search(pattern, finding):
            return cls
    return "other"


def side(finding, status):
    head = finding[:160]
    if "(Oracle" in head or head.startswith("Instrument") or "Oracle of area" in head:
        return "oracle"
    if "(Product" in head:
        return "product"
    if re.search(r"Not analysed|not analysed|Provisional|Not reproduced", finding[:400]):
        return "unanalysed"
    if re.search(r"oracle rule \d+", status) and "by the product" not in status:
        return "oracle"
    return "product"


def main():
    rows = []
    for line in open(sys.argv[1], encoding="utf-8"):
        if not line.startswith("| A7-"):
            continue
        cells = [c.strip() for c in re.split(r"(?<!\\)\|", line.strip())[1:-1]]
        if len(cells) != 8:
            print("row with %d cells: %s" % (len(cells), cells[0]), file=sys.stderr)
            continue
        status = cells[5]
        state = "fixed" if status.startswith("fixed") else "accepted" if status.startswith("accepted") else \
            "closed" if status.startswith("closed") else "open"
        rows.append((cells[0], cells[2], classify(cells[3]), side(cells[3], status), state))
    print("rows: %d" % len(rows))
    for title, key in (("class x side", lambda r: (r[2], r[3])), ("class x severity", lambda r: (r[2], r[1])),
                       ("class x state", lambda r: (r[2], r[4])), ("side", lambda r: (r[3],))):
        print("\n" + title)
        for k, n in sorted(Counter(key(r) for r in rows).items()):
            print("  %-22s %d" % (" / ".join(k), n))
    if "--rows" in sys.argv:
        for r in rows:
            print("\t".join(r))


if __name__ == "__main__":
    main()
