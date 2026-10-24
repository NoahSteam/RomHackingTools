#!/usr/bin/env python3
"""mkrows.py rows.json >> REVIEW_LEDGER.md : ledger rows with exactly eight cells (id | area | severity | finding | fixture | status | found | closed).
Every `|` inside a cell, code spans included, is written `\\|`; a newline in a cell is a space. rows.json is a list of objects with those keys."""
import json, sys

def cell(s):
    return ' '.join(str(s).split('\n')).replace('|', '\\|').strip()

rows = json.load(open(sys.argv[1]))
for r in rows:
    cells = [r['id'], r.get('area', '3'), r['sev'], r['finding'], r['fixture'], r.get('status', 'open'), r.get('found', 'area 3 round 13 review'), r.get('closed', '')]
    print('| ' + ' | '.join(cell(c) for c in cells) + ' |')
