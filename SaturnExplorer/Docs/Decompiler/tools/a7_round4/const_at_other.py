#!/usr/bin/env python3
"""Area 7 round 4: lines that assign a number (`uVar4 = 8;`, any variable) and draw it at a `mov #imm,Rn` of another number, or at a
literal-pool load of another number: the line's instruction makes a different value. A merge the engine collapsed (a MULTIEQUAL of the
same constant on every path into a COPY) keeps the join block's address, the first instruction there, whatever it is.

usage: const_at_other.py <dump.tsv> <hwram image> [--list N] [--join]

Counts the lines, by where the click goes (firstAddr: the instruction itself, or a lower one the line also names); lists the first N.
--join: instead, the lines `V = <number>;` whose one drawn instruction is a branch target (a join's first instruction, the targets of
the function's own bt/bf/bra/bsr) and makes no number: not a `mov #imm`, a literal load, a store, a call or a branch.
"""
import os, sys, re, collections
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'a7_round2'))
import token_semantics as ts

LINE = re.compile(r"^\s*([A-Za-z_][A-Za-z_0-9.]*) = (?:\([a-z0-9 ]+\))?(-?(?:0x[0-9a-f]+|\d+)|'\\x[0-9a-f]{2}'|'.'|true|false);$")

def word(img, a):
    o = (a & 0x07FFFFFF) - 0x06000000
    return img[o] << 8 | img[o + 1] if 0 <= o < len(img) - 1 else None

def literal(img, a, w):
    """value a PC-relative load at a reads, or None"""
    if w >> 12 == 0x9: return word(img, a + 4 + (w & 0xFF) * 2), 2
    if w >> 12 == 0xD:
        p = (a & ~3) + 4 + (w & 0xFF) * 4
        hi, lo = word(img, p), word(img, p + 2)
        return (None if hi is None or lo is None else hi << 16 | lo), 4
    return None, 0

def value(s):
    if s in ("true", "false"): return 1 if s == "true" else 0
    if s.startswith("'"):
        return int(s[3:5], 16) if s.startswith("'\\x") else ord(s[1])
    return int(s, 0)

def targets(img, lo, hi):
    out = set()
    for a in range(lo, hi + 2, 2):
        w = word(img, a)
        if w is None: continue
        if (w >> 8) in (0x89, 0x8B, 0x8D, 0x8F):
            d = w & 0xFF
            out.add(a + 4 + 2 * (d - 0x100 if d & 0x80 else d))
        elif (w >> 12) in (0xA, 0xB):
            d = w & 0xFFF
            out.add(a + 4 + 2 * (d - 0x1000 if d & 0x800 else d))
    return out

def joins(img, path, limit):
    counts = collections.Counter()
    shown = 0
    for fn in ts.load(path):
        addrs = [int(t["addr"], 16) for t in fn["tokens"]]
        if not addrs: continue
        entry = int(fn["entry"], 16)
        tg = targets(img, entry, max(addrs))
        by = collections.defaultdict(list)
        for t in fn["tokens"]: by[t["line"]].append(t)
        for li, toks in by.items():
            first, text = fn["lines"][li]
            m = LINE.match(text)
            if not m: continue
            drawn = {t["addr"]: t["insn"] for t in toks if t["b"] < t["e"]}
            if len(drawn) != 1: continue
            counts["number lines with one drawn instruction"] += 1
            a, insn = next(iter(drawn.items()))
            a = int(a, 16)
            if a not in tg: continue
            w = word(img, a)
            mn = insn.split(" ")[0]
            ops = insn[len(mn):].strip()
            makes = w >> 12 in (0xE, 0x9, 0xD) or (mn.startswith("mov") and ops.rsplit(",", 1)[-1].startswith("@")) or \
                    mn in ("jsr", "bsr", "bsrf", "jmp", "bra", "braf", "bt", "bf", "bt/s", "bf/s", "rts", "rte")
            if makes and not (w >> 12 in (0xE, 0x9, 0xD) and literal_or_imm(img, a, w) not in (None, value(m.group(2)) & 0xFFFFFFFF)):
                continue
            counts["...drawn at a join's first instruction that does not make it"] += 1
            if shown < limit:
                shown += 1
                print("%s %s line %d: %s   drawn at %08X %s; click %s" % (fn["label"], fn["entry"], li, text.strip(), a, insn, first))
    for k, v in sorted(counts.items()): print("%6d  %s" % (v, k))

def literal_or_imm(img, a, w):
    if w >> 12 == 0xE:
        imm = w & 0xFF
        return (imm - 0x100 if imm & 0x80 else imm) & 0xFFFFFFFF
    v, size = literal(img, a, w)
    if v is not None and size == 2 and v & 0x8000: v |= 0xFFFF0000
    return v

def main():
    img = open(sys.argv[2], "rb").read()
    if "--join" in sys.argv:
        return joins(img, sys.argv[1], int(sys.argv[sys.argv.index("--list") + 1]) if "--list" in sys.argv else 0)
    limit = int(sys.argv[sys.argv.index("--list") + 1]) if "--list" in sys.argv else 0
    counts = collections.Counter()
    shown = 0
    for fn in ts.load(sys.argv[1]):
        by = collections.defaultdict(list)
        for t in fn["tokens"]: by[t["line"]].append(t)
        for li, toks in by.items():
            first, text = fn["lines"][li]
            m = LINE.match(text)
            if not m: continue
            want = value(m.group(2)) & 0xFFFFFFFF
            drawn = {t["addr"] for t in toks if t["b"] < t["e"]}
            if len(drawn) != 1: continue
            a = int(next(iter(drawn)), 16)
            w = word(img, a)
            if w is None: continue
            if w >> 12 == 0xE:
                imm = w & 0xFF
                got, size = (imm - 0x100 if imm & 0x80 else imm) & 0xFFFFFFFF, 4
            else:
                got, size = literal(img, a, w)
                if got is None: continue
                if size == 2 and got & 0x8000: got |= 0xFFFF0000
            counts["number lines drawn at a mov #imm or a literal load"] += 1
            mask = 0xFFFFFFFF
            if got == want or (got & 0xFF) == (want & 0xFF) and want < 0x100 or (got & 0xFFFF) == (want & 0xFFFF) and want < 0x10000: continue
            names = {int(t["addr"], 16) for t in toks}
            click = "the instruction" if int(first, 16) == a else "a lower one it names"
            counts["...of another number; click to " + click] += 1
            if shown < limit:
                shown += 1
                insn = next(t["insn"] for t in toks if t["addr"] == next(iter(drawn)))
                print("%s %s line %d: %s   drawn at %08X %s (makes 0x%X); names %s" % (fn["label"], fn["entry"], li, text.strip(), a, insn, got,
                      " ".join("%08X" % x for x in sorted(names))))
    for k, v in sorted(counts.items()): print("%6d  %s" % (v, k))

if __name__ == "__main__":
    sys.exit(main())
