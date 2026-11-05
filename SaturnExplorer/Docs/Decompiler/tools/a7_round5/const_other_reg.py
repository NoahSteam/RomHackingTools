#!/usr/bin/env python3
"""Area 7 round 5 (A7-26): a number line `V = N;` drawn at a join's first instruction that makes the number N into ANOTHER register
than the paths into the join set (`mov #0,r12` at the join, `mov #0,r8` in the slots of the branches into it). The A7-22 rule (and the
oracle's constAtJoin) asks whether the instruction makes the number, not into which register.

usage: const_other_reg.py <dump.tsv> <hwram image> [--list N]

Counts the number lines with one drawn instruction that is a join's first instruction (a branch target) and makes the line's number
(mov #imm,Rm or a literal load into Rm); of those, the lines where some path into the join (a branch's delay slot, or the instruction
before it on the fall-through) sets the same number into another register Rx and none sets it into Rm: the candidates (the drawn
instruction is a register's own maker only when a path does not set it). Lists the first N.
"""
import os, sys, re, collections
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'a7_round2'))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'a7_round4'))
import token_semantics as ts
import const_at_other as co

def imm_of(img, a):
    """(register, value) a mov #imm,Rn or a literal load at a sets, else None"""
    w = co.word(img, a)
    if w is None: return None
    if w >> 12 == 0xE:
        i = w & 0xFF
        return (w >> 8) & 0xF, (i - 0x100 if i & 0x80 else i) & 0xFFFFFFFF
    if w >> 12 in (0x9, 0xD):
        v, size = co.literal(img, a, w)
        if v is None: return None
        if size == 2 and v & 0x8000: v |= 0xFFFF0000
        return (w >> 8) & 0xF, v
    return None

def preds(img, entry, hi):
    """target -> list of instruction addresses whose effect reaches it last on a path: a branch's slot, or the fall-through instruction"""
    out = collections.defaultdict(list)
    for a in range(entry, hi + 2, 2):
        w = co.word(img, a)
        if w is None: continue
        t = None; delayed = False
        if (w >> 8) in (0x89, 0x8B): d = w & 0xFF; t = a + 4 + 2 * (d - 0x100 if d & 0x80 else d)
        elif (w >> 8) in (0x8D, 0x8F): d = w & 0xFF; t = a + 4 + 2 * (d - 0x100 if d & 0x80 else d); delayed = True
        elif (w >> 12) == 0xA: d = w & 0xFFF; t = a + 4 + 2 * (d - 0x1000 if d & 0x800 else d); delayed = True
        if t is None: continue
        out[t].append(a + 2 if delayed else a - 2)
        if (w >> 8) in (0x89, 0x8B, 0x8D, 0x8F):   # a conditional branch: the fall-through reaches the next instruction (after the slot)
            nxt = a + 4 if delayed else a + 2
            out[nxt].append(nxt - 2)
    return out

def main():
    img = open(sys.argv[2], "rb").read()
    limit = int(sys.argv[sys.argv.index("--list") + 1]) if "--list" in sys.argv else 0
    counts = collections.Counter()
    shown = 0
    for fn in ts.load(sys.argv[1]):
        addrs = [int(t["addr"], 16) for t in fn["tokens"]]
        if not addrs: continue
        entry = int(fn["entry"], 16)
        pr = preds(img, entry, max(addrs))
        by = collections.defaultdict(list)
        for t in fn["tokens"]: by[t["line"]].append(t)
        for li, toks in by.items():
            first, text = fn["lines"][li]
            m = co.LINE.match(text)
            if not m: continue
            drawn = {t["addr"]: t["insn"] for t in toks if t["b"] < t["e"]}
            if len(drawn) != 1: continue
            a = int(next(iter(drawn)), 16)
            if a not in pr: continue
            mk = imm_of(img, a)
            want = co.value(m.group(2)) & 0xFFFFFFFF
            if mk is None or mk[1] != want: continue
            counts["number lines drawn at a join's first instruction that makes the number"] += 1
            regs = [imm_of(img, p) for p in pr[a]]
            same = [r for r in regs if r and r[1] == want]
            if not same: continue
            if any(r[0] == mk[0] for r in same): continue
            counts["...where the paths set the number in another register, not r%d (candidates)" % mk[0]] += 1
            if shown < limit:
                shown += 1
                print("%s %s line %d: %s   drawn at %08X %s; paths set r%s" % (fn["label"], fn["entry"], li, text.strip(), a, next(iter(drawn.values())),
                      ",r".join(str(r[0]) for r in same)))
    for k, v in sorted(counts.items()): print("%6d  %s" % (v, k))

if __name__ == "__main__":
    sys.exit(main())
