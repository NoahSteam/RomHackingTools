#!/usr/bin/env python3
"""Area 7 round 4: the empty tokens at a load (A7-3: a read printed inside another op's expression gets an empty token on each line that
prints that expression). A line that carries one should show a read: a dereference, an index, a global, a frame local, a hardware
register's name. Lists the lines whose text shows none of them.

usage: empty_load.py <dump.tsv> [--list N]
"""
import os, sys, re, collections
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'a7_round2'))
import token_semantics as ts

READ = re.compile(r"\*|\[|Ram[0-9a-f]{8}|Stack_|[A-Z][A-Z0-9]+_[A-Z0-9_]+|->|\.")

def main():
    limit = int(sys.argv[sys.argv.index("--list") + 1]) if "--list" in sys.argv else 0
    counts = collections.Counter()
    shown = 0
    for fn in ts.load(sys.argv[1]):
        for t in fn["tokens"]:
            if t["b"] != t["e"]: continue
            ins = t["insn"]
            m = ins.split(" ")[0]
            ops = ins[len(m):].strip()
            if not (m.startswith("mov") and ops.startswith("@")) and not m.startswith("mac"): continue
            text = fn["lines"][t["line"]][1]
            counts["empty load tokens"] += 1
            if READ.search(text): continue
            counts["on a line that shows no read"] += 1
            if shown < limit:
                shown += 1
                print("%s %s line %d: %s   <- %s %s" % (fn["label"], fn["entry"], t["line"], text.strip(), t["addr"], ins))
    for k, v in sorted(counts.items()): print("%6d  %s" % (v, k))

if __name__ == "__main__":
    sys.exit(main())
