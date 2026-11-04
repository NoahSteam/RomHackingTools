#!/usr/bin/env python3
"""Area 11 round 1: the mutation score per oracle, from mutate.py's results.tsv.

usage: score.py <results.tsv>

For each oracle: how many mutants it was run on, how many it killed (exit non-zero or a timeout; for
SE_GENERATED_MUTATE, the damage reported), and its score. Then each mutant with the oracles that killed
it, and the survivors (no oracle run on it killed it). Stillborn mutants (did not build) and mutants
that could not be applied are listed apart and not scored.
"""
import collections
import sys


def main(path):
    rows = [l.rstrip("\n").split("\t") for l in open(path) if l.strip()]
    per = collections.OrderedDict()
    killers = collections.OrderedDict()
    other = {}
    for r in rows:
        mid, oracle, verdict = r[0], r[1], r[2]
        killers.setdefault(mid, [])
        if verdict in ("stillborn", "not-applied", "error"):
            other[mid] = verdict + (": " + r[4] if len(r) > 4 else "")
            continue
        if oracle == "*":
            continue
        k = per.setdefault(oracle, [0, 0])
        k[0] += 1
        if verdict.startswith("killed"):
            k[1] += 1
            killers[mid].append(oracle)
    print("| oracle | run on | killed | score |")
    print("|---|---|---|---|")
    for o, (n, k) in per.items():
        print("| %s | %d | %d | %.0f%% |" % (o, n, k, 100.0 * k / n if n else 0))
    scored = [m for m in killers if m not in other]
    dead = [m for m in scored if killers[m]]
    print("\nmutants scored: %d, killed: %d, survived: %d (%.0f%%); not scored: %d" %
          (len(scored), len(dead), len(scored) - len(dead), 100.0 * len(dead) / max(1, len(scored)), len(other)))
    print("\nsurvivors:", " ".join(m for m in scored if not killers[m]))
    for m, why in other.items():
        print("not scored: %s (%s)" % (m, why))
    print("\nkilled by:")
    for m in dead:
        print("  %s: %s" % (m, ", ".join(killers[m])))


if __name__ == "__main__":
    main(sys.argv[1])
