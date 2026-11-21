#!/usr/bin/env python3
"""Link model step 0: old answers against new, per line and per body instruction, from the token dumps.

    linkdiff.py same OLD.tsv NEW.tsv          the old fields did not change: every F, L, T and O record of OLD is in NEW
    linkdiff.py diff NEW.tsv [--list N]       the shadow model against the tokens, per line and per instruction

The dumps are DumpMappingTokens' (MappingOracle.h): SE_GAME_TOKEN_DUMP=<prefix> of the game corpus run
(tools/a7_round2/corpus.sh makes <prefix>_alone.tsv and _lent.tsv) and SE_GENERATED_TOKEN_DUMP=<file> of the
generator. Records, tab separated: F function, L line (index, firstAddr, text), T token, O today's way back from a
body instruction (LineForInstruction: kind 0 Exact 1 Folded 2 Outside, the instruction it went via, the lines in
the order offered); S a statement (index, kind, first line, lines, span, first link, link count), K a link (statement,
address, role, flags, span), N a line's first statement, C the census counters.

`same` is how a run of the shadow model is shown not to have changed a printed text, a token address, a firstAddr
or an order of the way back: a dump made by the build before the model against one made by the build with it. The
records the older build lacks (S, K, N, C) are ignored; F, L, T and O must be identical, record for record.

`diff` is the comparison step 1 is judged by (the same one tests/LinkCensus.h makes in process, which the census
uses over 20,000 seeds): for each line, the lowest token address (firstAddr, today's click target) against the first
proven Effect of the statements that start on it; the address set of the line's tokens against the addresses of its
links; and for each body instruction the first line today's answer offers against the first the roles give (Effect,
Condition, Value, Placement; text order within a role).
"""
import collections
import sys

ROLE = ["effect", "condition", "value", "address", "placement"]
KIND = ["None", "Store", "Assign", "Branch", "Call", "Return", "Halt", "Label"]
CANDIDATE, BADDATA, PARTIAL, HALT = 1, 2, 4, 8


def functions(path):
    """Yield (header fields, {record type: [fields]}) for each function of a dump."""
    cur = None
    with open(path) as f:
        for raw in f:
            raw = raw.rstrip("\n")
            if not raw:
                continue
            fields = raw.split("\t")
            if fields[0] == "F":
                if cur:
                    yield cur
                cur = (fields, collections.defaultdict(list))
            elif cur:
                cur[1][fields[0]].append(fields)
    if cur:
        yield cur


def same(old, new):
    keys = ("L", "T", "O")
    bad = 0
    n_old = n_new = 0
    it_new = functions(new)
    for head, recs in functions(old):
        n_old += 1
        try:
            head2, recs2 = next(it_new)
        except StopIteration:
            print("NEW has fewer functions than OLD (after %d)" % n_old)
            return 1
        n_new += 1
        if head != head2:
            print("function header differs: %s | %s" % (head, head2))
            bad += 1
            continue
        for k in keys:
            if recs[k] != recs2[k]:
                bad += 1
                a, b = recs[k], recs2[k]
                at = next((i for i in range(min(len(a), len(b))) if a[i] != b[i]), min(len(a), len(b)))
                print("%s records differ in %s %s at #%d: %s | %s" % (k, head[1], head[2], at,
                      a[at] if at < len(a) else None, b[at] if at < len(b) else None))
    extra = sum(1 for _ in it_new)
    print("same: %d functions in OLD, %d in NEW (+%d more in NEW), %d differ" % (n_old, n_new, extra, bad))
    return 1 if bad or extra else 0


