import os, sys, collections
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import token_semantics as ts
DELAYED={'bra','bsr','jmp','jsr','rts','rte','braf','bsrf','bt/s','bf/s'}
funcs=ts.load(sys.argv[1])
tot=0; allbr=0; ctrl=0; slotmade=0; ex=[]
for fn in funcs:
    byline=collections.defaultdict(list)
    for t in fn['tokens']: byline[t['line']].append(t)
    for li,toks in byline.items():
        tot+=1
        if all(ts.mnem(t['insn']) in DELAYED for t in toks):
            allbr+=1
            if any(t['kind'] in('Keyword','FuncName') for t in toks): ctrl+=1
            else:
                slotmade+=1
                if len(ex)<12: ex.append((fn['entry'],fn['lines'][li][1],[ (t['addr'],t['insn']) for t in toks][:2]))
print('lines with addr',tot,'all tokens at delayed branches',allbr,'of them with keyword/call token',ctrl,'without (made by the slot or a copy)',slotmade)
for e in ex: print(e)
