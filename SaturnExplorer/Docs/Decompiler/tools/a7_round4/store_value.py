#!/usr/bin/env python3
"""Area 7 round 4: a line that assigns a frame local or a global a bare value (`V = param_2;`, `V = 5;`, `V = uVar3;`) and names stores of
V whose value is, as far as the instructions before the store say, not that value.

usage: store_value.py <dump.tsv> <hwram image> [--list N] [--all]

For each store the line names (a token at mov.x Rm,@...), Rm's value is followed back from the store through `mov Rk,Rm` and `mov #imm,Rm`
in the instructions before it, up to a branch or a delay slot (straight-line code only; a label in between is not seen, so a hit is a
candidate to read, not a verdict). It ends at a number (`mov #imm`), at a parameter register r4..r7 that nothing in the window wrote
(only when the window reaches the function's entry), or unknown. A line is listed when every store it names that resolves resolves to
another value than the line shows, and at least one does: `V = 5;` naming a store of `mov #3`, `V = param_2;` naming a store of r6 from
the entry. --all also lists the lines whose stores do not resolve.
"""
import os, sys, re, collections
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'a7_round2'))
import token_semantics as ts

def word(img, a):
    o = (a & 0x07FFFFFF) - 0x06000000
    return img[o] << 8 | img[o + 1] if 0 <= o < len(img) - 1 else None

def store_source(w):
    hi, lo = w >> 12, w & 0xF
    if hi == 0x2 and lo in (0, 1, 2): return (w >> 4) & 0xF, (1, 2, 4)[lo]
    if hi == 0x2 and lo in (4, 5, 6): return (w >> 4) & 0xF, (1, 2, 4)[lo - 4]
    if hi == 0x1: return (w >> 4) & 0xF, 4
    if hi == 0x0 and lo in (4, 5, 6): return (w >> 4) & 0xF, (1, 2, 4)[lo - 4]
    if w >> 8 in (0x80, 0xC0): return 0, 1
    if w >> 8 in (0x81, 0xC1): return 0, 2
    if w >> 8 == 0xC2: return 0, 4
    return None

def delayed(w):
    return (w >> 12) in (0xA, 0xB) or (w >> 8) in (0x8D, 0x8F) or (w & 0xF0FF) in (0x400B, 0x402B, 0x0003, 0x0023) or w in (0x000B, 0x002B)

def branch(w):
    return delayed(w) or (w >> 8) in (0x89, 0x8B)

def writes_reg(w, r):
    """does w write register r (approximately: the destination field of the common forms)"""
    n = (w >> 8) & 0xF
    hi = w >> 12
    if hi in (0x3, 0x6, 0x7, 0x9, 0xD, 0xE) and n == r: return True
    if hi == 0x5 and n == r: return True
    if hi == 0x0 and (w & 0xF) in (0xA, 0xC, 0xD, 0xE, 0x9, 0x2) and n == r: return True
    if hi == 0x4 and n == r and (w & 0xFF) not in (0x0B, 0x2B, 0x15, 0x11, 0x10 + 0x00) : return True
    if hi == 0x2 and (w & 0xF) in (0x4, 0x5, 0x6) and n == r: return True   # pre-decrement stores change Rn
    if hi == 0xC and r == 0 and (w >> 8) in (0xC4, 0xC5, 0xC6, 0xC7, 0xC9, 0xCA, 0xCB, 0xCC): return True
    if hi == 0x8 and r == 0 and (w >> 8) in (0x84, 0x85): return True
    if hi == 0x6 and (w & 0xF) in (0x4, 0x5, 0x6) and ((w >> 4) & 0xF) == r: return True   # post-increment loads change Rm
    return False

