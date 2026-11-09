#!/usr/bin/env python3
"""Resolve merge conflicts in the append-mostly review docs by keeping both sides (ours, then theirs).
Run from the repo root during a conflicted merge. Only touches the two doc files; other conflicts stay."""
import re, subprocess
FILES = ['SaturnExplorer/Docs/Decompiler/REVIEW_LEDGER.md',
         'SaturnExplorer/Docs/FunctionalityVerification/15_DECOMPILER_STEP5.md']
unmerged = subprocess.run(['git', 'diff', '--name-only', '--diff-filter=U'], capture_output=True, text=True).stdout.split()
for f in FILES:
    if f not in unmerged:
        continue
    out, state, ours, theirs = [], 0, [], []
    for line in open(f, encoding='utf-8').read().split('\n'):
        if state == 0 and line.startswith('<<<<<<< '):
            state, ours, theirs = 1, [], []
        elif state == 1 and line.startswith('||||||| '):
            state = 3
        elif state in (1, 3) and line == '=======':
            state = 2
        elif state == 2 and line.startswith('>>>>>>> '):
            # ledger: a row in both sides with the same ID -> keep theirs (the branch's newer status)
            if f.endswith('REVIEW_LEDGER.md'):
                tid = {re.match(r'\| (A\d+-\d+) ', l).group(1) for l in theirs if re.match(r'\| A\d+-\d+ ', l)}
                ours = [l for l in ours if not (re.match(r'\| (A\d+-\d+) ', l) and re.match(r'\| (A\d+-\d+) ', l).group(1) in tid)]
            out += ours + theirs
            state = 0
        elif state == 1:
            ours.append(line)
        elif state == 2:
            theirs.append(line)
        elif state == 3:
            pass
        else:
            out.append(line)
    open(f, 'w', encoding='utf-8').write('\n'.join(out))
    subprocess.run(['git', 'add', f])
    print('ledger resolved' if 'LEDGER' in f else 'record resolved')
