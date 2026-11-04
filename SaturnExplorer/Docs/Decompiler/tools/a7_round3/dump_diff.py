#!/usr/bin/env python3
"""Area 7 round 3: what a build changed in the mapping, from two token dumps of the same image (SE_GAME_TOKEN_DUMP, before and after).

usage: dump_diff.py <before.tsv> <after.tsv> [--list N]

Per function present in both: the lines whose text is the same but whose instructions differ (a moved token), and the lines whose text is new
(not in the before text), with the instructions each names and the instruction each address decodes to. Counts, and with --list the first N
functions in full.
"""
import os, sys, collections
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'a7_round2'))
import token_semantics as ts

def lines_of(fn):
    by = collections.defaultdict(list)
    for t in fn["tokens"]: by[t["line"]].append(t)
    out = []
    for li in sorted(fn["lines"]):
        toks = by.get(li, [])
        out.append((fn["lines"][li][1].strip(), tuple(sorted({(t["addr"], t["insn"]) for t in toks}))))
    return out

def main():
    if len(sys.argv) < 3:
        print(__doc__); return 2
    limit = int(sys.argv[sys.argv.index("--list") + 1]) if "--list" in sys.argv else 0
    before = {(f["label"], f["entry"]): f for f in ts.load(sys.argv[1])}
    after = {(f["label"], f["entry"]): f for f in ts.load(sys.argv[2])}
    changed = moved = new = shown = 0
    for key, fa in after.items():
        fb = before.get(key)
        if not fb: continue
        lb, la = lines_of(fb), lines_of(fa)
        if lb == la: continue
        changed += 1
        bt = collections.defaultdict(list)
        for text, ins in lb: bt[text].append(ins)
        report = []
        for text, ins in la:
            if not ins: continue
            if text in bt:
                if ins not in bt[text]:
                    moved += 1
                    report.append("  moved  %-60s %s   (was %s)" % (text, " ".join("%s %s" % a for a in ins), " | ".join(" ".join(a for a, _ in b) for b in bt[text])))
            else:
                new += 1
                report.append("  new    %-60s %s" % (text, "; ".join("%s %s" % a for a in ins)))
        if report and shown < limit:
            shown += 1
            print("%s %s" % key)
            print("\n".join(report))
    print("functions whose mapping changed: %d; lines with the same text and other instructions: %d; new lines with an address: %d" % (changed, moved, new))

if __name__ == "__main__":
    sys.exit(main())
