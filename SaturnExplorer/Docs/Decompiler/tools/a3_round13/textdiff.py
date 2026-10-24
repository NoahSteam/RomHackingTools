#!/usr/bin/env python3
"""textdiff.py base_dir new_dir [-l kind] [-n N]  (the lend_after directories of two corpus runs: SE_GAME_OUT/lend_after)

Classifies every text that differs between two runs. A text can be in several kinds:
  notes     the comment blocks (`/* ... */`, which carry the frame notes) differ
  decl      the declarations differ (a local kept as a variable, a new array)
  stmt      the statements differ (a read of a local where a constant was folded, a store kept)
  sig       the signature line (or the `// signature withheld` header) differs
  array     a frame array (`[N]` declaration) appears or changes
  instack   an `in_stack_` name appears or disappears
  only-notes  notes are the only difference
With -l kind, lists the functions of that kind. Output: counts, then the list when asked."""
import sys, os, re, collections

def norm(t):
    """split a text into (header, signature, decls, stmts, notes) with the address comments (`; 0600xxxx`) dropped"""
    notes = re.findall(r'/\*.*?\*/', t, re.S)
    body = re.sub(r'/\*.*?\*/', '', t, flags=re.S)
    lines = []
    for l in body.splitlines():
        l = re.sub(r'\s*;\s*[0-9A-Fa-f]{8}\s*$', ';', l)    # the address after the statement
        l = l.rstrip()
        if l.strip(): lines.append(l)
    header = [l for l in lines if l.startswith('//')]
    rest = [l for l in lines if not l.startswith('//')]
    sig = rest[0] if rest else ''
    decls, stmts = [], []
    inbody = False
    for l in rest[1:]:
        s = l.strip()
        if s == '{': inbody = True; continue
        if re.fullmatch(r'[A-Za-z_][A-Za-z_0-9 \*]*\s\**[A-Za-z_][A-Za-z_0-9]*(\s*\[\w+\])?;', s) and not re.search(r'\breturn\b|=', s):
            decls.append(s)
        else:
            stmts.append(s)
    return header, sig, decls, stmts, [re.sub(r'\s+', ' ', n) for n in notes]

def kinds(a, b):
    ha, sa, da, ta, na = norm(a); hb, sb, db, tb, nb = norm(b)
    k = set()
    if na != nb: k.add('notes')
    if sorted(da) != sorted(db): k.add('decl')
    if ta != tb: k.add('stmt')
    if sa != sb or ha != hb: k.add('sig')
    if any('[' in x for x in da + db) and sorted(x for x in da if '[' in x) != sorted(x for x in db if '[' in x): k.add('array')
    if ('in_stack_' in a) != ('in_stack_' in b): k.add('instack')
    if k == {'notes'}: k.add('only-notes')
    return k

if __name__ == '__main__':
    A, B = sys.argv[1], sys.argv[2]
    want = None; N = 40
    if '-l' in sys.argv: want = sys.argv[sys.argv.index('-l') + 1]
    if '-n' in sys.argv: N = int(sys.argv[sys.argv.index('-n') + 1])
    cnt = collections.Counter(); lst = collections.defaultdict(list); total = 0; diff = 0
    for f in sorted(os.listdir(B)):
        if not f.endswith('.c') or not os.path.exists(os.path.join(A, f)): continue
        a = open(os.path.join(A, f)).read(); b = open(os.path.join(B, f)).read()
        total += 1
        if a == b: continue
        diff += 1
        ks = kinds(a, b)
        if not ks: ks = {'address-comments-only'}
        for k in ks: cnt[k] += 1; lst[k].append(f)
    print('compared', total, 'differ', diff)
    for k, v in cnt.most_common(): print('  %-24s %d' % (k, v))
    if want:
        for f in lst[want][:N]: print(f)
