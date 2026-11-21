import json
j = json.load(open('/tmp/claude-0/r6/jobs_gen.json'))
out = {'A': [], 'B': [], 'C': [], 'D': []}
for x in j:
    p = x['patch']
    if p in ('0006', '0050'):
        x['runs'] = ['unit', 'sem', 'gen', 'fault']
        out['A'].append(x)
    elif p in ('0023', '0032'):
        x['runs'] = ['fault', 'pc', 'engine', 'unit', 'gen']
        out['B'].append(x)
    elif p == '0001':
        x['runs'] = ['unit', 'pc', 'engine', 'gen', 'runner', 'fault']
        out['C'].append(x)
    elif p == '0040':
        x['runs'] = ['fault', 'pc', 'engine', 'unit', 'gen']
        out['C'].append(x)
    else:
        x['runs'] = ['pc', 'engine', 'unit', 'gen', 'fault']
        out['C'].append(x)
for n in ('1', '3', '4'):
    out['D'].append({"id": "0015-g%sf" % n, "patch": "0015", "hunks": [int(n)], "named": [], "runs": ["fault"], "where": "0015 group %s" % n})
for k, v in out.items():
    json.dump(v, open('/tmp/claude-0/r6/jobs_%s.json' % k, 'w'))
    print(k, len(v))
