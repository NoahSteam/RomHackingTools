import os, sys, collections
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import token_semantics as ts
path, entry = sys.argv[1], sys.argv[2]
mode = sys.argv[3] if len(sys.argv)>3 else 'brief'
for fn in ts.load(path):
    if fn['entry']!=entry: continue
    byline=collections.defaultdict(list)
    for t in fn['tokens']: byline[t['line']].append(t)
    for li in sorted(fn['lines']):
        fa,text=fn['lines'][li]
        toks=byline.get(li,[])
        print(f'{li:3d} {text}')
        if toks:
            seen=collections.OrderedDict()
            for t in toks:
                seen.setdefault((t['addr'],t['insn']),[]).append(t['text'] if t['b']!=t['e'] else '<empty>')
            print('      ' + ' | '.join(f'{a} {i}: ' + ','.join(v[:6]) for (a,i),v in seen.items()))
    break
