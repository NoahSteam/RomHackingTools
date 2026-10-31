#!/usr/bin/env python3
"""uninit16.py DIR... : scan the lend_after texts of a corpus run for the signature of R16-1/R16-2: a stack local that no statement assigns
whose address is kept in a local pointer variable (`p = &xStack_24;`, or the array's own name) that the text dereferences (`*p`, `p[i]`, `*(T *)*p`).
Such a text reads the local through the pointer and never writes it: the stores the code made to it were dropped. A local also passed to a call by address
(a callee may write it) is not reported. Prints one line per hit."""
import sys, re, os
hits = 0; files = 0
def read_through(body, p):
    """is p dereferenced in a position that reads (the right of an assignment, a condition, a return, an argument)?"""
    deref = re.compile(r'\*\s*\(?[^;=]*\b' + re.escape(p) + r'\b|\b' + re.escape(p) + r'\s*\[')
    for st in re.split(r'[;{}]', body):
        if not deref.search(st): continue
        if re.match(r'^\s*[\w ]+\s*\*+\s*' + re.escape(p) + r'\s*$', st): continue      # its declaration
        mm = re.search(r'(?<![=!<>])=(?!=)', st)
        if mm is None: return True                  # return, if, while, call: a read
        if deref.search(st[mm.end():]): return True # right-hand side
        lhs = st[:mm.start()].strip()
        pure = re.match(r'^\*\s*(\(\s*[\w *]+\)\s*)?\(?\s*' + re.escape(p) + r'\s*\)?(\s*[-+]\s*\w+)?$', lhs) or re.match(r'^' + re.escape(p) + r'\s*\[[^\]]*\]$', lhs)
        if not pure: return True                    # `*(T *)*p = v`: the left side reads p's target to find where to store    return False
for root in sys.argv[1:]:
    for dp, dn, fn in os.walk(root):
        if 'lend_after' not in dp: continue
        for f in sorted(fn):
            if not f.endswith('.c'): continue
            files += 1
            t = open(os.path.join(dp, f), errors='replace').read()
            body = t
            locs = set(re.findall(r'\b([a-z]*Stack_[0-9a-f]+)\b', body))
            for L in sorted(locs):
                # assigned directly (whole, element, or piece)?
                if re.search(r'(^|[;{}]\s*)\s*_?' + re.escape(L) + r'(\s*\[[^\]]*\])?(\._\d+_\d+_)?\s*(=|[-+*/&|^]=)[^=]', body, re.M): continue
                # address kept in a pointer variable
                isarr = re.search(r'\b' + re.escape(L) + r'\s*\[', body) is not None and re.search(r'^\s*[\w ]+\s+' + re.escape(L) + r'\s*\[', body, re.M) is not None
                for m in re.finditer(r'(?<![*\w])([A-Za-z_0-9]+)\s*=\s*(\(\s*[\w *]+\)\s*)?' + ('&?' if isarr else '&') + re.escape(L) + r'\s*;', body):
                    p = m.group(1)
                    if p == L: continue
                    # dereferenced: *p, p[, ->, *(T*)*p, (*p)
                    if read_through(body.replace(m.group(0), ''), p):
                        # the address passed to a call anywhere?  then a callee may write it
                        if re.search(r'\(\s*[^;]*&' + re.escape(L) + r'\b|[\(,]\s*' + re.escape(p) + r'\s*[,\)]', body): continue
                        # ...or stored through (`*p = v`, `p[i] = v`, `*(T *)(p + k) = v`): the local is written there
                        if re.search(r'^\s*\*\s*(\([^)]*\)\s*)?\(?\s*' + re.escape(p) + r'\b[^=;]*=[^=]|^\s*' + re.escape(p) + r'\s*\[[^\]]*\]\s*=[^=]', body, re.M): continue
                        print('%s/%s: %s kept in %s, dereferenced, never assigned' % (os.path.basename(dp), f, L, p)); hits += 1
                        break
print('files %d, hits %d' % (files, hits), file=sys.stderr)
