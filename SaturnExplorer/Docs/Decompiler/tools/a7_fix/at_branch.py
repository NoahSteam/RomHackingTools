#!/usr/bin/env python3
"""Area 7 fixer: what the tokens at a delayed branch's address are, after patch 0075 (A7-10).

usage: at_branch.py <dump.tsv> [--list N]

Reads the TSV SE_GAME_TOKEN_DUMP=<prefix> writes (MappingOracle.h DumpMappingTokens; the parser is token_semantics.py's). A token at the address of
a delayed branch should be the branch's own (a keyword, the call, a case label of a switch it makes, the `;` that ends the statement) or the
copy a merge puts at the end of a block; a variable, a constant, an assignment or an operator there is what a delay slot's instruction made before the
patch. Prints, per branch mnemonic, the tokens by kind, and (with --list) the lines whose every token is at a delayed branch and that hold no keyword
or call name, which the oracle counts as `slotMadeLines`.
"""
import os, sys, collections
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'a7_round2'))
import token_semantics as ts

DELAYED = {"bra", "bsr", "jmp", "jsr", "rts", "rte", "braf", "bsrf", "bt/s", "bf/s"}

def main():
    if len(sys.argv) < 2:
        print(__doc__)
        return 2
    limit = int(sys.argv[sys.argv.index("--list") + 1]) if "--list" in sys.argv else 0
    funcs = ts.load(sys.argv[1])
    kinds = collections.defaultdict(collections.Counter)
    lines = made = 0
    shown = 0
    for fn in funcs:
        byline = collections.defaultdict(list)
        for t in fn["tokens"]:
            byline[t["line"]].append(t)
            if ts.mnem(t["insn"]) in DELAYED:
                kinds[ts.mnem(t["insn"])][t["kind"]] += 1
        for li, toks in byline.items():
            lines += 1
            if all(ts.mnem(t["insn"]) in DELAYED for t in toks) and not any(t["kind"] in ("Keyword", "FuncName") for t in toks):
                made += 1
                if shown < limit:
                    shown += 1
                    print("%s %s: %s   [%s]" % (fn["label"], fn["entry"], fn["lines"][li][1].strip(),
                                                 ", ".join("%s %s" % (t["addr"], t["insn"]) for t in toks[:3])))
    print("lines with an address: %d; with only a delayed branch's address and no keyword or call name: %d" % (lines, made))
    for m in sorted(kinds):
        print("  %-5s %s" % (m, ", ".join("%s %d" % kv for kv in sorted(kinds[m].items()))))

if __name__ == "__main__":
    sys.exit(main())
