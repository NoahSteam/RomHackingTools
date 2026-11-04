#!/usr/bin/env python3
"""Area 7 round 3 (A7-21): a line that assigns a frame local or a global a number and names, by an empty token (A7-11's rule: the latest store
before the instruction in the flow's order), a store whose value is another number.

usage: named_store_value.py <dump.tsv> <hwram image> [--list N]

For each line `<local or global> = <number>;` and each empty token at a store (mov.x Rm,@...), the instruction before the store is read: when
it is `mov #imm,Rm` into the store's source register, imm (sign-extended, at the store's width) is the value the store writes. A store of
another value is not the one the line shows. Counts the lines; with --list the first N.
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
    for fn in ts.load(sys.argv[1]):
        by = collections.defaultdict(list)
        for t in fn["tokens"]: by[t["line"]].append(t)
        for li, toks in by.items():
            m = LINE.match(fn["lines"][li][1])
            if not m: continue
            value = int(m.group(2), 0)
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
                    if shown < limit:
                        shown += 1
                        print("%s %s line %d: %s   names the store at %s (%s), which writes %d" % (fn["label"], fn["entry"], li, fn["lines"][li][1].strip(), t["addr"], t["insn"], imm))
    print("lines naming a store of a constant: %d; of another constant than the line shows: %d" % (lines, wrong))

if __name__ == "__main__":
    sys.exit(main())