def resolve(img, store, src, entry):
    """the value src holds at the store: ('num', v) / ('param', n) / None"""
    r = src
    a = store - 2
    for _ in range(40):
        if a < entry: break
        w = word(img, a)
        if w is None: return None
        pw = word(img, a - 2)
        if branch(w) or (pw is not None and delayed(pw) and a - 2 >= entry): return None
        if w >> 12 == 0xE and (w >> 8) & 0xF == r:
            imm = w & 0xFF
            return ('num', imm - 0x100 if imm & 0x80 else imm)
        if w >> 12 == 0x6 and (w & 0xF) == 3 and (w >> 8) & 0xF == r:
            r = (w >> 4) & 0xF
            a -= 2
            continue
        if writes_reg(w, r): return None
        a -= 2
    if a < entry and 4 <= r <= 7: return ('param', r - 3)
    return None

LINE = re.compile(r"^\s*[a-z]+(Stack_[0-9a-f]+(?:\._\d+_\d+_)?|Ram[0-9a-f]{8}) = (?:\([a-z0-9 ]+\))?(-?(?:0x[0-9a-f]+|\d+)|param_\d+);$")

def main():
    if len(sys.argv) < 3:
        print(__doc__); return 2
    img = open(sys.argv[2], "rb").read()
    limit = int(sys.argv[sys.argv.index("--list") + 1]) if "--list" in sys.argv else 0
    showall = "--all" in sys.argv
    counts = collections.Counter()
    shown = 0
    for fn in ts.load(sys.argv[1]):
        entry = int(fn["entry"], 16)
        by = collections.defaultdict(list)
        for t in fn["tokens"]: by[t["line"]].append(t)
        for li, toks in by.items():
            m = LINE.match(fn["lines"][li][1])
            if not m: continue
            counts["lines"] += 1
            rhs = m.group(2)
            want = ('param', int(rhs[6:])) if rhs.startswith("param_") else ('num', int(rhs, 0))
            verdicts = []
            named = {int(t["addr"], 16) for t in toks}
            # (a number made of a word and a narrower store, or a store shared by paths whose register a named `mov #imm` or a named branch's
            # slot sets: right, as named_store_value.py counts them apart)
            def narrow(a):
                w = word(img, a); src = store_source(w) if w is not None else None
                return src is not None and src[1] < 4
            def makes(a):
                w = word(img, a)
                if w is None or w >> 12 != 0xE or want[0] != 'num': return False
                imm = w & 0xFF
                return ((imm - 0x100 if imm & 0x80 else imm) & 0xFFFFFFFF) == (want[1] & 0xFFFFFFFF)
            if any(narrow(a) for a in named) and len(named) > 1: counts["composed or narrow"] += 1; continue
            if any(makes(a) or (word(img, a) is not None and delayed(word(img, a)) and makes(a + 2)) for a in named):
                counts["path value"] += 1; continue
            for a in sorted({int(t["addr"], 16) for t in toks if t["b"] == t["e"]}):
                w = word(img, a)
                src = store_source(w) if w is not None else None
                if not src: continue
                v = resolve(img, a, src[0], entry)
                if v is None:
                    verdicts.append((a, None)); continue
                if v[0] == 'num' and want[0] == 'num':
                    mask = (1 << (8 * src[1])) - 1
                    verdicts.append((a, (v[1] & mask) == (want[1] & mask)))
                else:
                    verdicts.append((a, v == want))
            if not verdicts: continue
            counts["with stores"] += 1
            known = [ok for _, ok in verdicts if ok is not None]
            if any(known): counts["a store agrees"] += 1; continue
            if not known:
                counts["unresolved"] += 1
                if not showall: continue
            else:
                counts["every resolved store disagrees"] += 1
            if shown < limit:
                shown += 1
                print("%s %s line %d: %s   stores: %s" % (fn["label"], fn["entry"], li, fn["lines"][li][1].strip(),
                      ", ".join("%08X%s" % (a, "" if ok is None else (" ok" if ok else " OTHER")) for a, ok in verdicts)))
    print(" ".join("%s=%d" % kv for kv in counts.items()))

if __name__ == "__main__":
    sys.exit(main())
