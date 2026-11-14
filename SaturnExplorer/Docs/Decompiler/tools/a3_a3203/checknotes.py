#!/usr/bin/env python3
"""Area 3, A3-203 and A3-204: checks each function text that differs between two token dumps (a7_round2/corpus.sh's <tag>_alone.tsv or
_lent.tsv) against the hwram image. For each function: whether any line but a comment changed (it must not); each note (A3-108's frame
notes, A3-197's cautions) by its text and the call printed after it (the line's call instruction), added or dropped (none may be), or with its
"likewise" changed. A3-204 says a frame note once, as before, and turns "likewise at the calls below" into "likewise at other calls of this
function" when a call left to it is not on every path after it: for each such note the image's CFG (branches with their delay slots; calls
fall through; rts ends; compiled `mova ; mov.w @(r0,rI) ; add ; jmp @r0` switches read from their tables; other `jmp` and `braf` end the walk
and are reported) must hold a call at a higher address (A3-108 leaves only those to a note) that a path from the entry reaches without
passing the note's call. The calls listed are candidates: the calls the note was left to are among those the probe found the fact at.
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
            lw, base = '', c
            for suffix, tag in (('; likewise at the calls below', 'below'), ('; likewise at other calls of this function', 'other')):
                if c.endswith(suffix): lw, base = tag, c[:-len(suffix)]
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

CALL = lambda i: i is not None and ((i & 0xF0FF) == 0x400B or i >> 12 == 0xB or (i & 0xF0FF) == 0x0003)

def calls_avoiding(entry, avoid):
    """the calls a path from the entry reaches without passing 'avoid', and the unresolved jumps met"""
    seen, work, unresolved, calls = {entry}, [(entry, None)], set(), set()
    while work:
        a, after_slot = work.pop()
        if CALL(word(a)): calls.add(a)
        s, extra = succ(a)
        nxt = list(s)
        if after_slot is not None: nxt = list(after_slot)
        elif isinstance(extra, str): unresolved.add('%08X %s' % (a, extra))
        for n in nxt:
            if n == avoid or n in seen: continue
            seen.add(n)
            slot = extra[1:] if isinstance(extra, tuple) and after_slot is None else None
            work.append((n, slot))
    return calls, unresolved

base, new = texts(sys.argv[2]), texts(sys.argv[3])
names = sys.argv[4:] or sorted(k for k in set(base) | set(new) if base.get(k) != new.get(k))
bad, changed = 0, {'below>other': 0, 'other': 0}
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
        if key not in bset or key not in nset:
            out.append('  %s %08X %s: %s' % (kind, call, 'ADDED' if key in nset else 'DROPPED', short)); bad += 1; continue
        if bset[key] == nset[key]: continue
        line = '  %s %08X likewise %s -> %s: %s' % (kind, call, bset[key] or 'none', nset[key] or 'none', short)
        if (bset[key], nset[key]) == ('below', 'other') and kind == 'frame':
            changed['below>other'] += 1
            calls, unres = calls_avoiding(entry, call)
            later = sorted(c for c in calls if c > call)
            line += ' | later calls reached without it: %s%s' % (
                (' '.join('%08X' % c for c in later[:6]) + (' ...(%d)' % len(later) if len(later) > 6 else '')) if later else 'NONE',
                (' (unresolved: ' + '; '.join(sorted(unres)) + ')') if unres and not later else '')
            if not later: bad += 1
        else:
            changed['other'] += 1; bad += 1
        out.append(line)
    print('=== %s%s' % (k, '' if out else ' (no note changed)'))
    for l in out: print(l)
print('checked %d function(s): %d note(s) below -> other; %d item(s) to read by hand (code changed, a note added or dropped, another change, '
      'or no later call reached without the note\'s)' % (len(names), changed['below>other'], bad))
