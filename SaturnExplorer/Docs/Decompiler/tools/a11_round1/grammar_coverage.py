#!/usr/bin/env python3
"""Area 11 round 1: does the generator reach every production of its grammars?

usage: grammar_coverage.py <Sh2GeneratedTests.cpp> <log> [<log> ...]

The productions are read from the generator's source: every shape name the grammar's code (struct Gen,
up to `Program Generate(`) assigns to an item, the name tables (`kindName`, `names`), and the keys
CountShapes / CountProgram derive (widths, table guards, function ends, frames ...). The logs are runs
of SaturnExplorerSh2GeneratedTests (SE_GENERATED_GRAMMAR=1 or 2, SE_GENERATED_COVERAGE=1); their
`shapes:` lines are summed. Prints each production with its count, and the ones never reached.
"""
import re
import sys

DERIVED = [
    # CountShapes
    "width.1", "width.2", "width.4", "poll.write_first", "poll.until_set", "call.arg_in_slot", "call.restores", "conv.observe",
    "table.entries", "table.cases", "table.cmp_ge", "table.cmp_gt", "table.cmp_hs", "table.cmp_hi", "table.entry_default",
    "table.entry_shared",
    # CountProgram
    "module.v1", "module.v2", "rts.slot_insn", "fn", "fn.end.rts", "fn.end.tail", "fn.end.fall", "fn.returns", "fn.void",
    "fn.pushed", "fn.pr.stack", "fn.pr.reg", "fn.frame", "fn.fp", "fn.shared_exit", "fn.exit_below", "fn.slot_pop",
    "fn.ret_in_slot", "fn.ret_call", "fn.ret_choice", "fn.arg_copies",
]


def productions(src):
    text = open(src, encoding="utf-8").read()
    a = text.index("struct Gen\n")
    b = text.index("Program Generate(uint64_t seed)")
    body = text[a:b]
    names = set()
    cont = False
    for line in body.splitlines():
        if cont or "shape" in line or "kindName" in line or "names[" in line or line.strip().startswith("{ \""):
            for m in re.finditer(r'"([a-z][a-z0-9_]*(?:\.[a-z0-9_]+)+)"', line):
                names.add(m.group(1))
        cont = ("shape =" in line or cont) and not line.rstrip().endswith(";")
    # the name tables span two lines
    for m in re.finditer(r'(?:kindName|names)\[2\]\[\d\] = \{(.*?)\};', body, re.S):
        names |= set(re.findall(r'"([a-z][a-z0-9_.]+)"', m.group(1)))
    names.add("loop")
    names.add("table")
    names.add("spill")
    return sorted(names | set(DERIVED))


def main(argv):
    prods = productions(argv[1])
    counts = {}
    for log in argv[2:]:
        for line in open(log, errors="replace"):
            if line.startswith("shapes:"):
                for kv in line.split()[1:]:
                    k, _, v = kv.rpartition("=")
                    counts[k] = counts.get(k, 0) + int(v)
    missing = [p for p in prods if counts.get(p, 0) == 0]
    extra = sorted(k for k in counts if k not in prods)
    for p in prods:
        print("%-28s %d" % (p, counts.get(p, 0)))
    print("productions: %d, reached: %d, never reached: %d" % (len(prods), len(prods) - len(missing), len(missing)))
    if missing:
        print("NEVER REACHED: " + " ".join(missing))
    if extra:
        print("counted but not a production read from the grammar (named in code the reader does not parse): " + " ".join(extra))


if __name__ == "__main__":
    main(sys.argv)
