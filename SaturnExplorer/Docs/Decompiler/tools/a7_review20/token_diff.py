#!/usr/bin/env python3
"""Area 7 review round 20: the lines whose tokens differ between two token dumps (SE_GAME_TOKEN_DUMP, see a7_round2/corpus.sh).
usage: token_diff.py <before.tsv> <after.tsv>   -- prints, per function, each changed line: its text and, before and after, each token's
instruction (address and mnemonic), its text and an 'm' when it is a copy materialised there; then a tally by the kind of change."""
import sys
from collections import Counter, OrderedDict


def load(path):
    funcs = OrderedDict()
    cur = None
    with open(path, encoding='utf-8', errors='replace') as f:
        for raw in f:
            p = raw.rstrip('\n').split('\t')
            if p[0] == 'F':
                cur = (p[1], p[2])
                funcs[cur] = {}
            elif p[0] == 'L' and cur:
                funcs[cur][int(p[1])] = [p[3] if len(p) > 3 else '', []]
            elif p[0] == 'T' and cur:
                li = int(p[1])
                if li in funcs[cur]:
                    funcs[cur][li][1].append((p[6], p[9], p[10], p[-1]))
    return funcs


def fmt(tokens):
    return ' '.join('[%s %s%s %r]' % (a, m, 'm' if mat == '1' else '', t) for a, m, t, mat in tokens)


def main():
    before, after = load(sys.argv[1]), load(sys.argv[2])
    kinds = Counter()
    for key, lines in after.items():
        old = before.get(key)
        if old is None:
            continue
        for li, (text, toks) in sorted(lines.items()):
            o = old.get(li)
            if o is None or o[1] == toks:
                continue
            ob = set((a, mat) for a, m, t, mat in o[1])
            nb = set((a, mat) for a, m, t, mat in toks)
            oa, na = set(a for a, _ in ob), set(a for a, _ in nb)
            kind = 'materialised mark' if oa == na else 'addresses'
            kinds[kind] += 1
            print('%s %s line %d (%s): %s' % (key[0], key[1], li, kind, text.strip()))
            print('    before:', fmt(o[1]))
            print('    after: ', fmt(toks))
    print('changed lines by kind:', dict(kinds))


if __name__ == '__main__':
    main()
