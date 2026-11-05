#!/usr/bin/env python3
"""Area 7 round 6: a case label against ITS switch's table. The label's switch is the `switch` line at the label's own indent before it
(the text's nesting, not the order the engine printed switches in, which A7-27's caseDestination uses). For a switch whose table is the
common `mova @(T),r0 ; mov.w/mov.b @(r0,Rm),Rn ; add Rn,r0 ; jmp @r0` form, the table's entries are read from the words; the case values
map to entries with an offset k (a bias `add #-k` before the bound check), taken as the one most labels agree with. A label is wrong when
it names no destination of its own switch's table, or another entry's than its value's.

usage: case_switch.py <dump.tsv> <hwram image> [--list N]
"""
import os, sys, re, collections
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'a7_round2'))
import token_semantics as ts

CASE = re.compile(r"^(\s*)case (-?(?:0x[0-9a-f]+|\d+)):$")
SWITCH = re.compile(r"^(\s*)switch\(")

def word(img, a):
    o = (a & 0x07FFFFFF) - 0x06000000
    return img[o] << 8 | img[o + 1] if 0 <= o < len(img) - 1 else None

def table(img, jmp):
    """(T, entry size) of the common form ending at `jmp @r0`, else None."""
    w = word(img, jmp)
    if w is None or (w & 0xF0FF) != 0x402B or (w >> 8) & 0xF != 0: return None
    size = None
    for k in range(1, 8):
        x = word(img, jmp - 2 * k)
        if x is None: return None
        if x & 0xF00F == 0x000D: size = 2
        if x & 0xF00F == 0x000C: size = 1
        if x >> 8 == 0xC7:
            return (((jmp - 2 * k) & ~3) + 4 + (x & 0xFF) * 4, size) if size else None
    return None

def entry(img, T, size, i):
    if size == 2:
        v = word(img, T + 2 * i)
        if v is None: return None
        return T + (v - 0x10000 if v & 0x8000 else v)
    w = word(img, (T + i) & ~1)
    if w is None: return None
    b = (w >> 8) if (T + i) % 2 == 0 else (w & 0xFF)
    return T + (b - 0x100 if b & 0x80 else b)

def main():
    img = open(sys.argv[2], "rb").read()
    limit = int(sys.argv[sys.argv.index("--list") + 1]) if "--list" in sys.argv else 0
    counts = collections.Counter()
    shown = 0
    for fn in ts.load(sys.argv[1]):
        by = collections.defaultdict(list)
        for t in fn["tokens"]: by[t["line"]].append(t)
        order = sorted(fn["lines"])
        groups = collections.defaultdict(list)   # switch line -> [(label line, value, address)]
        for i, li in enumerate(order):
            m = CASE.match(fn["lines"][li][1])
            if not m: continue
            ind = len(m.group(1))
            sw = None
            for lj in reversed(order[:i]):
                s = SWITCH.match(fn["lines"][lj][1])
                if s and len(s.group(1)) == ind:
                    sw = lj
                    break
            addrs = {int(t["addr"], 16) for t in by.get(li, []) if t["addr"] != "00000000"}
            counts["case labels"] += 1
            if sw is None or len(addrs) != 1:
                counts["...not judged (no switch at its indent, or not one address)"] += 1
                continue
            v = int(m.group(2), 0)
            if v >= 1 << 31: v -= 1 << 32   # (an unsigned label of a negative case)
            groups[sw].append((li, v, addrs.pop()))
        for sw, labels in groups.items():
            jmp = {int(t["addr"], 16) for t in by.get(sw, []) if t["text"] == "switch" and t["addr"] != "00000000"}
            tb = table(img, jmp.pop()) if len(jmp) == 1 else None
            if not tb:
                counts["...in a switch whose table is not the common form"] += len(labels)
                continue
            T, size = tb
            votes = collections.Counter()
            for _, v, a in labels:
                for k in range(-64, 65):
                    if 0 <= v - k < 256 and entry(img, T, size, v - k) == a: votes[k] += 1
            if not votes:
                counts["...in a switch no label of which names its table's entry"] += len(labels)
                k = None
            else:
                k = votes.most_common(1)[0][0]
            n = max(v for _, v, _ in labels) - (k or 0) + 1
            dests = {entry(img, T, size, i) for i in range(max(min(n, 256), 1))}
            for li, v, a in labels:
                counts["...judged"] += 1
                if k is not None and 0 <= v - k < 256 and entry(img, T, size, v - k) == a: continue
                kind = "...naming another entry of its own table" if a in dests else "...naming no destination of its own table"
                counts[kind] += 1
                if shown < limit:
                    shown += 1
                    want = entry(img, T, size, v - k) if k is not None and 0 <= v - k < 256 else None
                    print("%s %s line %d %r: names %08X, its table's entry %s (table %08X, offset %s)" % (fn["label"], fn["entry"], li,
                          fn["lines"][li][1].strip(), a, "%08X" % want if want is not None else "?", T, k))
    for c, v in sorted(counts.items()): print("%7d  %s" % (v, c))

if __name__ == "__main__":
    main()
