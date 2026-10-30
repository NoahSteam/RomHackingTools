import os, sys, collections, re
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import token_semantics as ts
funcs=ts.load(sys.argv[1])
n=0; fns=set(); tot=0
for fn in funcs:
    byline=collections.defaultdict(list)
    for t in fn['tokens']: byline[t['line']].append(t)
    for li,toks in byline.items():
        tot+=1
        text=fn['lines'][li][1]
        m=re.match(r'^\s*_?\w*Stack\w*(\._\d+_\d+_)? = ',text)
        if not m: continue
        eq=[t for t in toks if t['text']=='=' ]
        if eq and all(re.match(r'mov\.[bwl] @',t['insn']) and ',' in t['insn'] and not re.search(r',@',t['insn']) for t in eq):
            n+=1; fns.add(fn['entry'])
print('frame-local assignments whose = comes from a load instruction:',n,'in',len(fns),'functions; addressed lines',tot)
