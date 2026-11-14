#!/usr/bin/env python3
"""A11-21: list the oracle counts that no pin and no seed of a generator run reached.

usage: deadcounts.py <Sh2GeneratedTests.cpp> <output of one SaturnExplorerSh2GeneratedTests run> [more outputs]

The run prints a line "reached (every evaluation of this run, pins included): name=n ..."; a count whose name is a
string literal in a count("...") call of the source and is missing from it (or 0) is a rule nothing exercises: a pin
that required it lost the requirement (A5-130's 6605001), or a product change moved every seed off it. Counts built
at run time (a name with a computed part) are not in the literal list. Exit 1 when any is listed.
"""
import re
import sys


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    source = open(argv[1], encoding="utf-8", errors="replace").read()
    names = set(re.findall(r'\bcount\(\s*"([A-Za-z0-9_.\-]+)"', source))
    names |= set(re.findall(r'\bcounts\[\s*"([A-Za-z0-9_.\-]+)"\s*\]', source))
    reached = {}
    for path in argv[2:]:
        for line in open(path, encoding="utf-8", errors="replace"):
            if line.startswith("reached (every evaluation"):
                for tok in line.split(":", 1)[1].split():
                    k, _, v = tok.rpartition("=")
                    if v.lstrip("-").isdigit():
                        reached[k] = reached.get(k, 0) + int(v)
    if not reached:
        print("no 'reached' line in the outputs: run a binary built with A11-21")
        return 2
    dead = sorted(n for n in names if reached.get(n, 0) <= 0)
    print("%d literal counts in the source, %d reached, %d not reached by any pin or seed" % (len(names), len(names) - len(dead), len(dead)))
    for n in dead:
        print("  " + n)
    return 1 if dead else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
