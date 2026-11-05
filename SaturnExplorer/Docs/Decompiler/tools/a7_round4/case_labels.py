#!/usr/bin/env python3
"""Area 7 round 4: where a click on a case label goes. LinesFromMarkup gives a case label the address of the first op of its case
(Ghidra's opref), which is the first instruction of the case that kept an op, not the case's entry: a literal load or a register move
the case begins with is folded, and the label then names an instruction further in.

usage: case_labels.py <dump.tsv> <hwram image> [--list N]

For each switch whose table is the common `mova @(T),r0 ; mov.w @(r0,rN),rN ; add rN,r0 ; jmp @r0` (or `braf`) form, the destinations are
read from the table (16-bit entries from T, as long as they land in the function) and each case label's address is compared with them.
"""
import os, sys, re, collections
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'a7_round2'))
import token_semantics as ts

def word(img, a):
    o = (a & 0x07FFFFFF) - 0x06000000
    return img[o] << 8 | img[o + 1] if 0 <= o < len(img) - 1 else None

CASE = re.compile(r"^\s*case [^:]+:$")

def main():
    img = open(sys.argv[2], "rb").read()
    limit = int(sys.argv[sys.argv.index("--list") + 1]) if "--list" in sys.argv else 0
    counts = collections.Counter()
    shown = 0
    for fn in ts.load(sys.argv[1]):
        addrs = [int(t["addr"], 16) for t in fn["tokens"]]
        if not addrs: continue
        lo, hi = int(fn["entry"], 16), max(addrs) + 0x40
        by = collections.defaultdict(list)
        for t in fn["tokens"]: by[t["line"]].append(t)
        dests = set()
        tables = 0
        for t in fn["tokens"]:
            if t["text"] != "switch": continue
            j = int(t["addr"], 16)
            for k in range(1, 10):
                w = word(img, j - 2 * k)
                if w is not None and w >> 8 == 0xC7:
                    T = ((j - 2 * k) & ~3) + 4 + (w & 0xFF) * 4
                    tables += 1
                    for e in range(64):
                        v = word(img, T + 2 * e)
                        if v is None: break
                        d = T + (v - 0x10000 if v & 0x8000 else v)
                        if not (lo <= d < hi) or d & 1: break
                        dests.add(d)
                    break
        if not dests: continue
        for li, toks in by.items():
            text = fn["lines"][li][1]
            if not CASE.match(text): continue
            a = {int(t["addr"], 16) for t in toks if t["addr"]}
            if not a: continue
            counts["case labels in a decoded table's switch"] += 1
            if a & dests:
                counts["...at a destination of the table"] += 1; continue
            counts["...not at any destination"] += 1
            if shown < limit:
                shown += 1
                x = min(a)
                near = max([d for d in dests if d <= x], default=None)
                print("%s %s line %d: %s  -> %08X %s; the nearest destination before it %s" % (fn["label"], fn["entry"], li, text.strip(), x,
                      next(t["insn"] for t in toks if int(t["addr"], 16) == x), "%08X" % near if near else "none"))
    for k, v in sorted(counts.items()): print("%6d  %s" % (v, k))

if __name__ == "__main__":
    sys.exit(main())
