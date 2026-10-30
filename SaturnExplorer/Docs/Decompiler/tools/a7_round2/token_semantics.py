#!/usr/bin/env python3
"""Area 7 round 2: reads the TSV that SE_GAME_TOKEN_DUMP=<prefix> writes (MappingOracle.h DumpMappingTokens) and cross-tabulates
what kind of token names what kind of instruction, to find a token whose address names an instruction that cannot have made it.

usage: token_semantics.py <dump.tsv> [--slots] [--cross] [--suspect]
  --slots    every token at a delay slot's own address (A7-9), with its line and the branch before it
  --cross    token text -> mnemonic counts, for keywords and operators
  --suspect  the pairs a table of what each token can come from rejects (see RULES), with the line
"""
import sys, collections, re

KIND = ["Plain", "Keyword", "Type", "FuncName", "Variable", "Param", "Const", "Comment", "Global", "Label", "Error"]

def load(path):
    funcs = []   # (label, entry, lines[], tokens[])
    cur = None
    with open(path, errors="replace") as f:
        for raw in f:
            raw = raw.rstrip("\n")
            p = raw.split("\t")
            if p[0] == "F":
                cur = {"label": p[1], "entry": p[2], "lines": {}, "tokens": []}
                funcs.append(cur)
            elif p[0] == "L" and cur is not None:
                cur["lines"][int(p[1])] = (p[2], p[3] if len(p) > 3 else "")
            elif p[0] == "T" and cur is not None and len(p) >= 11:
                cur["tokens"].append({"line": int(p[1]), "ti": int(p[2]), "b": int(p[3]), "e": int(p[4]), "kind": KIND[int(p[5])],
                                      "addr": p[6], "inbody": p[7] == "1", "slot": p[8] == "1", "insn": p[9], "text": p[10]})
    return funcs

def mnem(insn):
    return insn.split(" ")[0]

# Token text -> the mnemonics it may come from (regex over the mnemonic). A pair outside the table is listed by --suspect, not failed:
# the table is a hypothesis about the engine (rules rewrite a - b to a + -b, a compare to its negation, ...).
BRANCH = r"b(t|f|ra|sr|raf|srf)?(/s)?|jmp|jsr|rts|rte|trapa"
RULES = {
    "return": r"rts|rte|jmp|bra|braf|b(t|f)(/s)?",
    "goto": BRANCH,
    "if": r"b(t|f)(/s)?|bra|jmp",
    "while": r"b(t|f)(/s)?|bra|jmp",
    "do": r".*",
    "switch": r"jmp|braf",
    "break": BRANCH, "continue": BRANCH,
}

def main():
    path = sys.argv[1]
    funcs = load(path)
    if "--slots" in sys.argv:
        n = 0
        for fn in funcs:
            for t in fn["tokens"]:
                if t["slot"]:
                    n += 1
                    print(f'{fn["label"]} fn {fn["entry"]} line {t["line"]} [{t["text"]!r} {t["kind"]}] addr {t["addr"]} {t["insn"]}  | {fn["lines"][t["line"]][1]}')
        print("slot tokens:", n)
    if "--cross" in sys.argv:
        tab = collections.defaultdict(collections.Counter)
        for fn in funcs:
            for t in fn["tokens"]:
                if t["kind"] in ("Keyword", "Plain") and t["text"].strip() and len(t["text"]) <= 4:
                    tab[t["text"]][mnem(t["insn"])] += 1
        for k in sorted(tab, key=lambda k: -sum(tab[k].values())):
            print(repr(k), sum(tab[k].values()), dict(tab[k].most_common(14)))
    if "--suspect" in sys.argv:
        n = 0
        for fn in funcs:
            for t in fn["tokens"]:
                r = RULES.get(t["text"].strip()) if t["kind"] in ("Keyword",) else None
                if r and not re.fullmatch(r, mnem(t["insn"])):
                    n += 1
                    if n <= 40:
                        print(f'{fn["label"]} fn {fn["entry"]} line {t["line"]} [{t["text"]!r}] addr {t["addr"]} {t["insn"]} | {fn["lines"][t["line"]][1]}')
        print("suspect:", n)

if __name__ == "__main__":
    main()
