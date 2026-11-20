import json, re, sys
tools = sys.argv[1]
res = {}
for l in open(tools + '/results.tsv').read().splitlines()[1:]:
    c = l.split('\t')
    res[c[0]] = (c[2], c[3] if len(c) > 3 else '')
j = json.load(open('/tmp/claude-0/s/jobs_early.json'))
skip = {'0001', '0040', '0052', '0060', '0010', '0028', '0038', '0044', '0056', '0057', '0065', '0066', '0072', '0076'}
out = []
for x in j:
    p = x['patch']
    if p in skip:
        continue
    v = res.get(p)
    if not v or v[0] not in ('named', 'other'):
        continue
    seen = []
    for o in re.findall(r'(\w+)\(rc', v[1]):
        if o not in seen:
            seen.append(o)
    x['runs'] = seen
    out.append(x)
from collections import Counter
c = Counter(x['patch'] for x in out)
multi = [x for x in out if c[x['patch']] > 1]
print(len(out), len(multi))
for x in multi:
    print(x['id'], x['runs'], x['hunks'], x['where'][:50])
json.dump(multi, open('/tmp/claude-0/s/jobs_B.json', 'w'))
