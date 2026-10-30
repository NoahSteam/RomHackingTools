import os, sys, collections, re
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import token_semantics as ts
CALLS={'jsr','bsr','bsrf'}
hw=open(sys.argv[2],'rb').read()
def word(a): o=a-0x06000000; return hw[o]<<8|hw[o+1]
def is_store(w):
    hi=w>>12
    if hi==1: return True
    if hi==2 and (w&0xF) in (0,1,2,4,5,6): return True
    if hi==0 and (w&0xF) in (4,5,6): return True   # mov.x Rm,@(R0,Rn)
    if (w>>8) in (0x80,0x81): return True
    if (w>>8) in (0xC0,0xC1,0xC2): return True
    return False
funcs=ts.load(sys.argv[1])
n=0; nonstore=0; ex=[]
for fn in funcs:
    byline=collections.defaultdict(list)
    for t in fn['tokens']: byline[t['line']].append(t)
    for li,toks in byline.items():
        text=fn['lines'][li][1]
        if not (re.match(r'^\s*\w*Stack\w*(\._\d+_\d+_)?(\[\d+\])? = ',text) or re.match(r'^\s*_\w*Stack\w* = ',text)): continue
        if all(ts.mnem(t['insn']) in CALLS for t in toks) and not any(t['kind']=='FuncName' for t in toks):
            n+=1
            a=int(toks[0]['addr'],16)
            if not is_store(word(a+2)):
                nonstore+=1
                if len(ex)<6: ex.append((fn['entry'],text.strip(),toks[0]['addr'],toks[0]['insn'],'slot=%04X'%word(a+2)))
print('frame-local assignments addressed only to a call:',n,'; the call\'s delay slot is no store:',nonstore)
for e in ex: print(e)
