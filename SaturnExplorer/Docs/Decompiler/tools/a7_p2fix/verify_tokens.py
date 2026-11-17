#!/usr/bin/env python3
"""Area 7 P2 fixes: check each token the round's changes moved or marked in a corpus token dump against the instruction it names.
usage: verify_tokens.py <image.bin> <before.tsv> <after.tsv>
For each changed line (token_diff.py's pairing): a token now at another instruction must be at a load whose pointer, found by walking back
through the straight-line code before it (a literal load, `mov #imm`, `add #imm`, `mov Rm,Rn`, and r0 for an indexed load), is the global
the token names (`xRam060572fc`, mirrors folded); a token newly marked materialised must be at a store, and the line a copy of a global.
Prints each check, `ok`, `UNRESOLVED` (the walk could not place the pointer: read by hand) or `WRONG`."""
import re, struct, sys
sys.path.insert(0, __import__('os').path.join(__import__('os').path.dirname(__file__), '..', 'a7_review20'))
from token_diff import load  # noqa: E402

BASE = 0x06000000


def word(img, a):
    o = a - BASE
    return struct.unpack('>H', img[o:o + 2])[0] if 0 <= o < len(img) - 1 else None


def long(img, a):
    o = a - BASE
    return struct.unpack('>I', img[o:o + 4])[0] if 0 <= o < len(img) - 3 else None


def norm(a):
    return a & 0x07FFFFFF if (a >> 29) <= 1 else a


def reg_value(img, at, reg, depth=0):
    """the value of register `reg` before instruction `at`, walking back through straight-line code; None when unknown"""
    if depth > 6:
        return None
    add = 0
    a = at - 2
    for _ in range(96):
        w = word(img, a)
        if w is None:
            return None
        n, m = (w >> 8) & 15, (w >> 4) & 15
        top = w >> 12
        if top == 0xD and n == reg:
            v = long(img, ((a & ~3) + 4 + (w & 0xFF) * 4))
            return None if v is None else (v + add) & 0xFFFFFFFF
        if top == 0x9 and n == reg:
            v = word(img, a + 4 + (w & 0xFF) * 2)
            return None if v is None else ((v - 0x10000 if v & 0x8000 else v) + add) & 0xFFFFFFFF
        if top == 0xE and n == reg:
            i = w & 0xFF
            return ((i - 0x100 if i & 0x80 else i) + add) & 0xFFFFFFFF
        if top == 0x7 and n == reg:
            i = w & 0xFF
            add += i - 0x100 if i & 0x80 else i
        elif top == 0x6 and (w & 15) == 3 and n == reg:
            v = reg_value(img, a, m, depth + 1)
            return None if v is None else (v + add) & 0xFFFFFFFF
        elif writes(w, reg):
            return None
        a -= 2
    return None


def writes(w, reg):
    """conservative: may the instruction write register `reg`? (when unsure, yes: the walk then gives up)"""
    top, n, m, lo = w >> 12, (w >> 8) & 15, (w >> 4) & 15, w & 15
    if top == 0x6 and lo in (4, 5, 6) and m == reg:    # mov.X @Rm+,Rn
        return True
    if reg == 0 and (top == 0x8 and n in (4, 5) or top == 0xC and n in (4, 5, 6, 7, 9, 0xA, 0xB)):
        return True
    if n != reg:
        return False
    if top == 0x2 and lo in (0, 1, 2, 8, 0xC):         # stores, tst, cmp/str: Rn is read only
        return False
    if top == 0x0 and lo in (4, 5, 6):                 # mov.X Rm,@(r0,Rn)
        return False
    if top == 0x1:                                     # mov.l Rm,@(disp,Rn)
        return False
    if top == 0x3 and lo in (0, 2, 3, 6, 7):           # cmp/eq, cmp/hs, cmp/ge, cmp/hi, cmp/gt
        return False
    if top == 0x8 or top == 0xC:                       # n is no register there (r0 forms above)
        return False
    return True


def load_address(img, at):
    w = word(img, at)
    if w is None:
        return None
    top, n, m, lo = w >> 12, (w >> 8) & 15, (w >> 4) & 15, w & 15
    if top == 0x6 and lo in (0, 1, 2, 4, 5, 6):
        return reg_value(img, at, m)
    if top == 0x0 and lo in (0xC, 0xD, 0xE):
        b, x = reg_value(img, at, m), reg_value(img, at, 0)
        return None if b is None or x is None else (b + x) & 0xFFFFFFFF
    if top == 0x5:
        b = reg_value(img, at, m)
        return None if b is None else (b + lo * 4) & 0xFFFFFFFF
    if top == 0x8 and n in (4, 5):
        b = reg_value(img, at, m)
        return None if b is None else (b + lo * (1 if n == 4 else 2)) & 0xFFFFFFFF
    return None


def main():
    img = open(sys.argv[1], 'rb').read()
    before, after = load(sys.argv[2]), load(sys.argv[3])
    counts = {}
    for key, lines in after.items():
        old = before.get(key, {})
        for li, (text, toks) in lines.items():
            if li not in old or old[li][1] == toks:
                continue
            olds = {(t[2]): t for t in old[li][1]}
            for i, (a, mn, t, mat) in enumerate(toks):
                prev = old[li][1][i] if i < len(old[li][1]) else None
                if prev and prev[0] == a and prev[3] == mat:
                    continue
                g = re.search(r'Ram([0-9a-f]{8})', t)
                if prev and prev[0] == a and mat == '1':
                    ok = ' = ' in text and re.search(r'Ram[0-9a-f]{8}', text.split(' = ', 1)[1] or '') is not None
                    w = word(img, int(a, 16))
                    store = mn.split(' ')[0].startswith(('mov', 'sts.l', 'stc.l')) and ',@' in mn
                    verdict = 'ok' if ok and store else 'WRONG'
                    print('%s %s %s line %d mark   [%s %s] %r: %s' % (key[0], key[1], verdict, li, a, mn, t, text.strip()))
                elif a and (not prev or prev[0] != a):
                    if not t:
                        verdict = 'ok (empty token at a maker)'
                    else:
                        want = None
                        m = re.search(r'Ram([0-9a-f]{8})', text.split(' = ', 1)[1] if ' = ' in text else text)
                        if m:
                            want = int(m.group(1), 16)
                        got = load_address(img, int(a, 16))
                        if got is None or want is None:
                            verdict = 'UNRESOLVED'
                        else:
                            width = 4 if '.l' in mn else 2 if '.w' in mn else 1
                            verdict = 'ok' if norm(want) <= norm(got) < norm(want) + 4 or norm(got) <= norm(want) < norm(got) + width else 'WRONG (reads %08X)' % got
                    print('%s %s %s line %d moved  [%s %s] %r (was %s): %s' % (key[0], key[1], verdict, li, a, mn, t, prev[0] if prev else '-', text.strip()))
                else:
                    verdict = 'other'
                    print('%s %s other line %d [%s %s %s] %r: %s' % (key[0], key[1], li, a, mn, mat, t, text.strip()))
                counts[verdict.split(' ')[0]] = counts.get(verdict.split(' ')[0], 0) + 1
    print('verdicts:', counts)


main()
