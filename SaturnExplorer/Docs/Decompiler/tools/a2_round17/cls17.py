#!/usr/bin/env python3
# usage: cls17.py <log> [class]   -> every function of a printed-C differential log whose verdict is <class> (default: anything but agrees / skipped / narrow-c / void-return),
# with its text on one line. For narrow-c, the known classes (a -0x80000000 literal read as C, a narrow value negated or complemented under a wider cast) are marked "known".
import re, sys
log = open(sys.argv[1], errors='replace').read().split('\n')
want = sys.argv[2] if len(sys.argv) > 2 else None
head = re.compile(r'^(hand\d+|[A-Za-z0-9_.-]+): (\S+)')
items = []
cur = None
for ln in log:
    m = head.match(ln)
    if m and (m.group(2) in ('agrees', 'narrow-c', 'mismatch', 'crashed', 'timeout', 'not-compilable', 'void-return', 'frame-lost') or m.group(2).startswith('skipped') or m.group(2).startswith('void-return')):
        cur = [m.group(1), m.group(2), ln, []]
        items.append(cur)
    elif cur is not None and not ln.startswith('printed-C:'):
        cur[3].append(ln)
for n, cls, ln, text in items:
    if want is None:
        if cls in ('agrees', 'narrow-c') or cls.startswith('skipped') or cls.startswith('void-return'): continue
    elif cls != want: continue
    t = ' '.join(x.strip() for x in text if x.strip() and not x.startswith('/*'))
    known = ''
    if cls == 'narrow-c':
        if '-0x80000000' in t: known = 'known(-0x80000000)'
        elif re.search(r'\(u?int[24]\)-', t) or re.search(r'\)~', t) or re.search(r'\(u?int2\)\(int2\)-', t): known = 'known(negate/not)'
        elif re.search(r'(?<![\w)\]\s])\s*-\s*\(*\s*(\(\w+\)\s*)*(param_\d|in_r\d|unaff_r\d|[a-z]Var\d)', t) or re.search(r'[<>=(,!] ?-\(\(?\w* ?\w*\)? ?\(?(param|in_)', t): known = 'known(negate)'
    print('%s %s %s | %s' % (n, cls, known, t[:600]))
