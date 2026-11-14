#!/usr/bin/env python3
"""Area 3, A3-203 and A3-204: checks each function text that differs between two token dumps (a7_round2/corpus.sh's <tag>_alone.tsv or
_lent.tsv) against the hwram image. For each function: whether any line but a comment changed (it must not); each note (A3-108's frame
notes, A3-197's cautions) by its text and the call printed after it (the line's address), added, dropped, or with "likewise at the calls
below" gained or lost. A frame note said at a call of the new text that the old one left to "likewise" (A3-204) is checked on a CFG read from the
image (branches with their delay slots; calls fall through; rts ends; `jmp`, `braf` and the like end the walk and are reported): for each
earlier call saying the same note, a path from the entry to the call that does not pass it must exist (no one of them is on every path to it,
so the text says it); for a
stack note (said again where one more word holds a frame address), one that passes not the call the old text left it to.
usage: checknotes.py <hwram image> <baseline.tsv> <new.tsv> [function ...]"""
import re, sys
img = open(sys.argv[1], 'rb').read()
def word(a):
    o = a - 0x06000000
    return img[o] << 8 | img[o + 1] if 0 <= o < len(img) - 1 else None

def texts(p):
    """function -> its lines: (address: the call instruction of the line when a token of it names one, else the line's address, text)"""
    out, cur, calls = {}, None, {}
    def close():
        if cur: out[cur] = [(calls.get(n, a), s) for n, (a, s) in enumerate(out[cur])]
    for l in open(p, encoding='utf-8', errors='replace'):
        t = l.rstrip('\n').split('\t')
        if t[0] == 'F': close(); cur = t[2]; out[cur] = []; calls = {}
        elif t[0] == 'L' and cur: out[cur].append((int(t[2], 16), t[3] if len(t) > 3 else ''))
        elif t[0] == 'T' and cur and len(t) > 9 and t[9].split(' ')[0] in ('jsr', 'bsr', 'bsrf') and int(t[1]) not in calls:
            calls[int(t[1])] = int(t[6], 16)
    close()
    return out

def split(lines):
    """the code lines (text only) and the notes: (kind, text without likewise, likewise, call address)"""
    code, notes, i = [], [], 0
    while i < len(lines):
        a, s = lines[i]
        if '/*' in s:
            c = s
            while '*/' not in c and i + 1 < len(lines):
                i += 1; c += ' ' + lines[i][1].strip()
            c = re.sub(r'\s+', ' ', c[c.index('/*') + 2:c.index('*/')]).strip()
            j = i + 1
            while j < len(lines) and (lines[j][0] == 0 or '/*' in lines[j][1]): j += 1
            call = lines[j][0] if j < len(lines) else 0
            lw = c.endswith('; likewise at the calls below')
            base = c[:-len('; likewise at the calls below')] if lw else c
            kind = 'caution' if base.startswith('this call may change') else 'frame' if base.startswith(('a frame address', 'a local at or above', "a saved register's")) else 'other'
            notes.append((kind, base, lw, call))
        else:
            code.append(s)
        i += 1
    return code, notes

def succ(a):
    """successors of the instruction at a, and whether the walk ends there unresolved"""
    i = word(a)
    if i is None: return [], 'not captured'
    t = i >> 12
    if i in (0x000B, 0x002B): return [], None   # rts, rte (their slot changes no path)
    if t == 0x8 and (i >> 8) & 0xF in (0x9, 0xB, 0xD, 0xF):
        d = i & 0xFF; d = d - 256 if d & 0x80 else d
        tgt = a + 4 + 2 * d
        if (i >> 8) & 0xF in (0xD, 0xF): return [a + 2], ('slot', tgt, a + 4)   # bt/s, bf/s: the slot, then either
        return [tgt, a + 2], None
    if t == 0xA:
        d = i & 0xFFF; d = d - 4096 if d & 0x800 else d
        return [a + 2], ('slot', a + 4 + 2 * d)
    if i & 0xF0FF == 0x402B:
        tbl = table(a)
        if tbl: return [a + 2], ('slot',) + tuple(tbl)
        return [], 'jmp @r%d' % ((i >> 8) & 15)
    if t == 0x0 and i & 0xFF == 0x23: return [], 'braf r%d' % ((i >> 8) & 15)
    return [a + 2], None   # calls (bsr, jsr, bsrf) return to the instruction after their slot

