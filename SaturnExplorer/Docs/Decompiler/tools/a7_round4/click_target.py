#!/usr/bin/env python3
"""Area 7 round 4: where a click goes on a line that assigns a frame local or a global a bare value and names a store of it by an empty
token (AddFrameStoreTokens). The click goes to the line's lowest address (firstAddr). Counted by what that instruction is: the named store,
a `mov #imm` (or a branch whose slot is one: A7-10's accepted residue) of the line's number, a call (A7-11's choice), a load, or something
else; the "else" lines are listed: a click that goes neither to the store nor to an instruction that makes the value.

usage: click_target.py <dump.tsv> <hwram image> [--list N]
"""
import os, sys, re, collections
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'a7_round2'))
import token_semantics as ts
from drawn_elsewhere import LINE, klass

NUM = re.compile(r"= (?:\([a-z0-9 ]+\))?(-?(?:0x[0-9a-f]+|\d+));$")

def word(img, a):
    o = (a & 0x07FFFFFF) - 0x06000000
    return img[o] << 8 | img[o + 1] if 0 <= o < len(img) - 1 else None

def delayed(w):
    return (w >> 12) in (0xA, 0xB) or (w >> 8) in (0x8D, 0x8F) or (w & 0xF0FF) in (0x400B, 0x402B, 0x0003, 0x0023) or w in (0x000B, 0x002B)

def main():
    img = open(sys.argv[2], "rb").read()
    limit = int(sys.argv[sys.argv.index("--list") + 1]) if "--list" in sys.argv else 0
    counts = collections.Counter()
    shown = 0
    for fn in ts.load(sys.argv[1]):
        by = collections.defaultdict(list)
        for t in fn["tokens"]: by[t["line"]].append(t)
        for li, toks in by.items():
            first, text = fn["lines"][li]
            if not LINE.match(text): continue
            stores = {t["addr"] for t in toks if t["b"] == t["e"] and klass(t["insn"]) == "store"}
            if not stores: continue
            counts["lines"] += 1
            if first in stores: counts["the store"] += 1; continue
            insn = next((t["insn"] for t in toks if t["addr"] == first), "?")
            k = klass(insn)
            num = NUM.search(text)
            fa = int(first, 16)
            w = word(img, fa)
            if num and k == "mov#": counts["mov #imm"] += 1; continue
            if num and w is not None and delayed(w):
                sw = word(img, fa + 2)
                if sw is not None and sw >> 12 == 0xE: counts["a branch whose slot is a mov #imm (A7-10 residue)"] += 1; continue
            if k == "call": counts["a call (A7-11)"] += 1; continue
            counts["else: " + k] += 1
            if shown < limit:
                shown += 1
                print("%s %s line %d: %s   click -> %s %s; stores %s" % (fn["label"], fn["entry"], li, text.strip(), first, insn, ",".join(sorted(stores))))
    for k, v in sorted(counts.items()): print("%6d  %s" % (v, k))

if __name__ == "__main__":
    sys.exit(main())
