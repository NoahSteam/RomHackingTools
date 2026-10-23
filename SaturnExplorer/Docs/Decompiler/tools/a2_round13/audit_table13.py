#!/usr/bin/env python3
# Area 2 round 13: the table "every rule audited and its verdict" of the record, from the tools' logs.
# usage: audit_table13.py <vendored cpp dir> <fuzz log dir or file>... -- <rulecheck log>...
#   The fuzz logs hold `RULE <name> tested <n> applied <n> bad <n> thrown <n>` lines (rulefuzz13); the rule check logs hold `RULECHECK <name> applied <n> checked <n>
#   bad <n> unmatched <n> skipped <n>` lines (rulecheck13.hh, the lowercase group names of the pool). Prints a Markdown table, one row per rule of the pool.
import glob, json, os, re, sys

cpp = sys.argv[1]
sep = sys.argv.index('--')
fuzz_logs = []
for a in sys.argv[2:sep]:
    fuzz_logs += glob.glob(os.path.join(a, '*.log')) if os.path.isdir(a) else [a]
rc_logs = []
for a in sys.argv[sep + 1:]:
    rc_logs += glob.glob(os.path.join(a, '*.log')) if os.path.isdir(a) else [a]

src = open(os.path.join(cpp, 'ruleaction.cc')).read()
core = open(os.path.join(cpp, 'coreaction.cc')).read()
briefs = {m.group(1): m.group(2) for m in re.finditer(r'/// \\class (Rule\w+)\n/// \\brief ([^\n]*)', src)}
pool = list(dict.fromkeys(re.findall(r'new (Rule\w+)\(', core)))
# the group name a rule reports to rulecheck13: its Rule constructor's name argument
names = {}
for m in re.finditer(r'(Rule\w+)::\1\(const string &g(?:,[^)]*)?\)\s*:\s*Rule\(\s*g\s*,\s*0\s*,\s*"(\w+)"', open(os.path.join(cpp, 'ruleaction.hh')).read()):
    names[m.group(1)] = m.group(2)
for m in re.finditer(r'class (Rule\w+) : public Rule \{[^}]*?Rule\(\s*g\s*,\s*0\s*,\s*"(\w+)"\)', open(os.path.join(cpp, 'ruleaction.hh')).read(), re.S):
    names[m.group(1)] = m.group(2)

fz = {}
for fn in fuzz_logs:
    for line in open(fn, errors='replace'):
        m = re.match(r'RULE (\w+)\s+tested\s+(\d+) applied\s+(\d+) bad\s+(\d+) thrown\s+(\d+)', line)
        if m:
            t = fz.setdefault(m.group(1), [0, 0, 0])
            t[0] += int(m.group(3)); t[1] += int(m.group(4)); t[2] += int(m.group(5))
rc = {}
for fn in rc_logs:
    for line in open(fn, errors='replace'):
        m = re.match(r'RULECHECK (\S+)\s+applied\s+(\d+) checked\s+(\d+) bad\s+(\d+) unmatched\s+(\d+) skipped\s+(\d+)', line)
        if m:
            t = rc.setdefault(m.group(1), [0, 0, 0])
            t[0] += int(m.group(2)); t[1] += int(m.group(3)); t[2] += int(m.group(4))

verdicts = json.load(open(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'verdicts13.json'))) if os.path.exists(os.path.join(os.path.dirname(os.path.abspath(__file__)), 'verdicts13.json')) else {}

print('| Rule | What it does | By value, graphs (applied, wrong) | In the engine (applied, checked, wrong) | Verdict |')
print('|---|---|---|---|---|')
for r in pool:
    b = briefs.get(r, '').replace('|', '\\|')
    f = fz.get(r)
    n = names.get(r, r[4:].lower())
    c = rc.get(n)
    fs = '%d, %d' % (f[0], f[1]) if f else '-'
    cs = '%d, %d, %d' % (c[0], c[1], c[2]) if c else '-'
    v = verdicts.get(r)
    if v is None:
        seen = (f is not None and f[0] > 0) or (c is not None and c[1] > 0)
        v = 'cleared: read, and checked by value where it applies' if seen else 'read; no run applied it, so not checked by value (the workloads do not reach it)'
        if f is not None and f[1] > 0 or c is not None and c[2] > 0: v = 'WRONG: see the ledger'

    print('| `%s` | %s | %s | %s | %s |' % (r, b, fs, cs, v.replace('|', '\\|')))
