#!/usr/bin/env python3
"""Link model step 1: add up the census reports of several runs and judge them against LINK_MODEL.md section 4's thresholds.

    census_merge.py [--title T] [--samples N] REPORT...

A REPORT is what a test binary wrote for SE_LINK_CENSUS=<file> (tests/LinkCensus.h): `CENSUS key value`, `CLASS key count`
and `SAMPLE class | where | text` lines. The chunks of a long run (each a range of generator seeds, SE_GENERATED_FIRST and
SE_GENERATED_SEEDS) are added; the table is printed with each measure's threshold and the go / no-go it gives.
"""
import collections
import sys

OPS = ["STORE", "CALL", "BRANCH", "RETURN", "COPY", "LOAD", "HARDWARE", "OTHER"]


def load(paths):
    census = collections.Counter()
    classes = collections.Counter()
    samples = collections.defaultdict(list)
    for p in paths:
        with open(p) as f:
            for raw in f:
                raw = raw.rstrip("\n")
                if raw.startswith("CENSUS "):
                    _, k, v = raw.split(" ", 2)
                    census[k] += int(v)
                elif raw.startswith("CLASS "):
                    _, k, v = raw.split(" ")
                    classes[k] += int(v)
                elif raw.startswith("SAMPLE "):
                    cls, rest = raw[7:].split(" | ", 1)
                    samples[cls].append(rest)
    return census, classes, samples


def pct(a, b):
    return "%.3f%%" % (100.0 * a / b) if b else "n/a"


def main(argv):
    title, nsamples, paths = "", 5, []
    i = 1
    while i < len(argv):
        if argv[i] == "--title":
            title = argv[i + 1]
            i += 2
        elif argv[i] == "--samples":
            nsamples = int(argv[i + 1])
            i += 2
        else:
            paths.append(argv[i])
            i += 1
    c, classes, samples = load(paths)
    if not c["functions"]:
        print("no functions in", paths)
        return 2
    print("## %s: %d functions, %d lines, %d statements, %d links" % (title or "census", c["functions"], c["lines"], c["statements"], c["links"]))
    print()
    print("Printed ops by origin and class (report only):")
    print("| origin | " + " | ".join(OPS) + " |")
    print("|---|" + "---|" * len(OPS))
    for o in ("flow", "clone", "engine"):
        print("| %s | " % o + " | ".join(str(c["ops.%s.%s" % (o, k)]) for k in OPS) + " |")
    print()
    kinds = ["None", "Store", "Assign", "Branch", "Call", "Return", "Halt", "Label"]
    print("Statements: " + ", ".join("%s %d" % (k, c["statements." + k]) for k in kinds))
    print("Halt statements by type: " + ", ".join("%s %d" % (k, c["halts." + k]) for k in ("baddata", "halt", "unimplemented", "missing")))
    print()
    stores = c["storeFlow"] + c["storeEngine"]
    surv = 100.0 * c["storeFlow"] / stores if stores else 100.0
    lost = c["lostStoreLines"]
    named = c["namedStoreLines"]
    rows = []
    rows.append(("Store statements with a surviving flow op or folded COPY", "%d of %d = %s" % (c["storeFlow"], stores, pct(c["storeFlow"], stores)),
                 "go at 95% or more; 85 to 95% needs a per-class account and step 2b; below 85% rework section 3",
                 "go" if surv >= 95 else "band 85-95" if surv >= 85 else "NO-GO"))
    walk = c["walkCandidate"] + c["walkNone"]
    rows.append(("Engine-made store statements: the bounded walk finds a store / candidates / nothing",
                 "%d / %d / %d of %d" % (c["walkProven"], c["walkCandidate"], c["walkNone"], c["storeEngine"]), "report (feeds the next row)", "-"))
    ratio = 100.0 * lost / named if named else 0.0
    rows.append(("Lines that name a store today left with no proven store (candidates or nothing) / lines that name a store",
                 "%d of %d = %s" % (lost, named, pct(lost, named)), "go at 2% or less; 2 to 10% requires step 2b; above 10% stop",
                 "go" if ratio <= 2 else "needs 2b" if ratio <= 10 else "NO-GO"))
    rows.append(("Flow store, branch, call and return statements with exactly one proven Effect", "%d of %d" % (c["effectGiven"], c["effectDue"]),
                 "every one", "go" if c["effectGiven"] == c["effectDue"] else "NO-GO"))
    rows.append(("Clone-made effect ops", str(c["cloneEffects"]), "non-zero confirms the Clone record", "confirms" if c["cloneEffects"] else "none seen"))
    rows.append(("Lines with more than one statement", str(c["multiStatementLines"]), "report", "-"))
    rows.append(("Lines with an op token and no statement", str(c["noStatementLines"]), "report", "-"))
    rows.append(("Walk bound hits (64 ops, 8 links)", "%d of %d walks = %s" % (c["walkBound"], c["storeEngine"], pct(c["walkBound"], c["storeEngine"])),
                 "under 0.1%", "go" if not c["storeEngine"] or 1000.0 * c["walkBound"] / c["storeEngine"] < 1 else "NO-GO"))
    rows.append(("Functions over the flow record's cap (20,000 ops)", str(c["overCap"]), "report", "-"))
    rows.append(("Assertions: opsEnd past flowOps / inlined clones", "%d / %d" % (c["opsEndDisagree"], c["inlineClones"]), "both zero",
                 "go" if not c["opsEndDisagree"] and not c["inlineClones"] else "NO-GO"))
    rows.append(("Time building the links against the decompilation", "%.1f ms of %.1f ms = %s" % (c["linkMicros"] / 1000.0, c["elapsedMicros"] / 1000.0,
                 pct(c["linkMicros"], c["elapsedMicros"])), "within 5% of today", "go" if c["linkMicros"] <= 0.05 * c["elapsedMicros"] else "check"))
    print("| Measure | Result | Threshold | Verdict |")
    print("|---|---|---|---|")
    for r in rows:
        print("| %s | %s | %s | %s |" % r)
    print()
    print("Lines against today's click target (firstAddr): %d lines have a proven Effect, %d agree, %d disagree (%s)" %
          (c["line.effect"], c["line.agree"], c["line.disagree"], pct(c["line.disagree"], c["line.effect"])))
    print("Address sets (the line's token addresses against its links): equal %d, old has more %d, new has more %d, different %d" %
          (c["set.equal"], c["set.oldHasMore"], c["set.newHasMore"], c["set.different"]))
    print("The way back: %d instructions with an exact answer today; %d unlinked in the model, %d agree on the first line, %d disagree" %
          (c["insn.exact"], c["insn.unlinked"], c["insn.agree"], c["insn.disagree"]))
    print()
    print("Disagreements by class:")
    for k, n in sorted(classes.items(), key=lambda kv: (-kv[1], kv[0])):
        print("- %s: %d" % (k, n))
        for s in samples[k][:nsamples]:
            print("    - %s" % s)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