def table(j):
    """the destinations of the compiled switch `mova @(d,pc),r0 ; mov.w @(r0,rI),rI (or mov.b) ; add rI,r0 ; jmp @r0`, with its bound from `mov #k,rK ;
    cmp/hi rK,rX` before it (k + 1 entries); None when the shape is not there"""
    if word(j) != 0x402B: return None
    base = width = bound = None
    for b in range(j - 2, j - 40, -2):
        i = word(b)
        if i is None: return None
        if i >> 8 == 0xC7 and base is None: base = ((b & ~3) + 4 + 4 * (i & 0xFF))
        if i & 0xF00F in (0x000D, 0x000C) and width is None: width = 2 if i & 0xF == 0xD else 1
        if i & 0xF00F == 0x3006:   # cmp/hi rK,rX
            k = (i >> 4) & 15
            for c in range(b - 2, b - 16, -2):
                w2 = word(c)
                if w2 is not None and w2 >> 12 == 0xE and (w2 >> 8) & 15 == k: bound = w2 & 0xFF; break
            break
    if base is None or width is None or bound is None: return None
    out = []
    for n in range(bound + 1):
        if width == 2:
            v = word(base + 2 * n); v = v - 0x10000 if v & 0x8000 else v
        else:
            v = img[base + n - 0x06000000]; v = v - 256 if v & 0x80 else v
        out.append(base + v)
    return out

def path_avoiding(entry, target, avoid):
    """a path from entry to target that passes none of 'avoid'; (found, unresolved jumps met)"""
    seen, work, unresolved = {entry}, [(entry, None)], set()
    if entry in avoid: return False, unresolved
    while work:
        a, after_slot = work.pop()
        if a == target: return True, unresolved
        s, extra = succ(a)
        nxt = list(s)
        if after_slot is not None: nxt = list(after_slot)   # the delay slot: on to the branch's destinations
        elif isinstance(extra, str): unresolved.add('%08X %s' % (a, extra))
        for n in nxt:
            if n in avoid or n in seen: continue
            seen.add(n)
            slot = extra[1:] if isinstance(extra, tuple) and after_slot is None else None
            work.append((n, slot))
    return False, unresolved

# "is in" and "may be in" a register or a stack argument slot are one family: A3-108's loop leaves either to the other
def fam(t): return t.replace('a frame address may be in ', 'a frame address is in ')

base, new = texts(sys.argv[2]), texts(sys.argv[3])
names = sys.argv[4:] or sorted(k for k in set(base) | set(new) if base.get(k) != new.get(k))
bad = 0
for k in names:
    bc, bn = split(base.get(k, []))
    nc, nn = split(new.get(k, []))
    entry = int(k, 16)
    out = []
    if bc != nc: out.append('CODE LINES DIFFER'); bad += 1
    bset = {(x[0], x[1], x[3]): x[2] for x in bn}
    nset = {(x[0], x[1], x[3]): x[2] for x in nn}
    for key in sorted(set(bset) | set(nset), key=lambda x: (x[2], x[1])):
        kind, text, call = key
        short = (text[:44] + '..') if len(text) > 46 else text
        if key in bset and key in nset:
            if bset[key] != nset[key]: out.append('  %s %08X likewise %s: %s' % (kind, call, 'gained' if nset[key] else 'lost', short))
            continue
        if key in bset: out.append('  %s %08X DROPPED%s: %s' % (kind, call, ' (likewise)' if bset[key] else '', short)); bad += kind == 'frame'; continue
        line = '  %s %08X added%s: %s' % (kind, call, ' +likewise' if nset[key] else '', short)
        if kind == 'frame':
            same = [x for x in bn if x[0] == kind and fam(x[1]) == fam(text)]
            earlier = [x[3] for x in nn if x[0] == kind and fam(x[1]) == fam(text) and x[3] < call and x[3] != 0]
            if 'stack argument slot' in text:
                # a stack note is said again at a call one more word holds a frame address at (A3-108): the new text may say it at a call its
                # earlier notes dominate. What the old text left it to is the last stack note before it by address (its "likewise"); the
                # call must be reachable without passing that one.
                cover = [x[3] for x in bn if x[0] == kind and fam(x[1]) == fam(text) and x[3] < call]
                earlier = cover[-1:] if cover else []
            # one path for each earlier call: the call is left to a note only when that one call is on every path to it (BlockDominates)
            found, unres = True, set()
            for e in earlier or [None]:
                f, u = path_avoiding(entry, call, {e} if e is not None else set())
                found, unres = found and f, unres | u
            line += ' | the old text says it at %s | a path from the entry avoiding each of %s: %s%s' % (
                ','.join('%08X%s' % (x[3], '+' if x[2] else '') for x in same) or 'NONE',
                ','.join('%08X' % e for e in earlier) or 'nothing earlier', 'yes' if found else 'NO',
                (' (unresolved: ' + '; '.join(sorted(unres)) + ')') if unres and not found else '')
            if not same or not found: bad += 1
        out.append(line)
    print('=== %s%s' % (k, '' if out else ' (no note changed)'))
    for l in out: print(l)
print('checked %d function(s); %d item(s) to read by hand (code changed, a frame note dropped, a fact not in the old text, or no path found)' % (len(names), bad))