def diff(path, limit):
    totals = collections.Counter()
    samples = collections.defaultdict(list)

    def note(cls, text):
        totals[cls] += 1
        if len(samples[cls]) < limit:
            samples[cls].append(text)

    for head, recs in functions(path):
        label = "%s %s" % (head[1], head[2])
        lines = {int(r[1]): (int(r[2], 16), r[3] if len(r) > 3 else "") for r in recs["L"]}
        stmts = {int(r[1]): [int(x) for x in r[2:]] for r in recs["S"]}   # kind firstLine lineCount spanB spanE linkB linkN
        links = collections.defaultdict(list)
        for r in recs["K"]:
            links[int(r[1])].append((int(r[2], 16), int(r[3]), int(r[4]), int(r[5])))
        tokaddrs = collections.defaultdict(set)
        for r in recs["T"]:
            tokaddrs[int(r[1])].add(int(r[6], 16))
        by_line = collections.defaultdict(list)
        for si, s in stmts.items():
            by_line[s[1]].append(si)
        totals["functions"] += 1
        for li, (first, text) in lines.items():
            sis = sorted(by_line.get(li, []), key=lambda i: stmts[i][3])
            if not sis:
                continue
            totals["lines"] += 1
            kind = KIND[stmts[sis[0]][0]]
            new_set = set(a for si in sis for (a, role, fl, sp) in links[si] if not fl & BADDATA)
            old_set = tokaddrs[li]
            if not old_set and not new_set:
                continue
            totals["set." + ("equal" if old_set == new_set else "oldHasMore" if old_set > new_set
                             else "newHasMore" if old_set < new_set else "different")] += 1
            effect = next(((a, KIND[stmts[si][0]]) for si in sis for (a, role, fl, sp) in links[si]
                           if role == 0 and not fl & CANDIDATE), None)
            if effect is None:
                if first:
                    note("line.no-effect/" + kind, "%s line %d firstAddr %08X | %s" % (label, li, first, text))
                continue
            totals["line.effect"] += 1
            if first == effect[0]:
                totals["line.agree"] += 1
                continue
            rel = "old-not-linked"
            for si in sis:
                for (a, role, fl, sp) in links[si]:
                    if a == first:
                        rel = ("old-is-placement" if role == 4 else "old-is-other-effect" if role == 0 else
                               "old-is-candidate-value" if fl & CANDIDATE else "old-is-value")
            totals["line.disagree"] += 1
            note("line.disagree/%s/%s/%s" % (effect[1], rel, "below" if first and first < effect[0] else "none" if not first else "above"),
                 "%s line %d firstAddr %08X new %08X | %s" % (label, li, first, effect[0], text))
        # the way back
        best = collections.defaultdict(dict)
        for si, s in stmts.items():
            for (a, role, fl, sp) in links[si]:
                if fl & (BADDATA | HALT):
                    continue
                rank = 0 if role == 0 else 1 if role == 1 else 3 if role == 4 else 2
                cur = best[a].get(s[1])
                if cur is None or rank < cur:
                    best[a][s[1]] = rank
        for r in recs["O"]:
            a, kind = int(r[1], 16), int(r[2])
            if kind != 0 or len(r) < 5 or not r[4]:
                continue
            old_lines = [int(x) for x in r[4].split(",")]
            totals["insn.exact"] += 1
            if a not in best:
                totals["insn.unlinked"] += 1
                continue
            rank, line = min((rk, ln) for ln, rk in best[a].items())
            if old_lines[0] == line:
                totals["insn.agree"] += 1
                continue
            totals["insn.disagree"] += 1
            note("insn.disagree/new-%s/%s" % (ROLE[rank], "old-first-linked" if old_lines[0] in best[a] else "old-first-unlinked"),
                 "%s insn %08X old line %d new line %d" % (label, a, old_lines[0], line))
    for k in sorted(totals):
        print("%-60s %d" % (k, totals[k]))
    for k in sorted(samples):
        for s in samples[k]:
            print("SAMPLE %s | %s" % (k, s))
    return 0


def main(argv):
    if len(argv) >= 4 and argv[1] == "same":
        return same(argv[2], argv[3])
    if len(argv) >= 3 and argv[1] == "diff":
        limit = int(argv[argv.index("--list") + 1]) if "--list" in argv else 5
        return diff(argv[2], limit)
    print(__doc__)
    return 2


if __name__ == "__main__":
    sys.exit(main(sys.argv))
