#!/usr/bin/env python3
"""Area 7 round 3 (A7-21): a line that assigns a frame local or a global a number and names, by an empty token (A7-11's rule: the latest store
before the instruction in the flow's order), a store whose value is another number.

usage: named_store_value.py <dump.tsv> <hwram image> [--list N]

For each line `<local or global> = <number>;` and each empty token at a store (mov.x Rm,@...), the instruction before the store is read: when
it is `mov #imm,Rm` into the store's source register, imm (sign-extended, at the store's width) is the value the store writes. A store of
another value is not the one the line shows. Counts the lines; with --list the first N.

Area 7 fixer (A7-15, A7-19, A7-20): a line flagged so can still be right, and is then counted apart (the A7-20 signal is the rest):
  composed    the line also names a narrower store (mov.b or mov.w) into the variable: the number is made of both stores' bytes
              (`xStack_10 = 0x10000000;` from `mov.l r2,@r1` of 0 and `mov.b r2,@r1` of 0x10)
  path value  the line names a `mov #imm` of the line's number, or a branch whose delay slot is one: the store is shared by paths that
              each set its register (`bra L ; mov #4,r1` to `L: mov.w r1,@r10`), and the instruction before it in memory is another path's
"""
import os, sys, re, collections
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'a7_round2'))
import token_semantics as ts

def word(img, a):
    o = a - 0x06000000
    return img[o] << 8 | img[o + 1] if 0 <= o < len(img) - 1 else None

def store_source(w):
    """(source register, width in bytes) of a store mov.x Rm,@..., else None"""
    hi, lo = w >> 12, w & 0xF
    if hi == 0x2 and lo in (0, 1, 2, 4, 5, 6): return (w >> 4) & 0xF, (1, 2, 4)[lo % 4]
    if hi == 0x1: return (w >> 4) & 0xF, 4
    if hi == 0x0 and lo in (4, 5, 6): return (w >> 4) & 0xF, (1, 2, 4)[lo - 4]
    if w >> 8 in (0x80, 0xC0): return 0, 1
    if w >> 8 in (0x81, 0xC1): return 0, 2
    if w >> 8 == 0xC2: return 0, 4
    return None

LINE = re.compile(r"^\s*[a-z]+(Stack_[0-9a-f]+|Ram[0-9a-f]{8}) = (-?(?:0x[0-9a-f]+|\d+));$")

def main():
    if len(sys.argv) < 3:
        print(__doc__); return 2
    img = open(sys.argv[2], "rb").read()
    limit = int(sys.argv[sys.argv.index("--list") + 1]) if "--list" in sys.argv else 0
    lines = wrong = shown = 0
    apart = collections.Counter()
    def narrower(a):
        w = word(img, a)
        src = store_source(w) if w is not None else None
        return src is not None and src[1] < 4
    def delayed(a):
        w = word(img, a)
        if w is None: return False
        return (w >> 12) in (0xA, 0xB) or (w >> 8) in (0x8D, 0x8F) or (w & 0xF0FF) in (0x400B, 0x402B, 0x0003, 0x0023) or w in (0x000B, 0x002B)
    def makes(a, value):
        w = word(img, a)
        if w is None or w >> 12 != 0xE: return False
        imm = w & 0xFF
        return ((imm - 0x100 if imm & 0x80 else imm) & 0xFFFFFFFF) == (value & 0xFFFFFFFF)
    for fn in ts.load(sys.argv[1]):
        by = collections.defaultdict(list)
        for t in fn["tokens"]: by[t["line"]].append(t)
        for li, toks in by.items():
            m = LINE.match(fn["lines"][li][1])
            if not m: continue
            value = int(m.group(2), 0)
            named = {int(t["addr"], 16) for t in toks}
            for t in toks:
                if t["b"] != t["e"]: continue
                a = int(t["addr"], 16)
                w = word(img, a)
                src = store_source(w) if w is not None else None
                if not src: continue
                p = word(img, a - 2)
                if p is None or p >> 12 != 0xE or (p >> 8) & 0xF != src[0]: continue
                imm = p & 0xFF
                imm = imm - 0x100 if imm & 0x80 else imm
                mask = (1 << (8 * src[1])) - 1
                lines += 1
                if (imm & mask) != (value & mask):
                    wrong += 1
                    if any(narrower(x) for x in named):
                        apart["composed"] += 1
                        continue
                    if any(makes(x, value) or (delayed(x) and makes(x + 2, value)) for x in named):
                        apart["path value"] += 1
                        continue
                    if shown < limit:
                        shown += 1
                        print("%s %s line %d: %s   names the store at %s (%s), which writes %d" % (fn["label"], fn["entry"], li, fn["lines"][li][1].strip(), t["addr"], t["insn"], imm))
    print("lines naming a store of a constant: %d; of another constant than the line shows: %d (%d composed, %d path value, %d neither)" %
          (lines, wrong, apart["composed"], apart["path value"], wrong - apart["composed"] - apart["path value"]))

if __name__ == "__main__":
    sys.exit(main())
