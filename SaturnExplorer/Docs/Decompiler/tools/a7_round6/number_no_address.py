#!/usr/bin/env python3
"""Area 7 round 6: number lines (`iVar1 = 0x1234;`) with no address at all, the line of a merged number whose paths' writers
AddJoinConstantTokens could not name (a load, a call, a computation, an input; and, the fixer's note, a `mov.w` literal, which the flow
reads into a temporary and extends into the register, so FlowNumber sees no number). For each, the instructions of the function that
make the number into some register (`mov #imm` or a literal of that value) are listed, so a `mov.w` literal's case can be told apart.

usage: number_no_address.py <dump.tsv> <hwram image> [--list N]
"""
import os, sys, re, collections
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'a7_round2'))
import token_semantics as ts

NUM = re.compile(r"^\s*([A-Za-z_][\w.]*) = (-?(?:0x[0-9a-f]+|\d+));$")

def word(img, a):
    o = (a & 0x07FFFFFF) - 0x06000000
    return img[o] << 8 | img[o + 1] if 0 <= o < len(img) - 1 else None

def makers(img, lo, hi, value):
    out = []
    for p in range(lo, hi, 2):
        w = word(img, p)
        if w is None: continue
        if w >> 12 == 0xE:
            v = w & 0xFF
            v = v - 0x100 if v & 0x80 else v
            if (v & 0xFFFFFFFF) == (value & 0xFFFFFFFF): out.append((p, "mov #imm"))
        elif w >> 12 == 0x9:
            q = p + 4 + (w & 0xFF) * 2
            lw = word(img, q)
            if lw is None: continue
            v = lw - 0x10000 if lw & 0x8000 else lw
            if (v & 0xFFFFFFFF) == (value & 0xFFFFFFFF): out.append((p, "mov.w literal"))
        elif w >> 12 == 0xD:
            q = (p & ~3) + 4 + (w & 0xFF) * 4
            hw, lw = word(img, q), word(img, q + 2)
            if hw is None or lw is None: continue
            if (hw << 16 | lw) == (value & 0xFFFFFFFF): out.append((p, "mov.l literal"))
    return out

def main():
    img = open(sys.argv[2], "rb").read()
    limit = int(sys.argv[sys.argv.index("--list") + 1]) if "--list" in sys.argv else 0
    counts = collections.Counter()
    shown = 0
    for fn in ts.load(sys.argv[1]):
        by = collections.defaultdict(list)
        for t in fn["tokens"]: by[t["line"]].append(t)
        addrs = [int(t["addr"], 16) for t in fn["tokens"] if t["addr"] != "00000000"]
        if not addrs: continue
        lo, hi = int(fn["entry"], 16), max(addrs) + 2
        for li, (first, text) in fn["lines"].items():
            m = NUM.match(text)
            if not m: continue
            counts["number lines"] += 1
            if any(t["addr"] != "00000000" for t in by.get(li, [])): continue
            counts["...with no address"] += 1
            v = int(m.group(2), 0) & 0xFFFFFFFF
            mk = makers(img, lo, hi, v)
            kinds = sorted({k for _, k in mk})
            counts["...of which the function makes the number only by: " + (", ".join(kinds) if kinds else "none")] += 1
            if shown < limit:
                shown += 1
                print("%s %s line %d %r: makers %s" % (fn["label"], fn["entry"], li, text.strip(), " ".join("%08X(%s)" % x for x in mk[:6])))
    for k, v in sorted(counts.items()): print("%7d  %s" % (v, k))

if __name__ == "__main__":
    main()
