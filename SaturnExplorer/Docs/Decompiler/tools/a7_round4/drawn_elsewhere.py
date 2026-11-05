#!/usr/bin/env python3
"""Area 7 round 4: lines that assign a frame local or a global a bare value, name a store of it by an empty token (the area 7 fixer's
AddFrameStoreTokens), and draw their text at another instruction that is not that store: what the hover lists and where the way back
from SH-2 Assembly (InstructionLineIndex::Find, mirrored here) goes for that instruction.

usage: drawn_elsewhere.py <dump.tsv> [--list N] [--kind K]

For each such line L and each instruction D its drawn tokens carry that is no store of L's variable named on L: the mnemonic class of D
(a store of something else, a branch, a call, ...), and whether the way back from D answers L first while D also draws on another line
(the line that is D's own statement): then Show in Pseudocode on D's row selects L. Counts by class; --list prints the first N of class K
(default: every class), with the line the way back should have chosen.
"""
import os, sys, re, collections
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'a7_round2'))
import token_semantics as ts

LINE = re.compile(r"^\s*[a-z]+(?:Stack_[0-9a-f]+(?:\._\d+_\d+_)?|Ram[0-9a-f]{8}) = (?:\([a-z0-9 ]+\))?(?:-?(?:0x[0-9a-f]+|\d+)|[A-Za-z_][A-Za-z_0-9]*);$")

def klass(insn):
    m = insn.split(" ")[0]
    ops = insn[len(m):].strip()
    if m in ("jsr", "bsr", "bsrf"): return "call"
    if m in ("rts", "rte"): return "return"
    if m in ("bra", "braf", "jmp", "bt", "bf", "bt/s", "bf/s"): return "branch"
    if m.startswith("mov") and "," in ops and "@" in ops.split(",")[-1] and "@(0x" not in ops.split(",")[0] or (m.startswith("mov") and ops.rsplit(",", 1)[-1].startswith("@")):
        return "store"
    if m.startswith("mov") and ops.startswith("@"): return "load"
    if m == "mov" and ops.startswith("#"): return "mov#"
    return "other"

def main():
    path = sys.argv[1]
    limit = int(sys.argv[sys.argv.index("--list") + 1]) if "--list" in sys.argv else 0
    want = sys.argv[sys.argv.index("--kind") + 1] if "--kind" in sys.argv else None
    counts = collections.Counter()
    shown = 0
    for fn in ts.load(path):
        by = collections.defaultdict(list)
        for t in fn["tokens"]: by[t["line"]].append(t)
        owners = collections.defaultdict(list)   # addr -> lines in text order
        drawn = collections.defaultdict(set)
        for li in sorted(by):
            for t in by[li]:
                a = t["addr"]
                if not owners[a] or owners[a][-1] != li: owners[a].append(li)
                if t["b"] < t["e"]: drawn[a].add(li)
        first = {li: fn["lines"][li][0] for li in fn["lines"]}
        def find(a):
            o = owners.get(a, [])
            return [li for li in o if first[li] == a] + [li for li in o if first[li] != a and li in drawn[a]] + \
                   [li for li in o if first[li] != a and li not in drawn[a]]
        for li, toks in by.items():
            text = fn["lines"][li][1]
            if not LINE.match(text): continue
            empties = [t for t in toks if t["b"] == t["e"] and klass(t["insn"]) == "store"]
            if not empties: continue
            stores = {t["addr"] for t in empties}
            ds = collections.OrderedDict()
            for t in toks:
                if t["b"] < t["e"] and t["addr"] not in stores: ds.setdefault(t["addr"], t["insn"])
            counts["copy lines naming a store by an empty token"] += 1
            if not ds: continue
            for a, insn in ds.items():
                k = klass(insn)
                counts["drawn at " + k] += 1
                elsewhere = [m for m in drawn[a] if m != li]
                back = find(a)
                wrong = bool(elsewhere) and back and back[0] == li
                if wrong: counts["way back from it answers this line, though it draws on another (" + k + ")"] += 1
                if shown < limit and (want is None or want == k or (want == "wrong" and wrong)):
                    shown += 1
                    print("%s %s line %d: %s\n    drawn at %s %s; stores %s; way back answers line %s%s" % (
                        fn["label"], fn["entry"], li, text.strip(), a, insn, ",".join(sorted(stores)), back[0] if back else None,
                        (" (its own line: %d %s)" % (sorted(elsewhere)[0], fn["lines"][sorted(elsewhere)[0]][1].strip())) if wrong else ""))
    for k, v in sorted(counts.items()): print("%6d  %s" % (v, k))

if __name__ == "__main__":
    sys.exit(main())
