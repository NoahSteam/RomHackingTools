#!/usr/bin/env python3
"""Area 7 round 6: a number line of a memory variable (`xRam06057310 = 4;`, `iStack_8 = 0;`) drawn at an instruction that makes a number
(`mov #imm,Rn`, a literal load into Rn) beside the stores it names (empty tokens). The stores store a register; when none of them stores
Rn, the drawn instruction made another register's number (A7-26's shape, at any instruction, not only a join's first: A7-28 at a delay
slot). When one stores Rn but the drawn instruction is not before the store on any path (it follows them: the A7-26 memory rule takes any
`mov #imm` into the stored register), the script says so when the drawn instruction is after every store in the address order and
follows no branch back (a hint, checked by hand).

usage: number_store_reg.py <dump.tsv> [--list N]
"""
import os, sys, re, collections
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'a7_round2'))
import token_semantics as ts

NUM = re.compile(r"^\s*[A-Za-z_][\w.]* = (-?(0x[0-9a-f]+|\d+)|true|false|'.*');$")
MOVI = re.compile(r"^mov #(-?0x[0-9A-Fa-f]+|-?\d+),r(\d+)$")
LIT = re.compile(r"^mov\.[wl] @\(0x[0-9A-Fa-f]+\),r(\d+)$")
STORE = re.compile(r"^mov\.[bwl] r(\d+),@")

def made_reg(insn):
    m = MOVI.match(insn)
    if m: return int(m.group(2))
    m = LIT.match(insn)
    if m: return int(m.group(1))
    return None

def main():
    limit = int(sys.argv[sys.argv.index("--list") + 1]) if "--list" in sys.argv else 0
    counts = collections.Counter()
    shown = 0
    for fn in ts.load(sys.argv[1]):
        by = collections.defaultdict(list)
        for t in fn["tokens"]: by[t["line"]].append(t)
        for li, toks in by.items():
            text = fn["lines"][li][1]
            if not NUM.match(text): continue
            drawn = {t["addr"] for t in toks if t["addr"] != "00000000" and t["b"] < t["e"]}
            if len(drawn) != 1: continue
            d = next(t for t in toks if t["addr"] in drawn and t["b"] < t["e"])
            rn = made_reg(d["insn"])
            if rn is None: continue
            stores = [(t["addr"], int(STORE.match(t["insn"]).group(1))) for t in toks if t["b"] == t["e"] and t["addr"] != "00000000" and STORE.match(t["insn"])]
            if not stores: continue
            counts["number lines drawn at a number instruction beside stores they name"] += 1
            regs = {r for _, r in stores}
            kind = None
            if rn not in regs:
                kind = "...none of the stores stores the drawn instruction's register"
                if d["slot"]: kind += " (drawn at a delay slot)"
            elif int(d["addr"], 16) > max(int(a, 16) for a, _ in stores):
                kind = "...a store stores it, the drawn instruction after every store (by address; check)"
            if not kind: continue
            counts[kind] += 1
            if shown < limit:
                shown += 1
                print("%s %s line %d %r: drawn at %s (%s), stores %s" % (fn["label"], fn["entry"], li, text.strip(), d["addr"], d["insn"],
                      ", ".join("%s r%d" % s for s in stores)))
    for k, v in counts.items(): print("%7d  %s" % (v, k))

if __name__ == "__main__":
    main()
