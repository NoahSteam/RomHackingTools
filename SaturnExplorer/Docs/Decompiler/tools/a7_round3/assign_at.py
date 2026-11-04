#!/usr/bin/env python3
"""Area 7 round 3: what instruction a statement that assigns is addressed to.

usage: assign_at.py <dump.tsv> <hwram image> [--list N] [--kind K]

Reads the TSV SE_GAME_TOKEN_DUMP=<prefix> writes (MappingOracle.h DumpMappingTokens; parser token_semantics.py). For each line of the
form `<lhs> = <rhs>;` it takes the instructions the line's tokens name and classes the line:
  slot-store-at-branch  every token is at a delayed branch whose delay slot is a store, and none is a keyword or a call name: the line a
                        slot's store made, still addressed to its branch (A7-10's shape, for an op patch 0075 did not mark)
  frame-no-store        a frame local (`xStack_..`, a piece of one) is assigned and no instruction of the line is a store and none is a call
                        (A7-11's shape outside the oracle's check)
  global-no-store       a global (`xRam...`, `DAT_`...) is assigned and no instruction of the line is a store (A7-15's shape)
Counts per class, and with --list N the first N lines of the class --kind K (default every class).
"""
import os, sys, collections, re
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'a7_round2'))
import token_semantics as ts

DELAYED = {"bra", "bsr", "jmp", "jsr", "rts", "rte", "braf", "bsrf", "bt/s", "bf/s"}

def word(img, a):
    o = a - 0x06000000
    if o < 0 or o + 1 >= len(img): return None
    return img[o] << 8 | img[o + 1]

def is_store(w):
    """SH-2 stores to memory (not push of system registers): mov.x Rm,@Rn / @-Rn / @(R0,Rn), mov.l Rm,@(d,Rn), mov.b/w r0,@(d,Rn),
    mov.x r0,@(d,gbr)."""
    if w is None: return False
    hi, lo = w >> 12, w & 0xF
    if hi == 0x2 and lo in (0, 1, 2, 4, 5, 6): return True
    if hi == 0x0 and lo in (4, 5, 6): return True
    if hi == 0x1: return True
    if w >> 8 in (0x80, 0x81, 0xC0, 0xC1, 0xC2): return True
    return False

ASSIGN = re.compile(r"^\s*([A-Za-z_][A-Za-z0-9_.\[\]\->*()]*)\s*=\s(?!=)")

def main():
    if len(sys.argv) < 3:
        print(__doc__); return 2
    img = open(sys.argv[2], "rb").read()
    limit = int(sys.argv[sys.argv.index("--list") + 1]) if "--list" in sys.argv else 0
    kind = sys.argv[sys.argv.index("--kind") + 1] if "--kind" in sys.argv else None
    counts = collections.Counter()
    shown = collections.Counter()
    for fn in ts.load(sys.argv[1]):
        byline = collections.defaultdict(list)
        for t in fn["tokens"]: byline[t["line"]].append(t)
        for li, toks in byline.items():
            text = fn["lines"][li][1]
            m = ASSIGN.match(text)
            if not m: continue
            lhs = m.group(1)
            addrs = sorted({int(t["addr"], 16) for t in toks})
            ws = {a: word(img, a) for a in addrs}
            mn = {int(t["addr"], 16): ts.mnem(t["insn"]) for t in toks}
            control = any(t["kind"] in ("Keyword", "FuncName") for t in toks) or "(*" in text
            stores = [a for a in addrs if is_store(ws[a])]
            calls = [a for a in addrs if mn[a] in ("jsr", "bsr", "bsrf")]
            cls = None
            if all(mn[a] in DELAYED for a in addrs) and not control and any(is_store(word(img, a + 2)) for a in addrs):
                cls = "slot-store-at-branch"
            elif re.match(r"^[a-z]+Stack_[0-9a-f]+(\._\d+_\d+_)?$", lhs) and not stores and not calls and not control:
                cls = "frame-no-store"
            elif re.match(r"^[a-z]*(Ram|DAT_)[0-9a-f]+$", lhs) and not stores:
                cls = "global-no-store"
            if not cls: continue
            counts[cls] += 1
            if limit and (kind is None or kind == cls) and shown[cls] < limit:
                shown[cls] += 1
                print("%s %s %s line %d: %s   [%s]" % (cls, fn["label"], fn["entry"], li, text.strip(),
                                                    ", ".join("%08X %s" % (a, next(t["insn"] for t in toks if int(t["addr"], 16) == a)) for a in addrs[:5])))
    for k, v in sorted(counts.items()): print("%-22s %d" % (k, v))

if __name__ == "__main__":
    sys.exit(main())
