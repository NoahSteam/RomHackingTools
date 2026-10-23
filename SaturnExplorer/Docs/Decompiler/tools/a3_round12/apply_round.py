#!/usr/bin/env python3
"""apply_round.py <scratch dir with section_head.md section_body.md section_tail.md rows.md> : append the round's rows to the ledger and its record to the step 5 document."""
import sys, os
S = sys.argv[1]
root = os.path.abspath(os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', '..'))
ledger = os.path.join(root, 'Docs', 'Decompiler', 'REVIEW_LEDGER.md')
doc = os.path.join(root, 'Docs', 'FunctionalityVerification', '15_DECOMPILER_STEP5.md')
rows = open(os.path.join(S, 'rows.md')).read().rstrip('\n')
l = open(ledger).read()
assert 'A3-89 |' not in l, 'rows already applied'
if not l.endswith('\n'): l += '\n'
open(ledger, 'w').write(l + rows + '\n')
sec = ''.join(open(os.path.join(S, n)).read().rstrip('\n') + '\n\n' for n in ('section_head.md', 'section_body.md', 'section_tail.md'))
d = open(doc).read()
assert '## Area 3, round 12 review' not in d, 'section already applied'
if not d.endswith('\n'): d += '\n'
open(doc, 'w').write(d + '\n' + sec.rstrip('\n') + '\n')
print('ok')
